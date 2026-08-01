/*
 * Characterization tests for WorkEntry time and pay arithmetic
 * (main/business/labor.cc)
 *
 * These compute payroll. They had no coverage, because labor.cc was compiled
 * into the vt_main executable and no test could link it.
 *
 * TimeInfo is date::local_time<seconds> (time_info.hh:36) -- wall-clock time
 * with no zone attached -- so a duration that spans a DST transition is
 * measured in wall-clock terms rather than elapsed real time. The last test
 * case below pins that explicitly; see its comment for what it does and does
 * not prove.
 */

#include <catch2/catch_all.hpp>
#include "main/business/labor.hh"
#include "main/data/settings.hh"
#include "src/core/time_info.hh"
#include "support/vt_test_env.hh"

#include <stdexcept>

namespace {

// Seconds since midnight Jan 1 of `year`, which is exactly how TimeInfo::Set
// interprets its two arguments.
TimeInfo At(int year, int day_of_year, int hour, int minute = 0)
{
    TimeInfo t;
    t.Set(((day_of_year * 24 + hour) * 60 + minute) * 60, year);
    return t;
}

WorkEntry MakeShift(TimeInfo start, TimeInfo end, int pay_amount = 1500)
{
    WorkEntry entry;
    entry.user_id = 42;
    entry.start = start;
    entry.end = end;
    entry.pay_rate = PERIOD_HOUR;
    entry.pay_amount = pay_amount;   // cents per hour
    return entry;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry::MinutesWorked", "[labor][time]")
{
    SECTION("a completed shift measures start to end")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17));
        REQUIRE(entry.MinutesWorked() == 8 * 60);
    }

    SECTION("partial hours are counted in minutes")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9, 15), At(2026, 100, 12, 45));
        REQUIRE(entry.MinutesWorked() == 3 * 60 + 30);
    }

    SECTION("a shift crossing midnight is measured correctly")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 22), At(2026, 101, 2));
        REQUIRE(entry.MinutesWorked() == 4 * 60);
    }

    SECTION("an inverted shift is worth nothing")
    {
        // A clock-out keyed before the clock-in used to bill the whole span --
        // 17:00 -> 09:00 paid eight hours. MinutesWorked ended with
        // `if (minute < 0) minute = 0;`, which stated the intent but could never
        // fire, because MinutesElapsed defers to SecondsElapsed and that returns
        // an absolute magnitude. MinutesWorked now orders the operands itself.
        WorkEntry entry = MakeShift(At(2026, 100, 17), At(2026, 100, 9));
        REQUIRE(entry.MinutesWorked() == 0);
    }

    SECTION("an inverted shift costs nothing")
    {
        // The consequence that mattered: pay followed the bogus duration.
        WorkEntry entry = MakeShift(At(2026, 100, 17), At(2026, 100, 9), 1500);
        REQUIRE(entry.LaborCost() == 0);
    }

    SECTION("a zero-length shift is zero minutes")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 9));
        REQUIRE(entry.MinutesWorked() == 0);
    }

    SECTION("an unset start reports zero rather than throwing")
    {
        // SecondsElapsed throws std::invalid_argument on an unset operand, and
        // MinutesWorked used to pass `start` straight through -- so a malformed
        // work entry turned a labor report into a process exit on a till that
        // runs unattended for a whole shift. MinutesWorked now checks first.
        WorkEntry entry;
        entry.pay_rate = PERIOD_HOUR;
        entry.pay_amount = 1500;
        entry.end = At(2026, 100, 17);

        REQUIRE_NOTHROW(entry.MinutesWorked());
        REQUIRE(entry.MinutesWorked() == 0);
        REQUIRE(entry.LaborCost() == 0);
    }

    SECTION("IsWorkDone follows whether end is set")
    {
        WorkEntry open_entry;
        open_entry.start = At(2026, 100, 9);
        REQUIRE_FALSE(open_entry.IsWorkDone());

        open_entry.end = At(2026, 100, 17);
        REQUIRE(open_entry.IsWorkDone());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry::MinutesWorked with a cutoff", "[labor][time][cutoff]")
{
    // The one-argument overload caps the shift at a supplied end, which is how
    // a report bounds a shift to the period it covers.
    SECTION("a cutoff inside the shift truncates it")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17));
        TimeInfo cutoff = At(2026, 100, 13);
        REQUIRE(entry.MinutesWorked(cutoff) == 4 * 60);
    }

    SECTION("a cutoff after the shift leaves it whole")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17));
        TimeInfo cutoff = At(2026, 100, 23);
        REQUIRE(entry.MinutesWorked(cutoff) == 8 * 60);
    }

    SECTION("a cutoff before the shift yields zero")
    {
        // Same root cause as the inverted-shift case: the cutoff moved the
        // effective end to 05:00 while start stayed at 09:00, and the absolute
        // difference reported four hours for a window the shift never touched,
        // so a report bounded to a period ending before a shift began still
        // attributed time to it.
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17));
        TimeInfo cutoff = At(2026, 100, 5);
        REQUIRE(entry.MinutesWorked(cutoff) == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry::LaborCost", "[labor][pay]")
{
    SECTION("an hourly shift costs rate times hours")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17), 1500);
        REQUIRE(entry.LaborCost() == 8 * 1500);   // 8h at $15.00
    }

    SECTION("a half hour costs half the rate")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 9, 30), 1500);
        REQUIRE(entry.LaborCost() == 750);
    }

    SECTION("a non-hourly pay rate costs nothing here")
    {
        // Salaried staff are not costed per entry; LaborCost returns 0 for any
        // pay_rate other than PERIOD_HOUR.
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17), 1500);
        entry.pay_rate = PERIOD_WEEK;
        REQUIRE(entry.LaborCost() == 0);
    }

    SECTION("a zero-length shift costs nothing")
    {
        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 9), 1500);
        REQUIRE(entry.LaborCost() == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry::Overlap", "[labor][overlap]")
{
    // Used to attribute a shift to a reporting window.
    WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 17));

    SECTION("a window containing the shift returns the whole shift")
    {
        TimeInfo from = At(2026, 100, 0);
        TimeInfo to = At(2026, 101, 0);
        REQUIRE(entry.Overlap(from, to) == 8 * 60);
    }

    SECTION("a window inside the shift returns the window")
    {
        TimeInfo from = At(2026, 100, 10);
        TimeInfo to = At(2026, 100, 12);
        REQUIRE(entry.Overlap(from, to) == 2 * 60);
    }

    SECTION("a partially overlapping window returns the intersection")
    {
        TimeInfo from = At(2026, 100, 15);
        TimeInfo to = At(2026, 100, 20);
        REQUIRE(entry.Overlap(from, to) == 2 * 60);
    }

    SECTION("a disjoint window returns no overlap")
    {
        TimeInfo from = At(2026, 100, 18);
        TimeInfo to = At(2026, 100, 20);
        REQUIRE(entry.Overlap(from, to) == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry::MinutesOvertime", "[labor][overtime]")
{
    Settings settings;
    TimeInfo unset;   // deliberately not Set(); MinutesOvertime falls back

    SECTION("no overtime rules means no overtime")
    {
        settings.overtime_shift = 0;
        settings.overtime_week = 0;

        WorkEntry entry = MakeShift(At(2026, 100, 8), At(2026, 100, 20));
        REQUIRE(entry.MinutesOvertime(&settings, unset) == 0);
    }

    SECTION("a shift under the daily threshold has no overtime")
    {
        settings.overtime_shift = 8;
        settings.overtime_week = 0;

        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 16));
        REQUIRE(entry.MinutesOvertime(&settings, unset) == 0);
    }

    SECTION("a shift over the daily threshold reports the excess")
    {
        settings.overtime_shift = 8;
        settings.overtime_week = 0;

        WorkEntry entry = MakeShift(At(2026, 100, 8), At(2026, 100, 18));
        REQUIRE(entry.MinutesOvertime(&settings, unset) == 2 * 60);
    }

    SECTION("overtime never exceeds the length of the entry")
    {
        // The function ends with Min(minute, amount) for exactly this reason.
        settings.overtime_shift = 1;
        settings.overtime_week = 0;

        WorkEntry entry = MakeShift(At(2026, 100, 9), At(2026, 100, 11));
        const int overtime = entry.MinutesOvertime(&settings, unset);
        REQUIRE(overtime <= entry.MinutesWorked());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Shift duration is wall-clock, not elapsed real time",
                 "[labor][time][dst][known-limitation]")
{
    // TimeInfo is date::local_time<seconds>: a naive wall-clock reading with no
    // zone. MinutesElapsed subtracts two such readings, so a shift that spans a
    // DST transition is off by an hour of real time -- an 8-hour night shift
    // across a spring-forward boundary is really 7 hours worked, and across
    // autumn's is really 9.
    //
    // What this test pins is the mechanism, not a specific jurisdiction's
    // transition date: the arithmetic is pure wall-clock subtraction, with no
    // zone consulted anywhere. That is what makes the discrepancy possible, and
    // it is why the SQL schema stores a UTC companion alongside the local time
    // rather than migrating the local value alone.
    //
    // Not fixed here: correcting it needs a timezone attached to stored times,
    // which is a data-model change, and payroll numbers should not shift
    // underneath anyone as a side effect of a test-coverage pass.

    SECTION("identical wall-clock spans measure identically regardless of date")
    {
        // Day 68 and day 300 of 2026 sit on opposite sides of both US DST
        // transitions. Real elapsed time across these two 02:00->04:00 spans
        // differs by an hour in any DST-observing zone; wall-clock arithmetic
        // reports them as equal.
        WorkEntry spring = MakeShift(At(2026, 68, 2), At(2026, 68, 4));
        WorkEntry autumn = MakeShift(At(2026, 300, 2), At(2026, 300, 4));

        REQUIRE(spring.MinutesWorked() == 2 * 60);
        REQUIRE(autumn.MinutesWorked() == 2 * 60);
        REQUIRE(spring.MinutesWorked() == autumn.MinutesWorked());
    }

    SECTION("pay follows the wall-clock duration")
    {
        // The consequence: an employee is paid for wall-clock hours, so the
        // spring-forward shift is overpaid by an hour and the autumn one
        // underpaid, relative to hours actually present.
        WorkEntry entry = MakeShift(At(2026, 68, 1), At(2026, 68, 5), 1500);
        REQUIRE(entry.MinutesWorked() == 4 * 60);
        REQUIRE(entry.LaborCost() == 4 * 1500);
    }
}
