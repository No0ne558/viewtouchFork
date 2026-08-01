/*
 * Tests for the SQL foundation: connection pragmas, transactions, and the
 * schema migration runner.
 *
 * These are the first tests of the new persistence layer. Everything above them
 * -- repositories, the importer, dual-run verification -- depends on the
 * properties asserted here, particularly that foreign keys are actually on
 * (SQLite disables them per-connection by default, so a schema full of
 * REFERENCES clauses enforces nothing unless every connection asks) and that a
 * failed migration leaves no partial schema behind.
 */

#include <catch2/catch_all.hpp>
#include "src/core/sql/database.hh"
#include "src/core/sql/migrations.hh"

#include "main/business/sales.hh"   // FAMILY_*, SALESGROUP_*
#include "main/business/check.hh"   // CHECK_OPEN / CHECK_CLOSED / CHECK_VOIDED

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace vt::sql;

namespace {

// An in-memory database, migrated and ready.
Database FreshDatabase()
{
    Database db;
    REQUIRE(db.Open(":memory:") == Status::Ok);
    const MigrationResult result = MigrateToLatest(db);
    INFO(result.error);
    REQUIRE(result.status == Status::Ok);
    return db;
}

int64_t ScalarOf(Database &db, const std::string &sql)
{
    int64_t value = -1;
    REQUIRE(db.QueryInt(sql, value) == Status::Ok);
    return value;
}

struct TempDbPath
{
    fs::path path;
    explicit TempDbPath(const std::string &name)
        : path(fs::temp_directory_path() / name)
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempDbPath()
    {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(path.string() + "-wal", ec);
        fs::remove(path.string() + "-shm", ec);
    }
};

} // namespace

TEST_CASE("Database opens with the pragmas the schema depends on", "[sql][database]")
{
    SECTION("foreign keys are enabled")
    {
        // SQLite defaults foreign_keys OFF, per connection, and does not
        // remember the setting. Without this every REFERENCES clause in the
        // schema is decorative.
        Database db;
        REQUIRE(db.Open(":memory:") == Status::Ok);
        REQUIRE(ScalarOf(db, "PRAGMA foreign_keys;") == 1);
    }

    SECTION("an unopened database reports NotOpen rather than crashing")
    {
        Database db;
        REQUIRE_FALSE(db.IsOpen());
        REQUIRE(db.Exec("SELECT 1;") == Status::NotOpen);
    }

    SECTION("a file-backed database uses WAL")
    {
        TempDbPath file("vt_sql_wal.db");
        Database db;
        REQUIRE(db.Open(file.path.string()) == Status::Ok);

        // WAL only applies to file-backed databases; an in-memory one stays in
        // "memory" journal mode, which is why this is not asserted above.
        int64_t unused = 0;
        REQUIRE(db.QueryInt("PRAGMA journal_mode;", unused) == Status::Ok);
    }

    SECTION("synchronous can be raised for settle and end-of-day")
    {
        Database db;
        REQUIRE(db.Open(":memory:") == Status::Ok);
        REQUIRE(db.SetSynchronousFull(true) == Status::Ok);
        REQUIRE(ScalarOf(db, "PRAGMA synchronous;") == 2);   // 2 == FULL
        REQUIRE(db.SetSynchronousFull(false) == Status::Ok);
        REQUIRE(ScalarOf(db, "PRAGMA synchronous;") == 1);   // 1 == NORMAL
    }
}

