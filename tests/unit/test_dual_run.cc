/*
 * Dual-run verification: write through both backends, compare what each
 * persisted, and pin exactly where they differ.
 *
 * The headline result is deliberately NOT "no divergence". The two backends
 * genuinely disagree in known places, and a test asserting they agree would
 * only be possible by comparing memory to memory -- which would prove nothing,
 * because the in-memory Check still holds fields the legacy format never
 * writes. Snapshots are read from the persisted form on both sides, so the
 * divergences below are the real, measured cost of the legacy format, and each
 * one is asserted by name.
 */

#include <catch2/catch_all.hpp>
#include "main/data/store/dual_store.hh"

#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/system.hh"
#include "support/vt_test_env.hh"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

// Redirects where the legacy backend writes, and cleans up after itself. The
// MasterSystem global is shared across the whole run, so current_path has to be
// restored or the next test inherits a directory that no longer exists.
struct DualFixture : vt_test::VtSystemFixture
{
    fs::path dir;
    std::string db;
    std::string previous_path;

    explicit DualFixture(const std::string &name)
        : dir(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(dir, ec);

        // MasterSystem is shared across the whole run and a failing REQUIRE
        // aborts the rest of a section, so a test cannot rely on its own
        // trailing cleanup. Purging at both ends keeps one failure from
        // cascading into every case that runs afterwards.
        PurgeChecks();

        System *system = MasterSystem.get();
        previous_path = (system->current_path.Value() != nullptr)
                            ? system->current_path.Value() : "";
        system->current_path.Set(dir.string().c_str());
    }

    ~DualFixture()
    {
        PurgeChecks();
        MasterSystem->current_path.Set(previous_path.c_str());
        Clean();
    }

    static void PurgeChecks()
    {
        System *system = MasterSystem.get();
        while (Check *check = system->CheckList())
        {
            if (system->Remove(check) != 0)
                break;      // not in the list; nothing more to unlink
            delete check;
        }
    }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }

    std::unique_ptr<DualRunStore> OpenDual()
    {
        // Legacy is the primary. That is the whole posture of a dual run: the
        // proven backend stays authoritative and the new one shadows it until
        // the evidence says otherwise.
        auto legacy = MakeLegacyFileStore(MasterSystem.get());
        StoreError error = StoreError::Io;
        auto sqlite = MakeSqliteStore(db, error);
        REQUIRE(error == StoreError::None);

        auto dual = MakeDualRunStore(std::move(legacy), std::move(sqlite));
        REQUIRE(dual != nullptr);
        return dual;
    }
};

// A check with two seats, a modifier tree carrying explicit call_order values,
// and a payment -- enough that the known divergences have somewhere to appear.
Check *BuildCheck(int serial, const char *label)
{
    auto *check = new Check;
    check->serial_number = serial;
    check->type = CHECK_TAKEOUT;
    check->guests = 2;
    check->label.Set(label);

    SubCheck *sub = check->NewSubCheck();

    auto *burger = new Order("Burger", 950);
    burger->item_family = FAMILY_BURGERS;
    burger->seat = 1;
    sub->Add(burger);

    auto *cheese = new Order("Add Cheese", 100);
    cheese->item_type = ITEM_MODIFIER;
    cheese->item_family = FAMILY_MODIFIER;
    cheese->call_order = 3;     // never written by Order::Write
    burger->Add(cheese);

    auto *soup = new Order("Soup", 450);
    soup->item_family = FAMILY_SOUP;
    soup->seat = 2;
    sub->Add(soup);

    auto *payment = new Payment(TENDER_CASH, 0, 0, 1500);
    sub->Add(payment);

    return check;
}

bool HasPathEnding(const std::vector<Divergence> &found, const std::string &suffix)
{
    return std::any_of(found.begin(), found.end(),
                       [&suffix](const Divergence &d) {
                           return d.path.size() >= suffix.size() &&
                                  d.path.compare(d.path.size() - suffix.size(),
                                                 suffix.size(), suffix) == 0;
                       });
}

} // namespace

