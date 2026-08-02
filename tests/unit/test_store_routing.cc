/*
 * Check::Save() routed through the Store seam.
 *
 * The claim this file exists to hold to account is narrow and load-bearing: a
 * site running the default configuration writes exactly the same bytes it wrote
 * before, because `legacy` routes back to the same System::SaveCheck the
 * fallback calls. That is asserted on the file contents, not argued from the
 * code -- the routed path is now in front of every save a live till performs,
 * so "should be equivalent" is not good enough.
 *
 * The fallback (no store configured) is not a degenerate case to tidy away.
 * LoadCurrentData saves checks while it reads them, long before startup has
 * read any config, so that window is real and has to keep working.
 */

#include <catch2/catch_all.hpp>

#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/store/dual_store.hh"
#include "main/data/store/store.hh"
#include "main/data/system.hh"
#include "sql/database.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

struct RoutingFixture : vt_test::VtSystemFixture
{
    fs::path dir;
    std::string db;
    std::string previous_path;

    explicit RoutingFixture(const std::string &name)
        : dir(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(dir, ec);
        PurgeChecks();

        System *system = MasterSystem.get();
        previous_path = (system->current_path.Value() != nullptr)
                            ? system->current_path.Value() : "";
        system->current_path.Set(dir.string().c_str());
    }

