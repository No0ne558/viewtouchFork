/*
 * Tests for the SQLite backend behind the Store seam.
 *
 * Two things are being established here, and they are different in kind.
 *
 * The first is that the SQL backend honours the same interface contract as the
 * legacy one, so a call site cannot tell them apart -- which is the whole
 * precondition for the dual-run comparison. Those expectations are deliberately
 * written to mirror test_store_seam.cc, with the one documented exception:
 * SupportsAtomicWrites() differs, because that is the guarantee the migration
 * exists to gain.
 *
 * The second is that a saved check actually lands in the right rows. Those
 * assertions query the tables directly rather than reading back through a
 * reader in this same file: a round trip through code written by the same hand
 * as the writer proves the two agree, not that either is correct.
 */

#include <catch2/catch_all.hpp>
#include "main/data/store/store.hh"

#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/archive.hh"
#include "sql/database.hh"
#include "sql/migrations.hh"
#include "support/vt_test_env.hh"

#include <memory>
#include <string>

using namespace vt::store;

namespace {

// Opens a second connection to the same file so the tests can inspect what the
// store wrote. In-memory databases are per-connection, so anything that needs
// to be queried has to live in a real file.
struct TempDb
{
    std::string path;

    explicit TempDb(const std::string &name)
        : path("/tmp/" + name + ".vtdb")
    {
        Remove();
    }
    ~TempDb() { Remove(); }

    void Remove() const
    {
        for (const char *suffix : {"", "-wal", "-shm"})
            std::remove((path + suffix).c_str());
    }
};

// Runs a scalar query on an independent connection. Deliberately does not reuse
// anything the store owns.
int64_t Scalar(const std::string &path, const std::string &sql)
{
    vt::sql::Database db;
    REQUIRE(db.Open(path) == vt::sql::Status::Ok);
    int64_t value = -1;
    REQUIRE(db.QueryInt(sql, value) == vt::sql::Status::Ok);
    return value;
}

std::unique_ptr<Store> OpenStore(const std::string &path)
{
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(path, error);
    REQUIRE(error == StoreError::None);
    REQUIRE(store != nullptr);
    return store;
}

// A check with two subchecks, a modifier tree and a payment -- enough shape
// that a mistake in the tree or the ordering has somewhere to show up.
Check *BuildCheck(int serial)
{
    auto *check = new Check;
    check->serial_number = serial;
    check->type = CHECK_TAKEOUT;
    check->guests = 2;
    check->label.Set("table 7");

    SubCheck *sub = check->NewSubCheck();

    auto *burger = new Order("Burger", 950);
    burger->item_family = FAMILY_BURGERS;   // absent from the 0001 seed
    burger->seat = 1;
    sub->Add(burger);

    auto *cheese = new Order("Add Cheese", 100);
    cheese->item_family = FAMILY_MODIFIER;
    cheese->call_order = 3;
    burger->Add(cheese);

    auto *bacon = new Order("Add Bacon", 200);
    bacon->item_family = FAMILY_MODIFIER;
    bacon->call_order = 7;
    burger->Add(bacon);

    auto *soup = new Order("Soup", 450);
    soup->item_family = FAMILY_SOUP;        // also absent from the 0001 seed
    soup->seat = 2;
    sub->Add(soup);

    auto *payment = new Payment(TENDER_CASH, 0, 0, 1700);
    sub->Add(payment);

    sub->total_sales = 1700;
    sub->total_tax_food = 140;
    sub->total_tax_VAT = 55;
    sub->total_cost = 1895;

    return check;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The SQLite store honours the same contract as the legacy one",
                 "[store][sqlite]")
{
    TempDb db("vt_sqlite_contract");
    auto store = OpenStore(db.path);

    SECTION("it names itself for logs and divergence reports")
    {
        REQUIRE(std::string(store->Name()) == "sqlite");
    }

    SECTION("it claims atomic writes, unlike the legacy backend")
    {
        // The one interface answer the two backends are expected to differ on.
        REQUIRE(store->SupportsAtomicWrites());
    }

    SECTION("health check passes on a freshly migrated database")
    {
        REQUIRE(store->HealthCheck() == StoreError::None);
    }

    SECTION("a transaction can be begun, committed and rolled back")
    {
        auto tx = store->Begin();
        REQUIRE(tx != nullptr);
        REQUIRE(tx->IsActive());
        REQUIRE(tx->Commit() == StoreError::None);
        REQUIRE_FALSE(tx->IsActive());

        auto second = store->Begin();
        REQUIRE(second->IsActive());
        second->Rollback();
        REQUIRE_FALSE(second->IsActive());
    }

    SECTION("counting an empty database yields zero")
    {
        int count = -1;
        REQUIRE(store->Checks().Count(count) == StoreError::None);
        REQUIRE(count == 0);
    }

    SECTION("an archived check is refused rather than written to the wrong day")
    {
        TimeInfo when;
        when.Set();
        Archive archive(when);
        archive.changed = 0;

        Check check;
        check.archive = &archive;

        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, check) == StoreError::Unsupported);