TEST_CASE("A dual run writes through both backends", "[dualrun]")
{
    DualFixture fixture("vt_dual_write");
    auto dual = fixture.OpenDual();

    SECTION("it names both sides so a report can attribute a difference")
    {
        REQUIRE(std::string(dual->Name()) == "dual(legacy-file + sqlite)");
    }

    SECTION("it reports the primary's guarantees, not the better of the two")
    {
        // A dual run does not gain atomicity just by having a backend that
        // offers it. Callers must keep behaving as if the weaker applies until
        // cutover, or the first crash after cutover-minus-one-day is a surprise.
        REQUIRE_FALSE(dual->SupportsAtomicWrites());
    }

    SECTION("health requires both sides")
    {
        REQUIRE(dual->HealthCheck() == StoreError::None);
    }

    SECTION("one save reaches a file and a row")
    {
        Check *check = BuildCheck(501, "table 7");
        REQUIRE(MasterSystem->Add(check) == 0);

        auto tx = dual->Begin();
        REQUIRE(dual->Checks().Save(*tx, *check) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);

        REQUIRE(dual->Shadow().saves == 1);
        REQUIRE(dual->Shadow().save_failures == 0);

        StoreSnapshot legacy;
        StoreSnapshot sqlite;
        REQUIRE(dual->Primary().Snapshot(legacy) == StoreError::None);
        REQUIRE(dual->ShadowStore().Snapshot(sqlite) == StoreError::None);
        REQUIRE(legacy.checks.size() == 1);
        REQUIRE(sqlite.checks.size() == 1);
        REQUIRE(legacy.checks[0].serial_number == 501);
        REQUIRE(sqlite.checks[0].serial_number == 501);

        auto remove = dual->Begin();
        REQUIRE(dual->Checks().Remove(*remove, *check) == StoreError::None);
        REQUIRE(remove->Commit() == StoreError::None);
    }
}

TEST_CASE("The dual run measures where the two backends actually differ",
          "[dualrun][divergence]")
{
    DualFixture fixture("vt_dual_diverge");
    auto dual = fixture.OpenDual();

    Check *check = BuildCheck(601, "table 7");
    REQUIRE(MasterSystem->Add(check) == 0);

    auto tx = dual->Begin();
    REQUIRE(dual->Checks().Save(*tx, *check) == StoreError::None);
    REQUIRE(tx->Commit() == StoreError::None);

    std::vector<Divergence> found;
    REQUIRE(dual->Compare(found) == StoreError::None);

    SECTION("call_order diverges, because the legacy format never wrote it")
    {
        // The finding the whole exercise exists to produce. Order::Write never
        // emitted call_order, so the legacy side reads back a constructor
        // default while SQL stores what was in memory. Order::Add sorts
        // modifiers by call_order, so this is kitchen ticket order silently
        // changing across a save -- now measured rather than argued about.
        //
        // Every order in this check diverges, not just the one that set the
        // field explicitly, because the two Order constructors disagree about
        // the default: Order() sets 1 (check.cc:5612) and
        // Order(name, price) sets 4 (check.cc:5708). Order::Read builds with
        // the first, and the application builds real orders with the second.
        // So the value changes on every order that has ever been saved and
        // reloaded, whether or not anyone touched it.
        const auto at = [&found](const std::string &suffix) {
            return std::find_if(found.begin(), found.end(),
                                [&suffix](const Divergence &d) {
                                    return d.path.size() >= suffix.size() &&
                                           d.path.compare(
                                               d.path.size() - suffix.size(),
                                               suffix.size(), suffix) == 0;
                                });
        };

        // order[0] is the Burger, built by Order(name, price) and never
        // assigned a call_order by this test.
        const auto burger = at("subcheck[0].order[0].call_order");
        REQUIRE(burger != found.end());
        REQUIRE(burger->left == "1");    // legacy: Order() default after Read
        REQUIRE(burger->right == "4");   // sqlite: Order(name, price) default
        REQUIRE_FALSE(burger->note.empty());

        // order[1] is the modifier, which this test set to 3 explicitly.
        const auto modifier = at("subcheck[0].order[1].call_order");
        REQUIRE(modifier != found.end());
        REQUIRE(modifier->left == "1");
        REQUIRE(modifier->right == "3");
    }

    SECTION("the divergence report names both backends and the reason")
    {
        std::string report;
        REQUIRE(dual->CompareAndDescribe(report) == StoreError::None);
        REQUIRE_FALSE(report.empty());
        REQUIRE(report.find("legacy-file") != std::string::npos);
        REQUIRE(report.find("sqlite") != std::string::npos);
        REQUIRE(report.find("call_order") != std::string::npos);
    }

    SECTION("everything else about the order tree agrees")
    {
        // The measured divergence must be the known one and nothing else. A
        // report that also disagreed about item names or the tree shape would
        // mean the SQL backend was losing data, not that the legacy one was.
        REQUIRE_FALSE(HasPathEnding(found, ".item_name"));
        REQUIRE_FALSE(HasPathEnding(found, ".item_cost"));
        REQUIRE_FALSE(HasPathEnding(found, ".item_family"));
        REQUIRE_FALSE(HasPathEnding(found, ".seat"));
        REQUIRE_FALSE(HasPathEnding(found, ".parent_index"));
        REQUIRE_FALSE(HasPathEnding(found, ".order_count"));
        REQUIRE_FALSE(HasPathEnding(found, ".payment_count"));
        REQUIRE_FALSE(HasPathEnding(found, ".subcheck_count"));
    }

    auto remove = dual->Begin();
    REQUIRE(dual->Checks().Remove(*remove, *check) == StoreError::None);
    REQUIRE(remove->Commit() == StoreError::None);
}

