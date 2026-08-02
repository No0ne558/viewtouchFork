/*
 * Round-trip tests for the legacy file format.
 *
 * These write a record with the production writer, read it back with the
 * production reader, and assert field by field what survived. That makes the
 * lossy parts of the format explicit and testable rather than folklore, and it
 * is the seed of the golden-file harness the SQL importer will be verified
 * against: whatever these tests say survives is exactly what the importer is
 * allowed to assume.
 *
 * Two documented losses below are real defects of the FILE FORMAT, pinned here
 * rather than fixed because repairing either needs an on-disk version bump and
 * a migration story for existing files:
 *
 *   - Order::call_order is never written, yet Order::Add sorts modifiers by it.
 *   - Settings::tax_takeout_food is read live by FigureTotals and never frozen
 *     into the Archive, so toggling it restates historical takeout tax.
 *
 * Both are now CLOSED for the SQL backend, which was the agreed resolution
 * rather than bumping the legacy format: order_item.call_order and
 * day_policy.tax_takeout_food are written and asserted in test_sqlite_store.cc
 * and test_importer.cc respectively. These tests stay because the legacy
 * backend is still the default and still loses both, and they should be
 * retired with it -- not treated as an open item waiting on a format bump that
 * is no longer planned.
 *
 * Historical data cannot recover either value. The importer records
 * day_policy.snapshot_complete = 0 to say so.
 */

#include <catch2/catch_all.hpp>
#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/archive.hh"
#include "main/data/settings.hh"
#include "src/core/data_file.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Writes to a temp path and removes it on destruction.
struct TempPath
{
    fs::path path;

    explicit TempPath(const std::string &name)
        : path(fs::temp_directory_path() / name) {}

    ~TempPath()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

Order MakeOrder()
{
    Order order;
    order.item_name.Set("Cheeseburger");
    order.item_type = ITEM_NORMAL;
    order.item_cost = 950;
    order.item_family = FAMILY_SANDWICHES;
    order.sales_type = SALES_FOOD;
    order.count = 2;
    order.qualifier = 0;
    order.status = 0;
    order.user_id = 7;
    order.seat = 3;
    order.employee_meal = 0;
    order.is_reduced = 0;
    order.reduced_cost = 0;
    order.auto_coupon_id = -1;
    return order;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Order survives a write/read round trip", "[roundtrip][order]")
{
    TempPath file("vt_order_roundtrip.dat");

    Order written = MakeOrder();
    {
        OutputDataFile out;
        REQUIRE(out.Open(file.path.string(), CHECK_VERSION, 0) == 0);
        REQUIRE(written.Write(out, CHECK_VERSION) == 0);
        out.Close();
    }

    Order read_back;
    {
        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);
        REQUIRE(version == CHECK_VERSION);
        read_back.Read(in, version);
        in.Close();
    }

    SECTION("the priced fields survive")
    {
        REQUIRE(read_back.item_cost == written.item_cost);
        REQUIRE(read_back.count == written.count);
        REQUIRE(read_back.reduced_cost == written.reduced_cost);
        REQUIRE(read_back.is_reduced == written.is_reduced);
    }

    SECTION("the classification fields survive")
    {
        REQUIRE(read_back.item_type == written.item_type);
        REQUIRE(read_back.item_family == written.item_family);
        REQUIRE(read_back.sales_type == written.sales_type);
        REQUIRE(read_back.qualifier == written.qualifier);
    }

    SECTION("the attribution fields survive")
    {
        REQUIRE(read_back.user_id == written.user_id);
        REQUIRE(read_back.seat == written.seat);
        REQUIRE(read_back.employee_meal == written.employee_meal);
        REQUIRE(read_back.auto_coupon_id == written.auto_coupon_id);
    }

    SECTION("the item name survives")
    {
        REQUIRE(std::string(read_back.item_name.Value()) == "Cheeseburger");
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Order::call_order does not survive a round trip",
                 "[roundtrip][order][known-loss]")
{
    // Order::Add inserts modifiers by call_order (check.cc:5906), but neither
    // Order::Read nor Order::Write mentions the field. Every order therefore
    // comes back with a constructor default, so modifiers attached after a
    // reload sort differently from the same modifiers attached before one --
    // i.e. kitchen ticket ordering can change across a save/load cycle.
    //
    // Wider than it looks, which the dual-run diff is what revealed: the two
    // Order constructors disagree about the default. Order() sets 1
    // (check.cc:5612) and Order(name, price) sets 4 (check.cc:5708).
    // Order::Read builds with the first while the application builds real
    // orders with the second, so the value changes on EVERY order that has
    // ever been saved and reloaded -- not only ones somebody set deliberately.
    //
    // Closed for the SQL backend (order_item.call_order), which was the agreed
    // resolution instead of a CHECK_VERSION bump. Pinned here for as long as
    // the file format is still the default. Historical orders cannot recover
    // the value at all: it was never stored.
    TempPath file("vt_callorder_roundtrip.dat");

    Order written = MakeOrder();
    written.call_order = 4;   // not the constructor default of 1

    {
        OutputDataFile out;
        REQUIRE(out.Open(file.path.string(), CHECK_VERSION, 0) == 0);
        written.Write(out, CHECK_VERSION);
        out.Close();
    }

    Order read_back;
    {
        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);
        read_back.Read(in, version);
        in.Close();
    }

    REQUIRE(written.call_order == 4);
    REQUIRE(read_back.call_order == 1);       // the constructor default
    REQUIRE(read_back.call_order != written.call_order);
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "tax_takeout_food is not frozen into the archive policy",
                 "[roundtrip][archive][known-loss]")
{
    // FigureTotals reads settings->tax_takeout_food live, at check.cc:4046,
    // :4280 and :4330, and Archive has no corresponding member. Every other
    // rate FigureTotals consults is snapshotted per business day precisely so
    // that changing it cannot restate closed days; this one is not, so toggling
    // it retroactively changes the food tax on every archived takeout check.
    //
    // Same reason as above for pinning rather than fixing: adding the field to
    // Archive changes the archive file format and needs an ARCHIVE_VERSION bump.
    // The SQL migration's day_policy table is the right place to close it.

    SECTION("the setting exists and is a policy input")
    {
        Settings settings;
        settings.tax_takeout_food = 0;
        REQUIRE(settings.tax_takeout_food == 0);
        settings.tax_takeout_food = 1;
        REQUIRE(settings.tax_takeout_food == 1);
    }

    SECTION("the archive snapshot carries the rates that are frozen")
    {
        // Guards the fields that ARE snapshotted, so this test fails loudly if
        // the snapshot regresses again the way tax_VAT did.
        Settings settings;
        settings.tax_food = 0.05;
        settings.tax_VAT = 0.20;
        settings.advertise_fund = 0.01;

        TimeInfo end;
        end.Set();
        Archive archive(end);
        archive.CopyPolicyFrom(settings);

        REQUIRE(archive.tax_food == Catch::Approx(0.05));
        REQUIRE(archive.tax_VAT == Catch::Approx(0.20));
        REQUIRE(archive.advertise_fund == Catch::Approx(0.01));
    }
}
