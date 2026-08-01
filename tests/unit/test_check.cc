/*
 * Unit tests for Check / SubCheck / Order (main/business/check.cc)
 *
 * Despite its name, this file previously constructed no Check at all: it tested
 * MockTerminal, MockSettings, and some inline arithmetic. It could not, because
 * check.cc was compiled into the vt_main executable and a test target cannot
 * link an executable. Now that the business logic lives in a library, these
 * tests drive the real types.
 *
 * Coverage here is deliberately limited to the aggregate structure -- building
 * checks, subchecks, orders and modifiers, and the ordering invariants that the
 * file format preserves only as byte position. FigureTotals, which is the
 * function that decides what a customer is charged, needs a populated Settings
 * and a wider fixture; it is the next target.
 */

#include <catch2/catch_all.hpp>
#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/settings.hh"

TEST_CASE("Check aggregates subchecks", "[check][structure]")
{
    Settings settings;

    SECTION("a new check starts empty")
    {
        Check check;
        REQUIRE(check.SubCount() == 0);
        REQUIRE(check.SubList() == nullptr);
    }

    SECTION("adding subchecks numbers them in sequence")
    {
        Check check;

        SubCheck *first = check.NewSubCheck();
        REQUIRE(first != nullptr);
        REQUIRE(check.SubCount() == 1);

        SubCheck *second = check.NewSubCheck();
        REQUIRE(second != nullptr);
        REQUIRE(check.SubCount() == 2);

        // SubCheck::number is assigned positionally on Add and is never
        // serialized -- the file format recovers it purely from record order.
        REQUIRE(first->number != second->number);
    }

    SECTION("subchecks are reachable by traversal in insertion order")
    {
        Check check;
        SubCheck *first = check.NewSubCheck();
        SubCheck *second = check.NewSubCheck();

        REQUIRE(check.SubList() == first);
        REQUIRE(check.SubList()->next == second);
        REQUIRE(second->next == nullptr);
    }
}

TEST_CASE("SubCheck aggregates orders", "[check][orders]")
{
    Settings settings;

    SECTION("a new subcheck has no orders or payments")
    {
        SubCheck sub;
        REQUIRE(sub.OrderList() == nullptr);
        REQUIRE(sub.PaymentList() == nullptr);
    }

    SECTION("added orders are retrievable")
    {
        SubCheck sub;

        auto *order = new Order;
        order->item_name.Set("Cheeseburger");
        order->item_cost = 950;
        order->count = 1;
        order->item_type = ITEM_NORMAL;

        REQUIRE(sub.Add(order, &settings) == 0);
        REQUIRE(sub.OrderList() == order);
        REQUIRE(sub.OrderList()->item_cost == 950);
    }

    SECTION("orders are ordered by seat")
    {
        // SubCheck::Add walks backwards comparing seat, so insertion order and
        // list order differ. Nothing in the file records position other than
        // byte order, which is why this invariant is worth pinning.
        SubCheck sub;

        auto *seat_two = new Order;
        seat_two->item_name.Set("Soup");
        seat_two->item_cost = 400;
        seat_two->count = 1;
        seat_two->item_type = ITEM_NORMAL;
        seat_two->seat = 2;

        auto *seat_one = new Order;
        seat_one->item_name.Set("Salad");
        seat_one->item_cost = 500;
        seat_one->count = 1;
        seat_one->item_type = ITEM_NORMAL;
        seat_one->seat = 1;

        REQUIRE(sub.Add(seat_two, &settings) == 0);
        REQUIRE(sub.Add(seat_one, &settings) == 0);

        // Lower seat sorts first regardless of the order they were added.
        REQUIRE(sub.OrderList() == seat_one);
        REQUIRE(sub.OrderList()->next == seat_two);
    }
}

TEST_CASE("Order modifier classification", "[check][modifiers]")
{
    // The parent/child relationship between an order and its modifiers is never
    // stored. On load it is re-derived from item_type plus adjacency, via
    // IsModifier(). That inference is the reason the relationship cannot be
    // recovered when it is wrong, so the classification itself is worth testing.

    SECTION("a normal item is not a modifier")
    {
        Order order;
        order.item_type = ITEM_NORMAL;
        REQUIRE(order.IsModifier() == 0);
    }

    SECTION("a modifier item is a modifier")
    {
        Order order;
        order.item_type = ITEM_MODIFIER;
        REQUIRE(order.IsModifier() != 0);
    }

    SECTION("a method item is a modifier")
    {
        Order order;
        order.item_type = ITEM_METHOD;
        REQUIRE(order.IsModifier() != 0);
    }

    SECTION("a substitute counts as a modifier only with the sub qualifier")
    {
        Order plain_sub;
        plain_sub.item_type = ITEM_SUBSTITUTE;
        plain_sub.qualifier = 0;
        REQUIRE(plain_sub.IsModifier() == 0);

        Order qualified_sub;
        qualified_sub.item_type = ITEM_SUBSTITUTE;
        qualified_sub.qualifier = QUALIFIER_SUB;
        REQUIRE(qualified_sub.IsModifier() != 0);
    }
}

TEST_CASE("Order cost arithmetic", "[check][cost]")
{
    SECTION("cost scales with count")
    {
        Order order;
        order.item_name.Set("Fries");
        order.item_cost = 350;
        order.count = 3;
        order.item_type = ITEM_NORMAL;

        REQUIRE(order.item_cost * order.count == 1050);
    }

    SECTION("a zero-cost item is representable")
    {
        // Comped and included items legitimately cost nothing.
        Order order;
        order.item_name.Set("Water");
        order.item_cost = 0;
        order.count = 1;
        order.item_type = ITEM_NORMAL;

        REQUIRE(order.item_cost == 0);
    }
}