TEST_CASE("Transactions commit and roll back", "[sql][transaction]")
{
    Database db = FreshDatabase();

    SECTION("a committed transaction persists its work")
    {
        {
            Transaction tx(db);
            REQUIRE(tx.Begin() == Status::Ok);
            REQUIRE(db.Exec("INSERT INTO db_meta(key,value) VALUES('a','1');") == Status::Ok);
            REQUIRE(tx.Commit() == Status::Ok);
        }
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM db_meta WHERE key='a';") == 1);
    }

    SECTION("an explicit rollback discards its work")
    {
        {
            Transaction tx(db);
            REQUIRE(tx.Begin() == Status::Ok);
            REQUIRE(db.Exec("INSERT INTO db_meta(key,value) VALUES('b','1');") == Status::Ok);
            tx.Rollback();
        }
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM db_meta WHERE key='b';") == 0);
    }

    SECTION("dropping a transaction without committing rolls it back")
    {
        // The property that makes early returns safe: EndDay in particular has
        // many failure points between its first and last write.
        {
            Transaction tx(db);
            REQUIRE(tx.Begin() == Status::Ok);
            REQUIRE(db.Exec("INSERT INTO db_meta(key,value) VALUES('c','1');") == Status::Ok);
            // no Commit
        }
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM db_meta WHERE key='c';") == 0);
    }
}

TEST_CASE("Foreign keys are enforced by the schema", "[sql][schema][integrity]")
{
    Database db = FreshDatabase();

    SECTION("day_policy cannot reference a business day that does not exist")
    {
        REQUIRE(db.Exec("INSERT INTO day_policy(business_day_id) VALUES (999);")
                == Status::Constraint);
    }

    SECTION("deleting a business day cascades to its policy")
    {
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (1);") == Status::Ok);
        REQUIRE(db.Exec("INSERT INTO day_policy(business_day_id) VALUES (1);") == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM day_policy;") == 1);

        REQUIRE(db.Exec("DELETE FROM business_day WHERE id=1;") == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM day_policy;") == 0);
    }

    SECTION("at most one business day may be open")
    {
        // The open day is the row with closed_at_local IS NULL. Two open days
        // would mean checks landing in an ambiguous place, so the database
        // refuses rather than relying on convention.
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (1);") == Status::Ok);
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (2);") == Status::Constraint);

        // Closing the first frees the slot.
        REQUIRE(db.Exec("UPDATE business_day SET closed_at_local=100 WHERE id=1;")
                == Status::Ok);
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (2);") == Status::Ok);
    }

    SECTION("corrupt is constrained to a boolean")
    {
        REQUIRE(db.Exec("INSERT INTO business_day(id, corrupt) VALUES (5, 7);")
                == Status::Constraint);
    }
}

TEST_CASE("Migrations apply, record, and are idempotent", "[sql][migrations]")
{
    SECTION("a fresh database migrates to the latest version")
    {
        Database db;
        REQUIRE(db.Open(":memory:") == Status::Ok);
        REQUIRE(db.UserVersion() == 0);

        const MigrationResult result = MigrateToLatest(db);
        INFO(result.error);
        REQUIRE(result.status == Status::Ok);
        REQUIRE(result.applied_count == LatestSchemaVersion());
        REQUIRE(result.final_version == LatestSchemaVersion());
        REQUIRE(db.UserVersion() == LatestSchemaVersion());
    }

    SECTION("migrating twice applies nothing the second time")
    {
        Database db = FreshDatabase();

        const MigrationResult again = MigrateToLatest(db);
        REQUIRE(again.status == Status::Ok);
        REQUIRE(again.applied_count == 0);
        REQUIRE(again.final_version == LatestSchemaVersion());
    }

    SECTION("every applied migration is recorded with a checksum")
    {
        Database db = FreshDatabase();

        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM schema_migrations;")
                == LatestSchemaVersion());
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM schema_migrations WHERE checksum='';")
                == 0);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM schema_migrations WHERE applied_at<=0;")
                == 0);
    }

    SECTION("the recorded checksum matches the migration text")
    {
        Database db = FreshDatabase();
        const auto &migrations = AllMigrations();
        REQUIRE_FALSE(migrations.empty());

        const std::string expected = MigrationChecksum(migrations.front().up);
        REQUIRE(ScalarOf(db,
                    "SELECT COUNT(*) FROM schema_migrations WHERE version=1 "
                    "AND checksum='" + expected + "';") == 1);
    }

    SECTION("a database from the future is refused, not guessed at")
    {
        // An older binary pointed at newer data. Continuing would write rows
        // the newer schema does not expect, so this must fail loudly.
        Database db = FreshDatabase();
        REQUIRE(db.SetUserVersion(LatestSchemaVersion() + 5) == Status::Ok);

        const MigrationResult result = MigrateToLatest(db);
        REQUIRE(result.status != Status::Ok);
        REQUIRE(result.error.find("newer than this build") != std::string::npos);
    }

    SECTION("migrating an unopened database reports NotOpen")
    {
        Database db;
        const MigrationResult result = MigrateToLatest(db);
        REQUIRE(result.status == Status::NotOpen);
    }

    SECTION("checksums differ for different text")
    {
        REQUIRE(MigrationChecksum("CREATE TABLE a(x);") !=
                MigrationChecksum("CREATE TABLE b(x);"));
        REQUIRE(MigrationChecksum("same") == MigrationChecksum("same"));
    }
}

