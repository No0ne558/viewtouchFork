/*
 * Golden-file corpus: reading historical file versions with today's readers.
 *
 * The production writers can only emit the current version constant, so the
 * version ladders inside Read() -- the `if (version >= N)` cascades that have
 * accumulated since 1997 -- have never been executed by anything except real
 * customer data. This file constructs files at each historical version directly
 * and reads them back with the production readers.
 *
 * That matters for the SQL migration specifically. The importer must reuse
 * these same readers rather than reimplement the ladder, because they are the
 * only artifact that knows what a decade of files actually mean. This corpus is
 * how that assumption gets checked: whatever these tests say a version yields is
 * what the importer is entitled to assume.
 *
 * The builder is verified against the production writer first. A builder that
 * silently disagreed with OutputDataFile would make every test below vacuous.
 */

#include <catch2/catch_all.hpp>
#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/settings.hh"
#include "src/core/data_file.hh"
#include "support/legacy_file_builder.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using vt_test::LegacyFileBuilder;

namespace {

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

// Writes the ten fields every Order version starts with, then whichever of the
// four later fields the requested version includes.
void BuildOrder(LegacyFileBuilder &b, int version,
                const std::string &name, int cost, int family,
                int employee_meal = 0, int is_reduced = 0,
                int reduced_cost = 0, int auto_coupon_id = -1)
{
    b.Str(name);
    b.Int(ITEM_NORMAL);
    b.Int(cost);
    b.Int(family);
    b.Int(SALES_FOOD);
    b.Int(1);              // count
    b.Int(0);              // qualifier
    b.Int(0);              // status
    b.Int(0);              // user_id
    b.Int(0);              // seat
    if (version >= 19) b.Int(employee_meal);
    if (version >= 20) b.Int(is_reduced);
    if (version >= 21) b.Int(reduced_cost);
    if (version >= 22) b.Int(auto_coupon_id, true);
}

Order ReadOrderFrom(const fs::path &path)
{
    int version = 0;
    InputDataFile in;
    REQUIRE(in.Open(path.string(), version) == 0);
    Order order;
    order.Read(in, version);
    in.Close();
    return order;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The corpus builder matches the production writer",
                 "[corpus][builder]")
{
    // Everything else in this file depends on the builder encoding exactly what
    // OutputDataFile would, so that is established before it is relied on.
    SECTION("integers, strings and times encode identically")
    {
        TempPath produced("vt_corpus_writer.dat");

        {
            OutputDataFile out;
            REQUIRE(out.Open(produced.path.string(), CHECK_VERSION, 0) == 0);
            out.Write(0);
            out.Write(1);
            out.Write(63);
            out.Write(64);
            out.Write(-1);
            out.Write(1000000);
            out.Write("Cheeseburger");
            out.Write("");
            out.Close();
        }

        LegacyFileBuilder builder;
        builder.Int(0).Int(1).Int(63).Int(64).Int(-1).Int(1000000)
               .Str("Cheeseburger").Str("");

        std::ifstream in(produced.path, std::ios::binary);
        const std::string from_writer((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());

        REQUIRE(builder.Build(CHECK_VERSION) == from_writer);
    }

    SECTION("a built file is readable by the production reader")
    {
        TempPath file("vt_corpus_readback.dat");
        LegacyFileBuilder builder;
        builder.Int(42).Str("Hello").Int(-7);
        builder.WriteTo(file.path.string(), CHECK_VERSION);

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);
        REQUIRE(version == CHECK_VERSION);

        int a = 0;
        Str s;
        int b = 0;
        in.Read(a);
        in.Read(s);
        in.Read(b);

        REQUIRE(a == 42);
        REQUIRE(std::string(s.Value()) == "Hello");
        REQUIRE(b == -7);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Order reads correctly at every historical version",
                 "[corpus][order][versions]")
{
    // Order::Read gates four fields: employee_meal at 19, is_reduced at 20,
    // reduced_cost at 21, auto_coupon_id at 22. Each boundary is exercised on
    // both sides.

    SECTION("version 18 predates all four optional fields")
    {
        TempPath file("vt_order_v18.dat");
        LegacyFileBuilder b;
        BuildOrder(b, 18, "Burger", 950, FAMILY_SANDWICHES);
        b.WriteTo(file.path.string(), 18);

        const Order order = ReadOrderFrom(file.path);

        REQUIRE(std::string(order.item_name.Value()) == "Burger");
        REQUIRE(order.item_cost == 950);
        REQUIRE(order.item_family == FAMILY_SANDWICHES);
        // Fields the file does not carry keep their constructor defaults.
        REQUIRE(order.employee_meal == 0);
        REQUIRE(order.is_reduced == 0);
        REQUIRE(order.reduced_cost == 0);
        REQUIRE(order.auto_coupon_id == -1);
    }

    SECTION("version 19 introduces employee_meal")
    {
        TempPath file("vt_order_v19.dat");
        LegacyFileBuilder b;
        BuildOrder(b, 19, "Staff Meal", 0, FAMILY_LUNCH_ENTREES, /*employee_meal=*/1);
        b.WriteTo(file.path.string(), 19);

        const Order order = ReadOrderFrom(file.path);

        REQUIRE(order.employee_meal == 1);
        REQUIRE(order.is_reduced == 0);
    }

    SECTION("version 21 introduces reduced_cost")
    {
        TempPath file("vt_order_v21.dat");
        LegacyFileBuilder b;
        BuildOrder(b, 21, "Happy Hour", 800, FAMILY_BEER,
                   /*employee_meal=*/0, /*is_reduced=*/1, /*reduced_cost=*/500);
        b.WriteTo(file.path.string(), 21);

        const Order order = ReadOrderFrom(file.path);

        REQUIRE(order.is_reduced == 1);
        REQUIRE(order.reduced_cost == 500);
        REQUIRE(order.auto_coupon_id == -1);   // still absent at 21
    }

    SECTION("version 22 introduces auto_coupon_id")
    {
        TempPath file("vt_order_v22.dat");
        LegacyFileBuilder b;
        BuildOrder(b, 22, "Combo", 1200, FAMILY_SANDWICHES,
                   0, 0, 0, /*auto_coupon_id=*/17);
        b.WriteTo(file.path.string(), 22);

        const Order order = ReadOrderFrom(file.path);

        REQUIRE(order.auto_coupon_id == 17);
    }

    SECTION("the current version carries every field")
    {
        TempPath file("vt_order_current.dat");
        LegacyFileBuilder b;
        BuildOrder(b, CHECK_VERSION, "Full", 1500, FAMILY_PIZZA, 1, 1, 900, 3);
        b.WriteTo(file.path.string(), CHECK_VERSION);

        const Order order = ReadOrderFrom(file.path);

        REQUIRE(order.item_cost == 1500);
        REQUIRE(order.employee_meal == 1);
        REQUIRE(order.is_reduced == 1);
        REQUIRE(order.reduced_cost == 900);
        REQUIRE(order.auto_coupon_id == 3);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Order maps the legacy 999 family sentinel",
                 "[corpus][order][family]")
{
    // Old files store 999 for "no family". Order::Read rewrites it to
    // FAMILY_UNKNOWN (255) on the way in. That path is how a 255 reaches
    // Settings::FamilyGroup, whose array is only MAX_FAMILIES (64) wide -- the
    // out-of-bounds read that had to be fixed in FigureTotals. Any importer has
    // to reproduce the same mapping, so it is pinned here.
    TempPath file("vt_order_fam999.dat");
    LegacyFileBuilder b;
    BuildOrder(b, CHECK_VERSION, "Mystery", 500, 999);
    b.WriteTo(file.path.string(), CHECK_VERSION);

    const Order order = ReadOrderFrom(file.path);

    REQUIRE(order.item_family == FAMILY_UNKNOWN);
    REQUIRE(order.item_family != 999);
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "String escaping is lossy in the legacy format",
                 "[corpus][strings][known-loss]")
{
    // The writer maps both ' ' and '~' to '_', and the reader maps every '_'
    // back to ' '. Underscores and tildes in stored names are therefore already
    // corrupted in existing data. The importer must carry across what the reader
    // returns rather than attempt reconstruction -- an intentional underscore is
    // indistinguishable from a mangled space.
    SECTION("a space survives as a space")
    {
        TempPath file("vt_str_space.dat");
        LegacyFileBuilder b;
        BuildOrder(b, CHECK_VERSION, "Grilled Cheese", 700, FAMILY_SANDWICHES);
        b.WriteTo(file.path.string(), CHECK_VERSION);

        REQUIRE(std::string(ReadOrderFrom(file.path).item_name.Value())
                == "Grilled Cheese");
    }

    SECTION("an underscore comes back as a space")
    {
        TempPath file("vt_str_underscore.dat");
        LegacyFileBuilder b;
        BuildOrder(b, CHECK_VERSION, "Grilled_Cheese", 700, FAMILY_SANDWICHES);
        b.WriteTo(file.path.string(), CHECK_VERSION);

        REQUIRE(std::string(ReadOrderFrom(file.path).item_name.Value())
                == "Grilled Cheese");
    }

    SECTION("a tilde comes back as a space")
    {
        TempPath file("vt_str_tilde.dat");
        LegacyFileBuilder b;
        BuildOrder(b, CHECK_VERSION, "Soup~Salad", 700, FAMILY_SANDWICHES);
        b.WriteTo(file.path.string(), CHECK_VERSION);

        REQUIRE(std::string(ReadOrderFrom(file.path).item_name.Value())
                == "Soup Salad");
    }

    SECTION("an empty name round-trips as empty")
    {
        TempPath file("vt_str_empty.dat");
        LegacyFileBuilder b;
        BuildOrder(b, CHECK_VERSION, "", 700, FAMILY_SANDWICHES);
        b.WriteTo(file.path.string(), CHECK_VERSION);

        REQUIRE(std::string(ReadOrderFrom(file.path).item_name.Value()).empty());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Unsupported versions are rejected", "[corpus][versions][bounds]")
{
    // Check::Read refuses anything below 7 or above CHECK_VERSION. Confirming
    // both ends matters for the importer: it tells us the reader will not
    // silently misparse a file it cannot understand.
    Settings settings;

    SECTION("a version below the supported floor is refused")
    {
        TempPath file("vt_check_v6.dat");
        LegacyFileBuilder b;
        b.Int(1).Int(0);
        b.WriteTo(file.path.string(), 6);

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);

        Check check;
        REQUIRE(check.Read(&settings, in, version) == 1);
    }

    SECTION("a version above the current constant is refused")
    {
        TempPath file("vt_check_future.dat");
        LegacyFileBuilder b;
        b.Int(1).Int(0);
        b.WriteTo(file.path.string(), CHECK_VERSION + 1);

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) == 0);

        Check check;
        REQUIRE(check.Read(&settings, in, version) == 1);
    }

    SECTION("a file with an unrecognised header is refused")
    {
        TempPath file("vt_bad_header.dat");
        std::ofstream out(file.path, std::ios::binary);
        out << "not a viewtouch file\n";
        out.close();

        int version = 0;
        InputDataFile in;
        REQUIRE(in.Open(file.path.string(), version) != 0);
    }
}

// ---------------------------------------------------------------------------
// Check::ReadFix -- two incompatible layouts sharing version number 10.
// ---------------------------------------------------------------------------

namespace {

// A version-10 restaurant check, up to but not including the kitchen-video
// fields that ReadFix has to disambiguate.
//
// Field order is Check::Read's, for version 10: serial, time_open, user_open,
// user_owner, flags, customer_type, then CustomerInfo::Read's restaurant branch
// (table, guests, reserve_start, reserve_end). flags carries the line break,
// matching Check::Write's `Write(flags, 1)`, so the kitchen-video fields begin
// a fresh line -- which is what makes counting tokens on that line meaningful.
void BuildCheckV10Prefix(LegacyFileBuilder &b, int serial)
{
    b.Int(serial);
    b.Time(12 * 3600, 2002);      // time_open
    b.Int(7);                     // user_open
    b.Int(7);                     // user_owner
    b.Int(0, true);               // flags, then newline
    b.Int(CHECK_RESTAURANT);      // customer_type
    b.Str("table 4");             // CustomerInfo::table
    b.Int(2);                     // CustomerInfo::guests
    b.UnsetTime();                // reserve_start
    b.UnsetTime();                // reserve_end
}

// Check is neither copyable nor movable, so the caller owns it.
int ReadCheckInto(Check &check, const fs::path &path, Settings &settings)
{
    int version = 0;
    InputDataFile in;
    REQUIRE(in.Open(path.string(), version) == 0);
    const int error = check.Read(&settings, in, version);
    in.Close();
    return error;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Check::ReadFix disambiguates the two version-10 layouts",
                 "[corpus][check][versions][readfix]")
{
    // Version 10 shipped twice with different bytes under the same number.
    // The first wrote chef_time and made_time as plain ints; version 11 fixed
    // it by writing them as TimeInfos -- two tokens each -- but files written
    // in between still say 10. ReadFix peeks at how many tokens remain on the
    // line and picks a layout, which is the only reason those archives are
    // readable at all.
    //
    // Nothing else in the corpus covers this, and it is the one place where the
    // format is genuinely ambiguous rather than merely versioned. The importer
    // inherits whatever this does.
    Settings settings;

    SECTION("the later layout reads chef_time and made_time as TimeInfos")
    {
        TempPath file("vt_check_v10_new.dat");
        LegacyFileBuilder b;
        BuildCheckV10Prefix(b, 5001);
        b.Int(3);                     // check_state
        b.Time(9 * 3600, 2002);       // chef_time  (two tokens)
        b.Time(10 * 3600, 2002);      // made_time  (two tokens)
        b.Int(77);                    // checknum
        b.Int(0, true);               // subcheck count, then newline
        b.WriteTo(file.path.string(), 10);

        Check check;
        REQUIRE(ReadCheckInto(check, file.path, settings) == 0);
        REQUIRE(check.serial_number == 5001);
        REQUIRE(check.check_state == 3);
        REQUIRE(check.checknum == 77);
        REQUIRE(check.chef_time.IsSet());
        REQUIRE(check.chef_time.Year() == 2002);
        REQUIRE(check.chef_time.Hour() == 9);
        REQUIRE(check.made_time.Hour() == 10);
    }

    SECTION("the earlier layout reads them as ints, and loses them")
    {
        TempPath file("vt_check_v10_old.dat");
        LegacyFileBuilder b;
        BuildCheckV10Prefix(b, 5002);
        b.Int(3);                     // check_state
        b.Int(1234);                  // chef, as a plain int
        b.Int(5678);                  // made, as a plain int
        b.Int(88);                    // checknum
        b.Int(0, true);               // subcheck count, then newline
        b.WriteTo(file.path.string(), 10);

        Check check;
        REQUIRE(ReadCheckInto(check, file.path, settings) == 0);
        REQUIRE(check.serial_number == 5002);
        REQUIRE(check.check_state == 3);
        REQUIRE(check.checknum == 88);

        // The stream is realigned, which is ReadFix's whole job. But the two
        // integers it consumed are discarded: ReadFix calls TimeInfo::Set()
        // with no argument, which is *the current time*, not the value read.
        // So a check from one of these archives reports having been sent to the
        // kitchen at the moment it was imported. That is data loss, it is
        // silent, and it is worth stating because the importer inherits it --
        // an imported chef_time from a pre-11 archive is meaningless.
        REQUIRE(check.chef_time.IsSet());
        REQUIRE(check.made_time.IsSet());
        REQUIRE(check.chef_time.Year() >= 2020);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "PeekTokens leaves the reader as it found it",
                 "[corpus][datafile][peek]")
{
    // PeekTokens is a peek: it saves the file offset and seeks back. But it
    // also sets `end_of_file` as a side effect when the line it is counting
    // runs to the end of the file, and never clears it -- so the position is
    // restored while the flag is not, and every later read on that reader
    // believes the file is exhausted.
    //
    // Found while covering ReadFix, its only caller. Not reachable through
    // Check::Read today, because Check::Write always follows the kitchen-video
    // fields with a newline-terminated subcheck count. The defect is in the
    // peek, not in the caller that currently happens to avoid it.
    TempPath file("vt_peek_eof.dat");
    LegacyFileBuilder b;
    b.Int(11).Int(22).Int(33);      // three tokens, no newline, then EOF
    b.WriteTo(file.path.string(), CHECK_VERSION);

    int version = 0;
    InputDataFile in;
    REQUIRE(in.Open(file.path.string(), version) == 0);

    REQUIRE_FALSE(in.end_of_file);
    (void)in.PeekTokens();
    REQUIRE_FALSE(in.end_of_file);

    // And the values are still there to be read, which is the point of a peek.
    int first = 0;
    int second = 0;
    in.Read(first);
    in.Read(second);
    REQUIRE(first == 11);
    REQUIRE(second == 22);
}
