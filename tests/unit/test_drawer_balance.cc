/*
 * Characterization tests for Drawer cash reconciliation
 * (main/hardware/drawer.cc)
 *
 * The drawer is where the till's counted cash is compared against what the
 * system thinks it took. It had no coverage, because drawer.cc was compiled
 * into the vt_main executable and no test could link it.
 *
 * These pin current behaviour rather than proposing better behaviour: the
 * numbers a drawer report shows today are what the SQL migration has to keep
 * producing. Two properties are worth knowing before reading:
 *
 *   - A DrawerBalance carries both a calculated `amount` and an operator-keyed
 *     `entered`. Only `entered` is persisted; `amount` and `count` are always
 *     recomputed. So the "expected" side of a balance is derived and only the
 *     counted-cash side is stored.
 *
 *   - Which of the two Balance() returns depends on the media_balanced
 *     bitfield, not on which is more recent.
 */

#include <catch2/catch_all.hpp>
#include "main/hardware/drawer.hh"
#include "main/data/settings.hh"
#include "support/vt_test_env.hh"

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Drawer status is derived from its timestamps", "[drawer][status]")
{
    // GetStatus() does not read a stored status field; it infers one from which
    // of start_time / pull_time / balance_time have been set.
    SECTION("a fresh drawer is open")
    {
        Drawer drawer;
        REQUIRE(drawer.GetStatus() == DRAWER_OPEN);
    }

    SECTION("setting pull_time makes it pulled")
    {
        Drawer drawer;
        drawer.pull_time.Set();
        REQUIRE(drawer.GetStatus() == DRAWER_PULLED);
    }

    SECTION("setting balance_time makes it balanced")
    {
        Drawer drawer;
        drawer.pull_time.Set();
        drawer.balance_time.Set();
        REQUIRE(drawer.GetStatus() == DRAWER_BALANCED);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Drawer::IsBalanced", "[drawer][balance]")
{
    SECTION("an empty drawer counts as balanced")
    {
        // Nothing went through it, so there is nothing to reconcile.
        Drawer drawer;
        REQUIRE(drawer.IsEmpty());
        REQUIRE(drawer.IsBalanced());
    }

    SECTION("a drawer with activity is unbalanced until balance_time is set")
    {
        Drawer drawer;
        drawer.total_checks = 1;
        drawer.total_payments = 1000;

        REQUIRE_FALSE(drawer.IsEmpty());
        REQUIRE_FALSE(drawer.IsBalanced());

        drawer.balance_time.Set();
        REQUIRE(drawer.IsBalanced());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Drawer::Balance selects calculated or entered",
                 "[drawer][balance][media]")
{
    Drawer drawer;

    auto *balance = new DrawerBalance(TENDER_CASH, 0);
    balance->amount = 10000;    // what the system calculated
    balance->entered = 9950;    // what the operator counted
    drawer.Add(balance);

    SECTION("without the media_balanced bit, the calculated amount is returned")
    {
        drawer.media_balanced = 0;
        REQUIRE(drawer.Balance(TENDER_CASH, 0) == 10000);
    }

    SECTION("with the media_balanced bit set, the entered amount is returned")
    {
        // This is the switch that makes a drawer report show counted cash
        // instead of expected cash, and it is per tender type.
        drawer.media_balanced = (1 << TENDER_CASH);
        REQUIRE(drawer.Balance(TENDER_CASH, 0) == 9950);
    }

    SECTION("an unknown tender type balances to zero")
    {
        REQUIRE(drawer.Balance(TENDER_CHECK, 0) == 0);
    }

    SECTION("a matching tender with a different id does not match")
    {
        REQUIRE(drawer.Balance(TENDER_CASH, 7) == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Drawer::TotalPaymentAmount sums payments of one tender",
                 "[drawer][payments]")
{
    // This function looped forever on any non-empty drawer: the loop body never
    // advanced its cursor. It was invisible because nothing calls it yet -- an
    // empty payment list leaves the cursor null and the loop never executes.
    // Without the fix these sections hang until the ctest timeout rather than
    // failing fast.
    Drawer drawer;
    TimeInfo when;
    when.Set();

    drawer.Add(new DrawerPayment(TENDER_CASH, 1500, 1, when, 0));
    drawer.Add(new DrawerPayment(TENDER_CASH, 2500, 1, when, 0));
    drawer.Add(new DrawerPayment(TENDER_CHECK, 700, 1, when, 0));

    SECTION("payments of the requested tender are summed")
    {
        REQUIRE(drawer.TotalPaymentAmount(TENDER_CASH) == 4000);
    }

    SECTION("other tenders are excluded")
    {
        REQUIRE(drawer.TotalPaymentAmount(TENDER_CHECK) == 700);
    }

    SECTION("a tender with no payments totals zero")
    {
        REQUIRE(drawer.TotalPaymentAmount(TENDER_CHARGE_CARD) == 0);
    }

    SECTION("an empty drawer totals zero")
    {
        Drawer empty;
        REQUIRE(empty.TotalPaymentAmount(TENDER_CASH) == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Drawer aggregates its child records", "[drawer][structure]")
{
    SECTION("added payments are retrievable and counted")
    {
        Drawer drawer;
        TimeInfo when;
        when.Set();

        REQUIRE(drawer.PaymentList() == nullptr);
        drawer.Add(new DrawerPayment(TENDER_CASH, 1000, 1, when, 0));

        REQUIRE(drawer.PaymentList() != nullptr);
        REQUIRE(drawer.PaymentList()->amount == 1000);
        REQUIRE(drawer.PaymentList()->tender_type == TENDER_CASH);
    }

    SECTION("added balances are retrievable")
    {
        Drawer drawer;
        REQUIRE(drawer.BalanceList() == nullptr);

        drawer.Add(new DrawerBalance(TENDER_CASH, 0));
        REQUIRE(drawer.BalanceList() != nullptr);
        REQUIRE(drawer.BalanceList()->tender_type == TENDER_CASH);
    }

    SECTION("Purge empties the drawer")
    {
        Drawer drawer;
        TimeInfo when;
        when.Set();
        drawer.Add(new DrawerPayment(TENDER_CASH, 1000, 1, when, 0));
        drawer.Add(new DrawerBalance(TENDER_CASH, 0));

        drawer.Purge();

        REQUIRE(drawer.PaymentList() == nullptr);
        REQUIRE(drawer.BalanceList() == nullptr);
    }
}