    ~RoutingFixture()
    {
        // The store has to go before the checks do: a configured store is what
        // DestroyCheck routes through, and PurgeChecks unlinks them directly.
        MasterSystem->SetDataStore(nullptr);
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
                break;
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
};

Check *BuildCheck(int serial)
{
    auto *check = new Check;
    check->serial_number = serial;
    check->type = CHECK_TAKEOUT;
    check->guests = 3;
    check->label.Set("table 12");

    // Pin the serialized times. Check::Check() runs `date.Set()` -- the current
    // wall clock at one-second resolution -- and Check::Write emits it, so any
    // comparison of bytes from two separately-constructed checks is a coin flip
    // on whether construction straddled a second boundary. The equivalence case
    // no longer builds two checks, so this is belt and braces rather than the
    // fix; it stays because the hazard is invisible from the call site.
    check->date.Set(12 * 3600, 2024);
    check->time_open.Set(12 * 3600, 2024);

    SubCheck *sub = check->NewSubCheck();

    auto *burger = new Order("Burger", 950);
    burger->item_family = FAMILY_BURGERS;
    burger->seat = 1;
    sub->Add(burger);

    auto *cheese = new Order("Add Cheese", 100);
    cheese->item_type = ITEM_MODIFIER;
    cheese->item_family = FAMILY_MODIFIER;
    burger->Add(cheese);

    auto *payment = new Payment(TENDER_CASH, 0, 0, 1050);
    sub->Add(payment);

    return check;
}

std::string ReadBytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int FilesIn(const fs::path &dir)
{
    int count = 0;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec))
    {
        (void)entry;
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("The routed path writes the same bytes as the unrouted one",
          "[routing][equivalence]")
{
    // The assertion the default configuration rests on. If these ever differ,
    // every deployed site's data changed shape the day it upgraded.
    //
    // One check object, saved twice, is deliberate. An earlier version built a
    // fresh check for each side, which let construction-time state -- the clock,
    // above all -- into a byte-for-byte comparison and made the test flaky.
    // Saving the same object down both paths compares the two writers and
    // nothing else, which is the whole claim.
    RoutingFixture fixture("vt_routing_equivalence");
    std::unique_ptr<Check> check(BuildCheck(4001));

    REQUIRE(MasterSystem->DataStore() == nullptr);       // fallback path
    REQUIRE(check->Save() == 0);
    REQUIRE(FilesIn(fixture.dir) == 1);
    const std::string unrouted = ReadBytes(fixture.dir / "check_4001");
    REQUIRE_FALSE(unrouted.empty());

    std::error_code ec;
    fs::remove(fixture.dir / "check_4001", ec);
    REQUIRE(FilesIn(fixture.dir) == 0);

    MasterSystem->SetDataStore(MakeLegacyFileStore(MasterSystem.get()));
    REQUIRE(MasterSystem->DataStore() != nullptr);       // routed path
    REQUIRE(check->Save() == 0);
    REQUIRE(FilesIn(fixture.dir) == 1);
    const std::string routed = ReadBytes(fixture.dir / "check_4001");
    REQUIRE_FALSE(routed.empty());

    REQUIRE(routed == unrouted);
}

TEST_CASE("The dispatch survives routing", "[routing]")
{
    // Check::Save()'s three cases have to behave the same whichever path is
    // taken. Getting `copy` wrong writes a working copy over the real check,
    // which is the mistake the seam already caught once.
    RoutingFixture fixture("vt_routing_dispatch");
    MasterSystem->SetDataStore(MakeLegacyFileStore(MasterSystem.get()));

    SECTION("a copy is still not written")
    {
        Check check;
        check.serial_number = 4100;
        check.copy = 1;
        REQUIRE(check.Save() == 0);
        REQUIRE(FilesIn(fixture.dir) == 0);
    }

    SECTION("an ordinary check is still written")
    {
        std::unique_ptr<Check> check(BuildCheck(4101));
        REQUIRE(check->Save() == 0);
        REQUIRE(FilesIn(fixture.dir) == 1);
    }
}

TEST_CASE("A dual-mode site writes both sides from an ordinary Save",
          "[routing][dual]")
{
    // The point of routing at all. Nothing here calls the store directly --
    // this is Check::Save(), the same call every zone and terminal path makes.
    RoutingFixture fixture("vt_routing_dual");

    auto legacy = MakeLegacyFileStore(MasterSystem.get());
    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    MasterSystem->SetDataStore(
        MakeDualRunStore(std::move(legacy), std::move(sqlite)));

    Check *check = BuildCheck(4200);
    REQUIRE(MasterSystem->Add(check) == 0);
    REQUIRE(check->Save() == 0);

    // File written...
    REQUIRE(FilesIn(fixture.dir) == 1);

    // ...and a row too, on a connection this test opens for itself.
    {
        vt::sql::Database db;
        REQUIRE(db.Open(fixture.db) == vt::sql::Status::Ok);
        int64_t rows = 0;
        REQUIRE(db.QueryInt("SELECT COUNT(*) FROM pos_check WHERE serial_number = 4200;",
                            rows) == vt::sql::Status::Ok);
        REQUIRE(rows == 1);
    }

    SECTION("and DestroyCheck removes it from both")
    {
        // A removal reaching one backend and not the other is the most
        // dangerous divergence available: it looks like success at the call
        // site and surfaces as a check returning from the dead after cutover.
        REQUIRE(MasterSystem->DestroyCheck(check) == 0);
        // `check` is destroyed now -- nothing below may touch it.

        REQUIRE(FilesIn(fixture.dir) == 0);

        vt::sql::Database db;
        REQUIRE(db.Open(fixture.db) == vt::sql::Status::Ok);
        int64_t rows = 0;
        REQUIRE(db.QueryInt("SELECT COUNT(*) FROM pos_check;", rows)
                == vt::sql::Status::Ok);
        REQUIRE(rows == 0);
    }
}

TEST_CASE("DestroyCheck and the legacy backend do not call each other forever",
          "[routing][recursion]")
{
    // System::DestroyCheck routes through the store, and the legacy backend
    // implements Remove by destroying the check -- so they had to be split into
    // DestroyCheck and DestroyCheckDirect. If that split is ever undone this
    // test does not fail, it blows the stack, which is its own kind of report.
    RoutingFixture fixture("vt_routing_recursion");
    MasterSystem->SetDataStore(MakeLegacyFileStore(MasterSystem.get()));

    Check *check = BuildCheck(4300);
    REQUIRE(MasterSystem->Add(check) == 0);
    REQUIRE(check->Save() == 0);
    REQUIRE(FilesIn(fixture.dir) == 1);

    REQUIRE(MasterSystem->DestroyCheck(check) == 0);
    REQUIRE(FilesIn(fixture.dir) == 0);
    REQUIRE(MasterSystem->CheckList() == nullptr);
}
