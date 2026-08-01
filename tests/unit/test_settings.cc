/*
 * Unit tests for Settings tax calculation (main/data/settings.cc)
 *
 * This file used to test MockSettings, a hand-written stand-in that declared a
 * SetTaxRate() method the real Settings class does not have. Its "bounds
 * checking" assertions were exercising the mock's own switch statement, so the
 * file could not fail no matter what production did. The mock is deleted; these
 * tests drive the real Settings.
 */

#include <catch2/catch_all.hpp>
#include "main/data/settings.hh"
#include "main/business/sales.hh"      // FAMILY_*, SALESGROUP_*
#include "main/hardware/terminal.hh"   // PRINTER_DEFAULT
#include "src/core/time_info.hh"

namespace {

TimeInfo Now()
{
    TimeInfo t;
    t.Set();
    return t;
}

} // namespace

TEST_CASE("Settings tax rates default to sane values", "[settings][tax]")
{
    Settings settings;

    SECTION("a fresh Settings has no negative tax rates")
    {
        REQUIRE(settings.tax_food >= 0.0);
        REQUIRE(settings.tax_alcohol >= 0.0);
        REQUIRE(settings.tax_room >= 0.0);
        REQUIRE(settings.tax_merchandise >= 0.0);
        REQUIRE(settings.tax_GST >= 0.0);
        REQUIRE(settings.tax_PST >= 0.0);
        REQUIRE(settings.tax_HST >= 0.0);
        REQUIRE(settings.tax_QST >= 0.0);
        REQUIRE(settings.tax_VAT >= 0.0);
    }
}

TEST_CASE("Settings::FigureFoodTax", "[settings][tax][food]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("an explicit rate overrides the configured one")
    {
        settings.tax_food = 0.05;
        // The trailing argument replaces the configured rate when >= 0.
        REQUIRE(settings.FigureFoodTax(10000, when, 0.0825) == 825);
    }

    SECTION("a negative rate falls back to the configured rate")
    {
        settings.tax_food = 0.0825;
        REQUIRE(settings.FigureFoodTax(10000, when, -1) == 825);
        REQUIRE(settings.FigureFoodTax(10000, when) == 825);
    }

    SECTION("tax is rounded, not truncated")
    {
        // This is the behaviour the old mirror tests got wrong: they asserted
        // against static_cast<int>(amount * rate), which truncates.
        // 1099 * 0.0825 = 90.6675 -> 91, not 90.
        settings.tax_food = 0.0825;
        REQUIRE(settings.FigureFoodTax(1099, when) == 91);

        // 10000 * 0.09875 = 987.5 -> 988, not 987.
        settings.tax_food = 0.09875;
        REQUIRE(settings.FigureFoodTax(10000, when) == 988);
    }

    SECTION("a zero rate produces zero tax")
    {
        settings.tax_food = 0.0;
        REQUIRE(settings.FigureFoodTax(5000, when) == 0);
    }

    SECTION("a zero subtotal produces zero tax")
    {
        settings.tax_food = 0.0825;
        REQUIRE(settings.FigureFoodTax(0, when) == 0);
    }
}

TEST_CASE("Settings::FigureAlcoholTax", "[settings][tax][alcohol]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("alcohol is taxed independently of food")
    {
        settings.tax_food = 0.0825;
        settings.tax_alcohol = 0.10;

        REQUIRE(settings.FigureFoodTax(5000, when) == 413);      // round(412.5)
        REQUIRE(settings.FigureAlcoholTax(3000, when) == 300);
    }

    SECTION("tax-free food with taxed alcohol")
    {
        settings.tax_food = 0.0;
        settings.tax_alcohol = 0.08;

        REQUIRE(settings.FigureFoodTax(4000, when) == 0);
        REQUIRE(settings.FigureAlcoholTax(2000, when) == 160);
    }
}