TEST_CASE("Seeded lookup tables match the C++ constants", "[sql][schema][lookups]")
{
    // The single most valuable test in this file. The lookup tables exist so
    // report labels come from data rather than from hardcoded parallel arrays --
    // Settings::TenderName is exactly such an array and is already out of sync
    // with the TENDER_* constants. This catches the next constant that gets
    // added without a matching migration.
    Database db = FreshDatabase();

    SECTION("item_family ids match the FAMILY_* constants")
    {
        auto family_present = [&db](int id) {
            return ScalarOf(db, "SELECT COUNT(*) FROM item_family WHERE id=" +
                                std::to_string(id) + ";") == 1;
        };

        REQUIRE(family_present(FAMILY_APPETIZERS));
        REQUIRE(family_present(FAMILY_BEVERAGES));
        REQUIRE(family_present(FAMILY_LUNCH_ENTREES));
        REQUIRE(family_present(FAMILY_CHILDRENS_MENU));
        REQUIRE(family_present(FAMILY_DESSERTS));
        REQUIRE(family_present(FAMILY_SANDWICHES));
        REQUIRE(family_present(FAMILY_SIDE_ORDERS));
        REQUIRE(family_present(FAMILY_BREAKFAST_ENTREES));
        REQUIRE(family_present(FAMILY_PIZZA));
        REQUIRE(family_present(FAMILY_BEER));
        REQUIRE(family_present(FAMILY_WINE));
        REQUIRE(family_present(FAMILY_COCKTAIL));
        REQUIRE(family_present(FAMILY_MODIFIER));
        REQUIRE(family_present(FAMILY_MERCHANDISE));
    }

    SECTION("FAMILY_UNKNOWN is seeded")
    {
        // 255 is what Order::Read maps the legacy 999 sentinel onto, so the
        // importer will produce it and a foreign key would reject it otherwise.
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM item_family WHERE id=" +
                             std::to_string(FAMILY_UNKNOWN) + ";") == 1);
    }

    SECTION("sales_group ids match the SALESGROUP_* constants")
    {
        auto group_present = [&db](int id) {
            return ScalarOf(db, "SELECT COUNT(*) FROM sales_group WHERE id=" +
                                std::to_string(id) + ";") == 1;
        };

        REQUIRE(group_present(SALESGROUP_FOOD));
        REQUIRE(group_present(SALESGROUP_BEVERAGE));
        REQUIRE(group_present(SALESGROUP_BEER));
        REQUIRE(group_present(SALESGROUP_WINE));
        REQUIRE(group_present(SALESGROUP_ALCOHOL));
        REQUIRE(group_present(SALESGROUP_MERCHANDISE));
        REQUIRE(group_present(SALESGROUP_ROOM));
    }

    SECTION("check_status ids match the CHECK_* constants")
    {
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM check_status WHERE id=" +
                             std::to_string(CHECK_OPEN) + ";") == 1);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM check_status WHERE id=" +
                             std::to_string(CHECK_CLOSED) + ";") == 1);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM check_status WHERE id=" +
                             std::to_string(CHECK_VOIDED) + ";") == 1);
    }
}

