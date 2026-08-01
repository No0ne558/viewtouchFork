/*
 * Unit tests for CouponInfo discount arithmetic (main/data/settings.cc)
 *
 * This file previously defined its own CalculateCouponAmount() and asserted
 * against that, with a comment saying it "mirrors the logic in
 * CouponInfo::Amount()". It could not catch a regression in the real function,
 * and by the time the business logic became linkable the copy had already
 * drifted: the production Amount() honours `active` and the TF_SUBSTITUTE flag,
 * and the mirror handled neither.
 *
 * These tests call the real CouponInfo.
 */

#include <catch2/catch_all.hpp>
#include "main/data/settings.hh"

namespace {

// A coupon in the state the constructor leaves it, then configured per test.
// active defaults to 0, which means Amount() returns 0 regardless of amount --
// see the "inactive" case below.
CouponInfo MakeCoupon(int amount, int flags, short active = 1)
{
    CouponInfo coupon;
    coupon.amount = amount;
    coupon.flags = flags;
    coupon.active = active;
    return coupon;
}

} // namespace

TEST_CASE("CouponInfo::Amount computes what the customer pays", "[coupon][amount]")
{
    SECTION("a flat discount is subtracted from the item cost")
    {
        // $2.00 off a $10.00 item leaves $8.00.
        CouponInfo coupon = MakeCoupon(200, 0);
        REQUIRE(coupon.Amount(1000) == 800);
    }

    SECTION("a percentage discount scales the item cost")
    {
        // Percentages are stored scaled by 10000, so 2000 == 20.00%.
        // 20% off $10.00 leaves $8.00.
        CouponInfo coupon = MakeCoupon(2000, TF_IS_PERCENT);
        REQUIRE(coupon.Amount(1000) == 800);
    }

    SECTION("a substitution coupon replaces the price outright")
    {
        // TF_SUBSTITUTE means "this item now costs `amount`", not "take `amount`
        // off". The previous mirror implementation did not model this at all.
        CouponInfo coupon = MakeCoupon(599, TF_SUBSTITUTE);
        REQUIRE(coupon.Amount(1000) == 599);
    }

    SECTION("an inactive coupon discounts nothing")
    {
        // Also unmodelled by the previous mirror: an inactive coupon yields 0,
        // not the undiscounted price.
        CouponInfo coupon = MakeCoupon(200, 0, /*active=*/0);
        REQUIRE(coupon.Amount(1000) == 0);
    }

    SECTION("the result scales with item count")
    {
        CouponInfo coupon = MakeCoupon(200, 0);
        REQUIRE(coupon.Amount(1000, 3) == 2400);   // 3 x $8.00

        CouponInfo percent = MakeCoupon(2500, TF_IS_PERCENT);
        REQUIRE(percent.Amount(1000, 4) == 3000);  // 4 x $7.50
    }

    SECTION("a 100% coupon makes the item free")
    {
        CouponInfo coupon = MakeCoupon(10000, TF_IS_PERCENT);
        REQUIRE(coupon.Amount(1000) == 0);
    }

    SECTION("a 0% coupon leaves the price untouched")
    {
        CouponInfo coupon = MakeCoupon(0, TF_IS_PERCENT);
        REQUIRE(coupon.Amount(1250) == 1250);
    }

    SECTION("a flat discount larger than the item goes negative")
    {
        // Documents current behaviour rather than endorsing it: Amount() does
        // not clamp at zero, so an over-large coupon produces a negative price
        // that the caller is responsible for handling.
        CouponInfo coupon = MakeCoupon(1500, 0);
        REQUIRE(coupon.Amount(1000) == -500);
    }
}

TEST_CASE("CouponInfo::CPAmount computes the coupon's own value", "[coupon][cpamount]")
{
    // Amount() is what the customer pays; CPAmount() is what the coupon is
    // worth. For reporting, the two must be complementary.

    SECTION("a flat coupon is worth its face value")
    {
        CouponInfo coupon = MakeCoupon(200, 0);
        REQUIRE(coupon.CPAmount(1000) == 200);
    }

    SECTION("a percentage coupon is worth the amount discounted")
    {
        CouponInfo coupon = MakeCoupon(2000, TF_IS_PERCENT);
        REQUIRE(coupon.CPAmount(1000) == 200);
    }

    SECTION("an inactive coupon is worth nothing")
    {
        CouponInfo coupon = MakeCoupon(200, 0, /*active=*/0);
        REQUIRE(coupon.CPAmount(1000) == 0);
    }

    SECTION("coupon value scales with item count")
    {
        CouponInfo coupon = MakeCoupon(200, 0);
        REQUIRE(coupon.CPAmount(1000, 3) == 600);
    }

    SECTION("price paid plus coupon value equals the original price")
    {
        // The invariant that makes discount reporting reconcile.
        const int item_cost = 1000;

        CouponInfo flat = MakeCoupon(250, 0);
        REQUIRE(flat.Amount(item_cost) + flat.CPAmount(item_cost) == item_cost);

        CouponInfo percent = MakeCoupon(3000, TF_IS_PERCENT);
        REQUIRE(percent.Amount(item_cost) + percent.CPAmount(item_cost) == item_cost);
    }
}