TEST_CASE("String escaping loss shows up in the diff", "[dualrun][divergence]")
{
    DualFixture fixture("vt_dual_strings");
    auto dual = fixture.OpenDual();

    // The legacy writer maps ' ' and '~' onto '_' and the reader maps '_' back
    // to ' ', so a label containing an underscore does not survive a round trip
    // -- existing production data is already damaged this way. The SQL backend
    // stores the bytes, so the diff surfaces it.
    Check *check = BuildCheck(701, "bar_side");
    REQUIRE(MasterSystem->Add(check) == 0);

    auto tx = dual->Begin();
    REQUIRE(dual->Checks().Save(*tx, *check) == StoreError::None);
    REQUIRE(tx->Commit() == StoreError::None);

    std::vector<Divergence> found;
    REQUIRE(dual->Compare(found) == StoreError::None);

    const auto it = std::find_if(found.begin(), found.end(),
                                 [](const Divergence &d) {
                                     return d.path.find(".label") != std::string::npos;
                                 });
    REQUIRE(it != found.end());
    REQUIRE(it->left == "bar side");    // legacy: '_' read back as ' '
    REQUIRE(it->right == "bar_side");   // sqlite: the bytes as given

    auto remove = dual->Begin();
    REQUIRE(dual->Checks().Remove(*remove, *check) == StoreError::None);
    REQUIRE(remove->Commit() == StoreError::None);
}