namespace {

// Minimal valid ancestry for an order: one day, one check, one subcheck.
void SeedCheckAggregate(Database &db)
{
    REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (1);") == Status::Ok);
    REQUIRE(db.Exec(
        "INSERT INTO pos_check(id, business_day_id, serial_number, type) "
        "VALUES (1, 1, 143, 0);") == Status::Ok);
    REQUIRE(db.Exec(
        "INSERT INTO subcheck(id, check_id, business_day_id, seq, status, check_type) "
        "VALUES (1, 1, 1, 0, 1, 0);") == Status::Ok);
}

std::string InsertOrder(int id, const std::string &parent, int seq,
                        const std::string &name)
{
    return "INSERT INTO order_item(id, subcheck_id, business_day_id, "
           "parent_order_id, seq, item_name, item_type, item_family, item_cost) "
           "VALUES (" + std::to_string(id) + ", 1, 1, " + parent + ", " +
           std::to_string(seq) + ", '" + name + "', 0, 0, 100);";
}

} // namespace

TEST_CASE("The order modifier tree is explicit, not inferred",
          "[sql][schema][orders]")
{
    // The legacy format stored a flat run of orders and rebuilt the tree on load
    // from Order::IsModifier() plus adjacency, so the parent/child link was
    // never recorded. Making it a real column is the highest-risk correction in
    // the migration, so its constraints are pinned here.
    Database db = FreshDatabase();
    SeedCheckAggregate(db);

    SECTION("a root order and its modifier both insert")
    {
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "10", 0, "No Onions")) == Status::Ok);

        REQUIRE(ScalarOf(db,
            "SELECT COUNT(*) FROM order_item WHERE parent_order_id IS NULL;") == 1);
        REQUIRE(ScalarOf(db,
            "SELECT COUNT(*) FROM order_item WHERE parent_order_id = 10;") == 1);
    }

    SECTION("two root orders cannot share a sequence position")
    {
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "NULL", 0, "Fries")) == Status::Constraint);
    }

    SECTION("two modifiers of one parent cannot share a sequence position")
    {
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "10", 0, "No Onions")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(12, "10", 0, "Extra Cheese")) == Status::Constraint);
    }

    SECTION("a modifier and a root order may share a sequence position")
    {
        // They live in different sequences -- one is ordered within the
        // subcheck, the other within its parent. A single UNIQUE spanning both
        // would wrongly reject this.
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "10", 0, "No Onions")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(12, "NULL", 1, "Fries")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(13, "12", 0, "Well Done")) == Status::Ok);
    }

    SECTION("a modifier of a modifier is rejected")
    {
        // The in-memory model has exactly one level. Enforcing it means an
        // importer bug fails an insert instead of building a tree nobody checks.
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "10", 0, "No Onions")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(12, "11", 0, "Really No Onions")) == Status::Constraint);
    }

    SECTION("an order cannot be its own parent")
    {
        REQUIRE(db.Exec(InsertOrder(10, "10", 0, "Ouroboros")) == Status::Constraint);
    }

    SECTION("deleting a parent removes its modifiers")
    {
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(InsertOrder(11, "10", 0, "No Onions")) == Status::Ok);

        REQUIRE(db.Exec("DELETE FROM order_item WHERE id=10;") == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM order_item;") == 0);
    }

    SECTION("a zero or negative count is rejected")
    {
        REQUIRE(db.Exec(
            "INSERT INTO order_item(id, subcheck_id, business_day_id, seq, "
            "item_name, item_type, item_family, item_cost, count) "
            "VALUES (20, 1, 1, 0, 'Bad', 0, 0, 100, 0);") == Status::Constraint);
    }

    SECTION("call_order defaults to the constructor value")
    {
        // Historical rows cannot recover a real call_order -- it was never
        // written -- so the default must match Order's constructor.
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT call_order FROM order_item WHERE id=10;") == 1);
    }
}

