/*
 * Characterization tests for SubCheck::FigureTotals (main/business/check.cc)
 *
 * FigureTotals is the function that decides what every customer is charged:
 * ~670 lines and ~131 branch points covering sales categorisation, tax across
 * several jurisdictions, comps, discounts, coupons and employee meals. It had
 * no test coverage, because check.cc was compiled into the vt_main executable
 * and no test could link it.
 *
 * These are characterization tests: they pin down what the code currently does,
 * not what it arguably should. That is the point -- they are the oracle the SQL
 * persistence migration will be checked against, so they must describe today's
 * behaviour precisely, including the parts that look odd. Where behaviour is
 * surprising it is called out in a comment rather than "fixed".
 *
 * Two properties of FigureTotals worth knowing before reading:
 *
 *   - It is not a pure function. It removes and recreates TENDER_CHANGE,
 *     TENDER_OVERAGE and TENDER_MONEY_LOST payments, and overwrites
 *     order->discount, order->is_reduced and payment->value. Calling it twice is
 *     not the same as calling it once with respect to the payment list.
 *
 *   - When a SubCheck belongs to an Archive it reads the archive's frozen tax
 *     rates instead of live Settings. With no archive attached (as here) it uses
 *     the Settings passed in.
 */

#include <catch2/catch_all.hpp>
#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/settings.hh"

namespace {

// Orders are owned by the SubCheck once added, so these are raw new-ed.
Order *MakeOrder(const char *name, int cost, int count, int sales_type)
{
    auto *order = new Order;
    order->item_name.Set(name);
    order->item_cost = cost;
    order->count = count;
    order->item_type = ITEM_NORMAL;
    order->item_family = FAMILY_APPETIZERS;
    order->sales_type = sales_type;
    return order;
}

// A Settings with one simple food rate and everything else off, so a test that
// cares about food tax is not perturbed by the other jurisdictions.
Settings FoodOnlySettings(Flt food_rate)
{
    Settings settings;
    settings.tax_food = food_rate;
    settings.tax_alcohol = 0.0;
    settings.tax_room = 0.0;
    settings.tax_merchandise = 0.0;
    settings.tax_GST = 0.0;
    settings.tax_PST = 0.0;
    settings.tax_HST = 0.0;
    settings.tax_QST = 0.0;
    settings.tax_VAT = 0.0;
    return settings;
}

} // namespace

TEST_CASE("FigureTotals accumulates raw sales", "[figure_totals][sales]")
{
    SECTION("a single food order")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1000);
    }

    SECTION("quantity multiplies into raw sales")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Fries", 350, 3, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1050);
    }

    SECTION("several orders sum")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);
        sub.Add(MakeOrder("Fries", 350, 2, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1700);
    }

    SECTION("an empty subcheck totals to zero")
    {
        Settings settings = FoodOnlySettings(0.0825);
        SubCheck sub;

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 0);
        REQUIRE(sub.total_sales == 0);
        REQUIRE(sub.total_tax_food == 0);
        REQUIRE(sub.balance == 0);
    }

    SECTION("a zero-cost item contributes nothing but is counted")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Water", 0, 1, SALES_FOOD), &settings);
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1000);
    }
}

TEST_CASE("FigureTotals applies food tax", "[figure_totals][tax]")
{
    SECTION("tax is computed on the food subtotal")
    {
        Settings settings = FoodOnlySettings(0.0825);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 10000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 10000);
        REQUIRE(sub.total_tax_food == 825);
    }

    SECTION("total_sales is pre-tax; total_cost is the grand total")
    {
        // Worth stating explicitly because the names invite the opposite
        // reading: total_sales is the sum of the sales categories only, and
        // total_cost is total_sales plus every tax, minus item comps. total_cost
        // is what the customer owes.
        Settings settings = FoodOnlySettings(0.10);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1000);
        REQUIRE(sub.total_tax_food == 100);
        REQUIRE(sub.total_sales == 1000);   // excludes tax
        REQUIRE(sub.total_cost == 1100);    // includes tax
    }

    SECTION("tax rounds half-up, matching Settings::FigureFoodTax")
    {
        Settings settings = FoodOnlySettings(0.0825);
        SubCheck sub;
        sub.Add(MakeOrder("Item", 1099, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        // 1099 * 0.0825 = 90.6675 -> 91
        REQUIRE(sub.total_tax_food == 91);
    }

    SECTION("a zero rate produces no tax")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.total_tax_food == 0);
        REQUIRE(sub.total_sales == 1000);
    }
}