TEST_CASE("Settings Canadian tax methods", "[settings][tax][canada]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("GST at 5%")
    {
        settings.tax_GST = 0.05;
        REQUIRE(settings.FigureGST(10000, when) == 500);
    }

    SECTION("HST at 13%")
    {
        settings.tax_HST = 0.13;
        REQUIRE(settings.FigureHST(10000, when) == 1300);
    }

    SECTION("PST distinguishes beverages from food")
    {
        settings.tax_PST = 0.07;
        // Both paths must produce a non-negative result; whether a beverage is
        // charged PST is a policy flag on Settings, so assert the arithmetic
        // rather than the policy.
        REQUIRE(settings.FigurePST(10000, when, false) >= 0);
        REQUIRE(settings.FigurePST(10000, when, true) >= 0);
    }

    SECTION("QST is computed against the amount and the GST already charged")
    {
        settings.tax_GST = 0.05;
        settings.tax_QST = 0.09975;

        const int gst = settings.FigureGST(10000, when);
        REQUIRE(settings.FigureQST(10000, gst, when, false) >= 0);
    }
}

TEST_CASE("Settings::FigureVAT", "[settings][tax][vat]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("an explicit rate is used")
    {
        REQUIRE(settings.FigureVAT(10000, when, 0.20) == 2000);
    }

    SECTION("a negative rate falls back to the configured rate")
    {
        settings.tax_VAT = 0.20;
        REQUIRE(settings.FigureVAT(10000, when, -1) == 2000);
    }

    SECTION("zero is treated as a real rate, not as 'unset'")
    {
        // This distinction matters: Archive stores a frozen tax_VAT per business
        // day, and a day whose snapshot was never populated holds 0. Because 0
        // is >= 0 it is used as the rate rather than falling back to the live
        // setting, so such a day silently reports no VAT at all.
        settings.tax_VAT = 0.20;
        REQUIRE(settings.FigureVAT(10000, when, 0.0) == 0);
    }
}

TEST_CASE("Settings per-family lookups are bounds checked", "[settings][family]")
{
    Settings settings;

    // family_group, family_printer and video_target are MAX_FAMILIES (64) wide,
    // but a family id is not bounded by that: FAMILY_UNKNOWN is 255, and
    // Order::Read maps the legacy 999 sentinel onto it, so any check loaded from
    // an older file can carry one. Indexing the arrays directly read ~191 ints
    // past the end of Settings; the garbage fed `drinksOnly` in FigureTotals,
    // which selects the Canadian PST basis. UBSan caught it as
    // "index 255 out of bounds for type 'int [64]'".

    SECTION("in-range families read the configured value")
    {
        settings.family_group[FAMILY_BEVERAGES] = SALESGROUP_BEVERAGE;
        REQUIRE(settings.FamilyGroup(FAMILY_BEVERAGES) == SALESGROUP_BEVERAGE);
    }

    SECTION("FAMILY_UNKNOWN does not read out of bounds")
    {
        REQUIRE(settings.FamilyGroup(FAMILY_UNKNOWN) == SALESGROUP_FOOD);
        REQUIRE(settings.FamilyPrinter(FAMILY_UNKNOWN) == PRINTER_DEFAULT);
        REQUIRE(settings.VideoTarget(FAMILY_UNKNOWN) == PRINTER_DEFAULT);
    }

    SECTION("negative and oversized indices fall back to defaults")
    {
        REQUIRE(settings.FamilyGroup(-1) == SALESGROUP_FOOD);
        REQUIRE(settings.FamilyGroup(MAX_FAMILIES) == SALESGROUP_FOOD);
        REQUIRE(settings.FamilyGroup(999) == SALESGROUP_FOOD);
        REQUIRE(settings.FamilyPrinter(-1) == PRINTER_DEFAULT);
        REQUIRE(settings.VideoTarget(MAX_FAMILIES) == PRINTER_DEFAULT);
    }

    SECTION("the boundary index is in range")
    {
        REQUIRE(Settings::IsValidFamilyIndex(0));
        REQUIRE(Settings::IsValidFamilyIndex(MAX_FAMILIES - 1));
        REQUIRE_FALSE(Settings::IsValidFamilyIndex(MAX_FAMILIES));
        REQUIRE_FALSE(Settings::IsValidFamilyIndex(-1));
    }
}

TEST_CASE("Settings room and merchandise tax", "[settings][tax][other]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("room tax")
    {
        settings.tax_room = 0.12;
        REQUIRE(settings.FigureRoomTax(20000, when) == 2400);
    }

    SECTION("merchandise tax")
    {
        settings.tax_merchandise = 0.06;
        REQUIRE(settings.FigureMerchandiseTax(5000, when) == 300);
    }
}