TEST_CASE("A full business cycle stays consistent across both backends",
          "[dualrun][cycle]")
{
    // Open, order, add a modifier, tender, close, void one check -- the shape
    // of a service, driven through the seam rather than through the UI.
    DualFixture fixture("vt_dual_cycle");
    auto dual = fixture.OpenDual();

    std::vector<Check *> live;
    for (int i = 0; i < 3; ++i)
    {
        Check *check = BuildCheck(800 + i, "table");
        REQUIRE(MasterSystem->Add(check) == 0);
        live.push_back(check);

        auto tx = dual->Begin();
        REQUIRE(dual->Checks().Save(*tx, *check) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);
    }

    SECTION("both backends hold the same set of checks")
    {
        StoreSnapshot legacy;
        StoreSnapshot sqlite;
        REQUIRE(dual->Primary().Snapshot(legacy) == StoreError::None);
        REQUIRE(dual->ShadowStore().Snapshot(sqlite) == StoreError::None);

        REQUIRE(legacy.checks.size() == 3);
        REQUIRE(sqlite.checks.size() == 3);
        for (std::size_t i = 0; i < legacy.checks.size(); ++i)
        {
            REQUIRE(legacy.checks[i].serial_number ==
                    sqlite.checks[i].serial_number);
        }
    }

    SECTION("settling a check keeps both sides in step")
    {
        // Re-saving after a change is where a backend that got insert-vs-update
        // wrong duplicates rows. Neither may.
        live[0]->SubList()->status = CHECK_CLOSED;
        live[0]->SubList()->settle_time.Set();

        auto tx = dual->Begin();
        REQUIRE(dual->Checks().Save(*tx, *live[0]) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);

        StoreSnapshot sqlite;
        REQUIRE(dual->ShadowStore().Snapshot(sqlite) == StoreError::None);
        REQUIRE(sqlite.checks.size() == 3);
        REQUIRE(sqlite.checks[0].subchecks.size() == 1);
        REQUIRE(sqlite.checks[0].subchecks[0].status == CHECK_CLOSED);

        std::vector<Divergence> found;
        REQUIRE(dual->Compare(found) == StoreError::None);
        REQUIRE_FALSE(HasPathEnding(found, ".status"));
        REQUIRE_FALSE(HasPathEnding(found, ".subcheck_count"));
    }

    SECTION("voiding a check removes it from both")
    {
        Check *doomed = live.back();
        live.pop_back();

        auto tx = dual->Begin();
        REQUIRE(dual->Checks().Remove(*tx, *doomed) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);

        StoreSnapshot legacy;
        StoreSnapshot sqlite;
        REQUIRE(dual->Primary().Snapshot(legacy) == StoreError::None);
        REQUIRE(dual->ShadowStore().Snapshot(sqlite) == StoreError::None);
        REQUIRE(legacy.checks.size() == 2);
        REQUIRE(sqlite.checks.size() == 2);

        std::vector<Divergence> found;
        REQUIRE(dual->Compare(found) == StoreError::None);
        // No "present on one side only" entries: a Remove that reached one
        // backend and not the other is the single most dangerous shadow bug,
        // because it looks like success at the call site.
        REQUIRE_FALSE(HasPathEnding(found, "]"));

        REQUIRE(dual->Shadow().removes == 1);
        REQUIRE(dual->Shadow().remove_failures == 0);
    }

    for (Check *check : live)
    {
        auto tx = dual->Begin();
        REQUIRE(dual->Checks().Remove(*tx, *check) == StoreError::None);
        REQUIRE(tx->Commit() == StoreError::None);
    }
}

TEST_CASE("A shadow failure never fails a save", "[dualrun][safety]")
{
    // The rule the whole arrangement rests on: a bug in the unproven backend
    // must not be able to stop a restaurant taking money.
    DualFixture fixture("vt_dual_shadow_fail");

    auto legacy = MakeLegacyFileStore(MasterSystem.get());
    // A shadow with no System of its own fails every operation with Io.
    auto broken = MakeLegacyFileStore(nullptr);
    auto dual = MakeDualRunStore(std::move(legacy), std::move(broken));
    REQUIRE(dual != nullptr);

    Check *check = BuildCheck(901, "table 9");
    REQUIRE(MasterSystem->Add(check) == 0);

    auto tx = dual->Begin();
    REQUIRE(dual->Checks().Save(*tx, *check) == StoreError::None);
    REQUIRE(tx->Commit() == StoreError::None);

    // The save succeeded from the caller's point of view, and the failure is
    // recorded rather than swallowed.
    REQUIRE(dual->Shadow().saves == 1);
    REQUIRE(dual->Shadow().save_failures == 1);
    REQUIRE(dual->Shadow().last_error == std::string(StoreErrorName(StoreError::Io)));

    // HealthCheck is where a broken shadow is allowed to be loud: unlike a
    // save, there is nothing to lose by surfacing it.
    REQUIRE(dual->HealthCheck() == StoreError::Io);

    auto remove = dual->Begin();
    REQUIRE(dual->Checks().Remove(*remove, *check) == StoreError::None);
    REQUIRE(remove->Commit() == StoreError::None);
}