        check.archive = nullptr;
        archive.changed = 0;
    }

    SECTION("a copy is accepted but not written, matching every other backend")
    {
        Check check;
        check.copy = 1;

        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, check) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);

        int count = -1;
        REQUIRE(store->Checks().Count(count) == StoreError::None);
        REQUIRE(count == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A saved check lands in the right rows",
                 "[store][sqlite][checks]")
{
    TempDb db("vt_sqlite_rows");
    auto store = OpenStore(db.path);

    std::unique_ptr<Check> check(BuildCheck(4242));

    auto tx = store->Begin();
    REQUIRE(store->Checks().Save(*tx, *check) == StoreError::None);
    REQUIRE(tx->Commit() == StoreError::None);

    SECTION("the check row carries its identity and type")
    {
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM pos_check WHERE serial_number = 4242;") == 1);
        REQUIRE(Scalar(db.path,
                       "SELECT type FROM pos_check WHERE serial_number = 4242;")
                == CHECK_TAKEOUT);
        REQUIRE(Scalar(db.path,
                       "SELECT guests FROM pos_check WHERE serial_number = 4242;") == 2);
    }

    SECTION("the check type resolves to the right name")
    {
        // check_type_ref was seeded from 0 in migration 0002 while CheckType
        // starts at 1, so before migration 0003 a Takeout check (2) resolved to
        // 'DELIVERY'. This is the assertion that catches a shifted lookup.
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM pos_check c "
                       "JOIN check_type_ref r ON r.id = c.type "
                       "WHERE c.serial_number = 4242 AND r.code = 'TAKEOUT';") == 1);
    }

    SECTION("the order tree is stored explicitly, not left to be inferred")
    {
        // Two root orders, each modifier pointing at its parent by id. Legacy
        // wrote a flat run and rebuilt this from adjacency on load.
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM order_item WHERE parent_order_id IS NULL;")
                == 2);
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM order_item o "
                       "JOIN order_item p ON p.id = o.parent_order_id "
                       "WHERE p.item_name = 'Burger';") == 2);
    }

    SECTION("root order sequence preserves list order")
    {
        REQUIRE(Scalar(db.path,
                       "SELECT seq FROM order_item WHERE item_name = 'Burger';") == 0);
        REQUIRE(Scalar(db.path,
                       "SELECT seq FROM order_item WHERE item_name = 'Soup';") == 1);
    }

    SECTION("call_order is persisted, which the legacy writer never did")
    {
        // Order::Write never emitted call_order even though Order::Add sorts
        // modifiers by it, so modifiers silently reordered across a save/load
        // and changed kitchen ticket order.
        REQUIRE(Scalar(db.path,
                       "SELECT call_order FROM order_item WHERE item_name = 'Add Cheese';")
                == 3);
        REQUIRE(Scalar(db.path,
                       "SELECT call_order FROM order_item WHERE item_name = 'Add Bacon';")
                == 7);
    }

    SECTION("families outside the original seed are accepted")
    {
        // FAMILY_BURGERS and FAMILY_SOUP were both missing from item_family
        // until migration 0003, so these two inserts failed the foreign key.
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM order_item o "
                       "JOIN item_family f ON f.id = o.item_family "
                       "WHERE f.code IN ('BURGERS','SOUP');") == 2);
    }

    SECTION("the payment is stored with its raw tender id")
    {
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM payment;") == 1);
        REQUIRE(Scalar(db.path, "SELECT amount FROM payment;") == 1700);
        REQUIRE(Scalar(db.path, "SELECT tender_type FROM payment;") == TENDER_CASH);
        REQUIRE(Scalar(db.path, "SELECT synthesized FROM payment;") == 0);
    }

    SECTION("totals are stored rather than left to be recomputed")
    {
        REQUIRE(Scalar(db.path, "SELECT total_sales FROM subcheck_total;") == 1700);
        REQUIRE(Scalar(db.path, "SELECT total_cost FROM subcheck_total;") == 1895);
        // The bug that made freezing necessary: EndDay never snapshotted
        // tax_VAT, so every archived check recomputed it as zero.
        REQUIRE(Scalar(db.path, "SELECT tax_VAT FROM subcheck_total;") == 55);
    }

    SECTION("Count reports what is in the database")
    {
        int count = -1;
        REQUIRE(store->Checks().Count(count) == StoreError::None);
        REQUIRE(count == 1);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The aggregate is the unit of change",
                 "[store][sqlite][atomicity]")
{
    TempDb db("vt_sqlite_aggregate");
    auto store = OpenStore(db.path);

    SECTION("re-saving replaces the children rather than duplicating them")
    {
        std::unique_ptr<Check> check(BuildCheck(11));

        auto first = store->Begin();
        REQUIRE(store->Checks().Save(*first, *check) == StoreError::None);
        REQUIRE(first->Commit() == StoreError::None);

        auto second = store->Begin();
        REQUIRE(store->Checks().Save(*second, *check) == StoreError::None);
        REQUIRE(second->Commit() == StoreError::None);

        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM pos_check;") == 1);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM subcheck;") == 1);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM order_item;") == 4);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM payment;") == 1);
    }

    SECTION("a rolled back save leaves nothing behind")
    {
        // This is the guarantee the legacy backend structurally cannot offer.
        // There, each of these rows is its own already-renamed file by the time
        // anything could go wrong.
        std::unique_ptr<Check> check(BuildCheck(12));

        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, *check) == StoreError::None);
        tx->Rollback();

        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM pos_check;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM subcheck;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM order_item;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM payment;") == 0);
    }

    SECTION("removing a check takes its whole tree with it")
    {
        std::unique_ptr<Check> check(BuildCheck(13));

        auto save = store->Begin();
        REQUIRE(store->Checks().Save(*save, *check) == StoreError::None);
        REQUIRE(save->Commit() == StoreError::None);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM order_item;") == 4);

        auto remove = store->Begin();
        REQUIRE(store->Checks().Remove(*remove, *check) == StoreError::None);
        REQUIRE(remove->Commit() == StoreError::None);

        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM pos_check;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM subcheck;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM order_item;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM payment;") == 0);
        REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM subcheck_total;") == 0);
    }

    SECTION("a check with no serial number is allocated one")
    {
        // System::SaveCheck assigned a serial when absent; the SQL path uses the
        // sequence table instead of the in-memory counter that was never
        // persisted and could hand out numbers that already existed.
        std::unique_ptr<Check> check(new Check);
        REQUIRE(check->serial_number == 0);

        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, *check) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);

        REQUIRE(check->serial_number > 0);
        REQUIRE(Scalar(db.path,
                       "SELECT COUNT(*) FROM pos_check WHERE serial_number = " +
                       std::to_string(check->serial_number) + ";") == 1);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Frozen money cannot be rewritten",
                 "[store][sqlite][frozen]")
{
    TempDb db("vt_sqlite_frozen");
    auto store = OpenStore(db.path);

    std::unique_ptr<Check> check(BuildCheck(77));

    auto save = store->Begin();
    REQUIRE(store->Checks().Save(*save, *check) == StoreError::None);
    REQUIRE(save->Commit() == StoreError::None);

    // Nothing in this PR freezes automatically -- auto-freezing on close would
    // look right and would break the reopen path, since a settled subcheck can
    // go back to open. Freezing here by hand is how the guard gets exercised
    // before the end-of-day operation that will set it for real.
    REQUIRE(Scalar(db.path, "SELECT COUNT(*) FROM subcheck "
                            "WHERE frozen_at_local IS NOT NULL;") == 0);
    {
        vt::sql::Database direct;
        REQUIRE(direct.Open(db.path) == vt::sql::Status::Ok);
        REQUIRE(direct.Exec("UPDATE subcheck SET frozen_at_local = 1700000000;")
                == vt::sql::Status::Ok);
    }

    SECTION("re-saving a frozen check is refused, not silently applied")
    {
        // Rewriting the aggregate deletes the subcheck and inserts a new one,
        // which the frozen-totals trigger cannot catch because it fires on
        // UPDATE. Refusing at the repository is what actually closes that.
        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, *check) == StoreError::Constraint);
        tx->Rollback();

        REQUIRE(Scalar(db.path, "SELECT total_cost FROM subcheck_total;") == 1895);
    }
}

TEST_CASE("The SQLite store reports why it could not open",
          "[store][sqlite][errors]")
{
    StoreError error = StoreError::None;
    auto store = MakeSqliteStore("/nonexistent-directory/vt.db", error);
    REQUIRE(store == nullptr);
    REQUIRE(error != StoreError::None);
}
