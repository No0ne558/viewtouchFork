/*
 * Tests for sales-item constants and multi-jurisdiction tax behaviour.
 *
 * This file previously computed expressions like
 *   static_cast<int>(subtotal * tax_rate)
 * in the test body and asserted against its own result. That tested the C++
 * compiler, not ViewTouch -- and it was wrong: the real tax_calc() uses
 * std::round(), so the truncating expectations here disagreed with production
 * on every rate that did not divide evenly. It also pulled in MockSettings,
 * which is now deleted.
 *
 * The arithmetic-only cases are gone. What remains are the two things worth
 * asserting: that the FAMILY_* wire values have not shifted, and that the real
 * Settings tax methods behave correctly when several jurisdictions' rates are
 * configured at once.
 */

#include <catch2/catch_all.hpp>
#include "main/business/sales.hh"
#include "main/data/settings.hh"
#include "src/core/time_info.hh"

namespace {

TimeInfo Now()
{
    TimeInfo t;
    t.Set();
    return t;
}

} // namespace

TEST_CASE("Sales item family values are stable", "[sales][family]")
{
    // These are not arbitrary labels: item_family is written into every stored
    // order, so renumbering a family silently reinterprets historical sales.
    // Any change here must come with a data migration.

    SECTION("core families")
    {
        REQUIRE(FAMILY_APPETIZERS == 0);
        REQUIRE(FAMILY_BEVERAGES == 1);
        REQUIRE(FAMILY_LUNCH_ENTREES == 2);
        REQUIRE(FAMILY_CHILDRENS_MENU == 3);
        REQUIRE(FAMILY_DESSERTS == 4);
        REQUIRE(FAMILY_SANDWICHES == 5);
        REQUIRE(FAMILY_SIDE_ORDERS == 6);
        REQUIRE(FAMILY_BREAKFAST_ENTREES == 7);
    }

    SECTION("specialty families")
    {
        REQUIRE(FAMILY_PIZZA == 14);
        REQUIRE(FAMILY_BEER == 16);
        REQUIRE(FAMILY_WINE == 18);
        REQUIRE(FAMILY_COCKTAIL == 20);
        REQUIRE(FAMILY_MODIFIER == 23);
        REQUIRE(FAMILY_MERCHANDISE == 26);
    }

    SECTION("FAMILY_UNKNOWN sits outside the per-family array bounds")
    {
        // Worth stating explicitly: this is why every family lookup has to be
        // bounds checked. See Settings::FamilyGroup and its tests.
        REQUIRE(FAMILY_UNKNOWN == 255);
        REQUIRE(FAMILY_UNKNOWN >= MAX_FAMILIES);
    }
}

TEST_CASE("Mixed jurisdiction tax rates", "[tax][mixed]")
{
    Settings settings;
    TimeInfo when = Now();

    SECTION("food and alcohol are taxed at their own rates")
    {
        settings.tax_food = 0.0825;
        settings.tax_alcohol = 0.10;

        const int food_tax = settings.FigureFoodTax(5000, when);
        const int alcohol_tax = settings.FigureAlcoholTax(3000, when);

        REQUIRE(food_tax == 413);      // round(412.5); the old test expected 412
        REQUIRE(alcohol_tax == 300);
        REQUIRE(food_tax + alcohol_tax == 713);
    }

    SECTION("configuring one rate does not disturb the others")
    {
        settings.tax_food = 0.07;
        settings.tax_alcohol = 0.0;
        settings.tax_merchandise = 0.05;
        settings.tax_room = 0.11;

        REQUIRE(settings.FigureFoodTax(10000, when) == 700);
        REQUIRE(settings.FigureAlcoholTax(10000, when) == 0);
        REQUIRE(settings.FigureMerchandiseTax(10000, when) == 500);
        REQUIRE(settings.FigureRoomTax(10000, when) == 1100);
    }

    SECTION("Canadian GST and HST coexist with the US-style food rate")
    {
        settings.tax_food = 0.0;
        settings.tax_GST = 0.05;
        settings.tax_HST = 0.13;

        REQUIRE(settings.FigureFoodTax(10000, when) == 0);
        REQUIRE(settings.FigureGST(10000, when) == 500);
        REQUIRE(settings.FigureHST(10000, when) == 1300);
    }
}

TEST_CASE("Tax rounding is half-up at the cent", "[tax][rounding]")
{
    Settings settings;
    TimeInfo when = Now();

    // tax_calc() is std::round(amount * rate). These cases straddle the .5
    // boundary, which is exactly where the old truncating expectations broke.
    SECTION("exactly one half rounds up")
    {
        settings.tax_food = 0.10;
        REQUIRE(settings.FigureFoodTax(15, when) == 2);   // 1.5 -> 2
    }

    SECTION("just below one half rounds down")
    {
        settings.tax_food = 0.0825;
        REQUIRE(settings.FigureFoodTax(100, when) == 8);  // 8.25 -> 8
    }

    SECTION("just above one half rounds up")
    {
        settings.tax_food = 0.0875;
        REQUIRE(settings.FigureFoodTax(100, when) == 9);  // 8.75 -> 9
    }

    SECTION("a large subtotal stays exact")
    {
        settings.tax_food = 0.0825;
        REQUIRE(settings.FigureFoodTax(1000000, when) == 82500);
    }
}