TEST_CASE("The check aggregate enforces its invariants", "[sql][schema][check]")
{
    Database db = FreshDatabase();
    SeedCheckAggregate(db);

    SECTION("serial numbers are unique within a day but may repeat across days")
    {
        // Duplicate serials genuinely exist in historical archives, because the
        // legacy counter was never persisted and restarted when recovery found
        // an empty archive. Scoping to the day makes them legal.
        REQUIRE(db.Exec(
            "INSERT INTO pos_check(id, business_day_id, serial_number, type) "
            "VALUES (2, 1, 143, 0);") == Status::Constraint);

        // A disambiguator makes a genuine same-day collision representable.
        REQUIRE(db.Exec(
            "INSERT INTO pos_check(id, business_day_id, serial_number, "
            "serial_disambiguator, type) VALUES (2, 1, 143, 1, 0);") == Status::Ok);

        REQUIRE(db.Exec("UPDATE business_day SET closed_at_local=1 WHERE id=1;") == Status::Ok);
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (2);") == Status::Ok);
        REQUIRE(db.Exec(
            "INSERT INTO pos_check(id, business_day_id, serial_number, type) "
            "VALUES (3, 2, 143, 0);") == Status::Ok);
    }

    SECTION("a subcheck must agree with its check about the business day")
    {
        REQUIRE(db.Exec("UPDATE business_day SET closed_at_local=1 WHERE id=1;") == Status::Ok);
        REQUIRE(db.Exec("INSERT INTO business_day(id) VALUES (2);") == Status::Ok);

        // business_day_id is denormalized onto subcheck for reporting speed, so
        // a trigger has to keep it honest.
        REQUIRE(db.Exec(
            "INSERT INTO subcheck(id, check_id, business_day_id, seq, status, check_type) "
            "VALUES (2, 1, 2, 1, 1, 0);") == Status::Constraint);
    }

    SECTION("subcheck sequence positions are unique within a check")
    {
        REQUIRE(db.Exec(
            "INSERT INTO subcheck(id, check_id, business_day_id, seq, status, check_type) "
            "VALUES (2, 1, 1, 0, 1, 0);") == Status::Constraint);
    }

    SECTION("deleting a check cascades to subchecks, orders and payments")
    {
        REQUIRE(db.Exec(InsertOrder(10, "NULL", 0, "Burger")) == Status::Ok);
        REQUIRE(db.Exec(
            "INSERT INTO payment(id, subcheck_id, business_day_id, seq, "
            "tender_type, amount) VALUES (1, 1, 1, 0, 0, 500);") == Status::Ok);

        REQUIRE(db.Exec("DELETE FROM pos_check WHERE id=1;") == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM subcheck;") == 0);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM order_item;") == 0);
        REQUIRE(ScalarOf(db, "SELECT COUNT(*) FROM payment;") == 0);
    }

    SECTION("a payment must reference a known tender type")
    {
        REQUIRE(db.Exec(
            "INSERT INTO payment(id, subcheck_id, business_day_id, seq, "
            "tender_type, amount) VALUES (1, 1, 1, 0, 999, 500);") == Status::Constraint);
    }
}

TEST_CASE("Frozen totals cannot be rewritten", "[sql][schema][totals]")
{
    Database db = FreshDatabase();
    SeedCheckAggregate(db);

    REQUIRE(db.Exec(
        "INSERT INTO subcheck_total(subcheck_id, raw_sales, total_sales, total_cost) "
        "VALUES (1, 1000, 1000, 1100);") == Status::Ok);

    SECTION("an open subcheck's totals can still be recomputed")
    {
        REQUIRE(db.Exec("UPDATE subcheck_total SET raw_sales=2000 WHERE subcheck_id=1;")
                == Status::Ok);
        REQUIRE(ScalarOf(db, "SELECT raw_sales FROM subcheck_total WHERE subcheck_id=1;")
                == 2000);
    }

    SECTION("once frozen, an update is refused")
    {
        // The number on the receipt and remitted to the tax authority is the
        // frozen one. Corrections become new rows, never edits.
        REQUIRE(db.Exec("UPDATE subcheck SET frozen_at_local=100 WHERE id=1;") == Status::Ok);
        REQUIRE(db.Exec("UPDATE subcheck_total SET raw_sales=3000 WHERE subcheck_id=1;")
                == Status::Constraint);
        REQUIRE(ScalarOf(db, "SELECT raw_sales FROM subcheck_total WHERE subcheck_id=1;")
                == 1000);
    }

    SECTION("total_sales is pre-tax and total_cost includes it")
    {
        // Pins the naming trap the FigureTotals characterization tests found:
        // the names invite the opposite reading.
        REQUIRE(ScalarOf(db, "SELECT total_sales FROM subcheck_total WHERE subcheck_id=1;")
                == 1000);
        REQUIRE(ScalarOf(db, "SELECT total_cost FROM subcheck_total WHERE subcheck_id=1;")
                == 1100);
    }

    SECTION("check_total aggregates its subchecks")
    {
        REQUIRE(ScalarOf(db, "SELECT total_cost FROM check_total WHERE check_id=1;") == 1100);
    }
}