TEST_CASE("FigureTotals separates sales categories", "[figure_totals][categories]")
{
    SECTION("food and alcohol are taxed at their own rates")
    {
        Settings settings = FoodOnlySettings(0.10);
        settings.tax_alcohol = 0.20;

        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);
        sub.Add(MakeOrder("Beer", 500, 1, SALES_ALCOHOL), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 1500);
        REQUIRE(sub.total_tax_food == 100);      // 10% of 1000
        REQUIRE(sub.total_tax_alcohol == 100);   // 20% of 500
    }

    SECTION("merchandise is tracked separately from food")
    {
        Settings settings = FoodOnlySettings(0.10);
        settings.tax_merchandise = 0.05;

        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);
        sub.Add(MakeOrder("T-Shirt", 2000, 1, SALES_MERCHANDISE), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 3000);
        REQUIRE(sub.total_tax_food == 100);
        REQUIRE(sub.total_tax_merchandise == 100);  // 5% of 2000
    }

    SECTION("room charges are tracked separately")
    {
        Settings settings = FoodOnlySettings(0.0);
        settings.tax_room = 0.12;

        SubCheck sub;
        sub.Add(MakeOrder("Room", 20000, 1, SALES_ROOM), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.raw_sales == 20000);
        REQUIRE(sub.total_tax_room == 2400);
    }
}

TEST_CASE("FigureTotals tracks balance against payments", "[figure_totals][balance]")
{
    SECTION("an unpaid subcheck owes its grand total")
    {
        Settings settings = FoodOnlySettings(0.10);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.total_cost == 1100);
        REQUIRE(sub.payment == 0);
        REQUIRE(sub.balance == 1100);
    }

    SECTION("an exact cash payment clears the balance")
    {
        Settings settings = FoodOnlySettings(0.10);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);
        sub.FigureTotals(&settings);

        // Pay total_cost, not total_sales -- the latter omits tax.
        const int due = sub.total_cost;
        sub.Add(new Payment(TENDER_CASH, 0, 0, due), &settings);
        sub.FigureTotals(&settings);

        REQUIRE(sub.payment == due);
        REQUIRE(sub.balance == 0);
        REQUIRE(sub.IsBalanced());
    }

    SECTION("a partial payment leaves the remainder owing")
    {
        Settings settings = FoodOnlySettings(0.0);
        SubCheck sub;
        sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);
        sub.Add(new Payment(TENDER_CASH, 0, 0, 600), &settings);

        sub.FigureTotals(&settings);

        REQUIRE(sub.payment == 600);
        REQUIRE(sub.balance == 400);
        REQUIRE_FALSE(sub.IsBalanced());
    }
}

TEST_CASE("FigureTotals is stable across repeated calls", "[figure_totals][idempotence]")
{
    // The totals must not drift when recomputed, since FigureTotals runs on
    // every load and after every edit. This is the property the SQL migration
    // most depends on: if recomputation were unstable, frozen and recomputed
    // totals could never be reconciled.
    Settings settings = FoodOnlySettings(0.0825);
    SubCheck sub;
    sub.Add(MakeOrder("Burger", 1000, 2, SALES_FOOD), &settings);
    sub.Add(MakeOrder("Beer", 600, 1, SALES_ALCOHOL), &settings);

    sub.FigureTotals(&settings);
    const int raw = sub.raw_sales;
    const int total = sub.total_sales;
    const int food_tax = sub.total_tax_food;
    const int balance = sub.balance;

    for (int i = 0; i < 3; ++i)
    {
        sub.FigureTotals(&settings);
        REQUIRE(sub.raw_sales == raw);
        REQUIRE(sub.total_sales == total);
        REQUIRE(sub.total_tax_food == food_tax);
        REQUIRE(sub.balance == balance);
    }
}

TEST_CASE("FigureTotals respects a changed tax rate", "[figure_totals][rates]")
{
    // Guards the property that makes the Archive policy snapshot necessary:
    // totals follow whatever rate they are handed, so a rate change with no
    // frozen snapshot would silently restate historical checks.
    Settings settings = FoodOnlySettings(0.10);
    SubCheck sub;
    sub.Add(MakeOrder("Burger", 1000, 1, SALES_FOOD), &settings);

    sub.FigureTotals(&settings);
    REQUIRE(sub.total_tax_food == 100);

    settings.tax_food = 0.20;
    sub.FigureTotals(&settings);
    REQUIRE(sub.total_tax_food == 200);
}