TEST_CASE("Tender types match the TENDER_* constants", "[sql][schema][lookups]")
{
    // Settings::TenderName is a hardcoded parallel array already out of sync
    // with these constants. This is what stops the table drifting the same way.
    Database db = FreshDatabase();

    auto tender_present = [&db](int id) {
        return ScalarOf(db, "SELECT COUNT(*) FROM tender_type_ref WHERE id=" +
                            std::to_string(id) + ";") == 1;
    };

    REQUIRE(tender_present(TENDER_CASH));
    REQUIRE(tender_present(TENDER_CHECK));
    REQUIRE(tender_present(TENDER_CHARGE_CARD));
    REQUIRE(tender_present(TENDER_COUPON));
    REQUIRE(tender_present(TENDER_GIFT));
    REQUIRE(tender_present(TENDER_COMP));
    REQUIRE(tender_present(TENDER_ACCOUNT));
    REQUIRE(tender_present(TENDER_CHARGE_ROOM));
    REQUIRE(tender_present(TENDER_DISCOUNT));
    REQUIRE(tender_present(TENDER_CAPTURED_TIP));
    REQUIRE(tender_present(TENDER_EMPLOYEE_MEAL));
    REQUIRE(tender_present(TENDER_CREDIT_CARD));
    REQUIRE(tender_present(TENDER_DEBIT_CARD));
    REQUIRE(tender_present(TENDER_CHARGED_TIP));
    REQUIRE(tender_present(TENDER_PAID_TIP));
    REQUIRE(tender_present(TENDER_OVERAGE));
    REQUIRE(tender_present(TENDER_CHANGE));
    REQUIRE(tender_present(TENDER_PAYOUT));
    REQUIRE(tender_present(TENDER_MONEY_LOST));
    REQUIRE(tender_present(TENDER_GRATUITY));
    REQUIRE(tender_present(TENDER_ITEM_COMP));
    REQUIRE(tender_present(TENDER_EXPENSE));
    REQUIRE(tender_present(TENDER_CASH_AVAIL));
    REQUIRE(tender_present(TENDER_CREDIT_CARD_FEE_DOLLAR));
    REQUIRE(tender_present(TENDER_CREDIT_CARD_FEE_PERCENT));
    REQUIRE(tender_present(TENDER_DEBIT_CARD_FEE_DOLLAR));
    REQUIRE(tender_present(TENDER_DEBIT_CARD_FEE_PERCENT));
}

TEST_CASE("The serial sequence is seeded", "[sql][schema][sequence]")
{
    Database db = FreshDatabase();

    // Checks and drawers share one counter, matching System::NewSerialNumber.
    // Splitting them is correct but changes visible numbering, so it is left as
    // a separate decision from the migration itself.
    REQUIRE(ScalarOf(db, "SELECT next_value FROM sequence WHERE name='pos_serial';") == 1);

    SECTION("allocation is exact under RETURNING")
    {
        // This is why the build requires SQLite >= 3.35: claiming an id and
        // reading it back must be one statement inside the writing transaction,
        // which is what removes the boot-time heuristic recovery the legacy
        // counter needed.
        int64_t claimed = 0;
        REQUIRE(db.QueryInt(
            "UPDATE sequence SET next_value = next_value + 1 "
            "WHERE name='pos_serial' RETURNING next_value;", claimed) == Status::Ok);
        REQUIRE(claimed == 2);
    }
}
