/*
 * Characterization tests for WorkEntry time and pay arithmetic
 * (main/business/labor.cc)
 *
 * These compute payroll. They had no coverage, because labor.cc was compiled
 * into the vt_main executable and no test could link it.
 *
 * TimeInfo is date::local_time<seconds> (time_info.hh:36) -- wall-clock time
 * with no zone attached. Durations used to be computed by subtracting two of
 * those readings, which is not elapsed real time across a daylight-saving
 * transition. The last two cases below cover the fix: the calculation against
 * a pinned zone, and the wiring that gets payroll to it.
 */

#include <catch2/catch_all.hpp>
#include "main/business/check.hh"
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

TEST_CASE("A shift spanning a DST transition measures elapsed real time",
          "[labor][time][dst]")
{
    // The defect this replaces: TimeInfo is date::local_time<seconds>, a
    // wall-clock reading with no zone, and SecondsElapsed used to subtract two
    // of them directly. A night shift across a transition was therefore off by
    // a full hour -- in payroll.
    //
    // The zone is pinned rather than taken from the machine. date's
    // current_zone() reads /etc/localtime and ignores TZ, so a test process
    // cannot choose its own zone; CI also runs in UTC, which has no
    // transitions to span. Passing the zone in is what makes these figures
    // reproducible on any machine.
    const date::time_zone *ny = date::locate_zone("America/New_York");
    REQUIRE(ny != nullptr);

    // Day indices are 0-based from Jan 1, matching TimeInfo::Set's encoding.
    // 2026 is not a leap year, so day 65 is Mar 7 and day 66 Mar 8 (clocks go
    // forward 02:00 -> 03:00); day 303 is Oct 31 and day 304 Nov 1 (clocks go
    // back 02:00 -> 01:00).
    constexpr int mar_7 = 65, mar_8 = 66, oct_31 = 303, nov_1 = 304;

    SECTION("clocks forward: eight hours on the clock, seven worked")
    {
        const TimeInfo in  = At(2026, mar_7, 22);
        const TimeInfo out = At(2026, mar_8, 6);

        REQUIRE(SecondsElapsedIn(ny, out, in) == 7 * 3600);

        // Falsification, permanently in the test rather than done once by hand:
        // a null zone is the arithmetic this used to do, and it is an hour out.
        REQUIRE(SecondsElapsedIn(nullptr, out, in) == 8 * 3600);
    }

    SECTION("clocks back: eight hours on the clock, nine worked")
    {
        const TimeInfo in  = At(2026, oct_31, 22);
        const TimeInfo out = At(2026, nov_1, 6);

        REQUIRE(SecondsElapsedIn(ny, out, in) == 9 * 3600);
        REQUIRE(SecondsElapsedIn(nullptr, out, in) == 8 * 3600);
    }

    SECTION("the same wall-clock span differs by date, which is the point")
    {
        // Two 22:00->06:00 shifts a week either side of the spring transition.
        // The old arithmetic reported them equal; they are an hour apart.
        const int across = SecondsElapsedIn(ny, At(2026, mar_8, 6), At(2026, mar_7, 22));
        const int clear  = SecondsElapsedIn(ny, At(2026, mar_8 + 7, 6),
                                                At(2026, mar_7 + 7, 22));
        REQUIRE(clear - across == 3600);
    }

    SECTION("a break inside the repeated hour is not inflated")
    {
        // Both readings resolve with choose::earliest, so 01:15 -> 01:45 on the
        // autumn transition night is thirty minutes. Resolving the two ends
        // differently -- widening the interval to be safe -- would report
        // ninety, every autumn, for every short break in that hour.
        REQUIRE(SecondsElapsedIn(ny, At(2026, nov_1, 1, 45),
                                     At(2026, nov_1, 1, 15)) == 30 * 60);
    }

    SECTION("a reading in the hour that never happened still yields a duration")
    {
        // 02:30 on the spring transition day does not exist. date resolves it
        // to the transition instant, so 01:30 -> 02:30 is thirty minutes of
        // real time rather than an exception thrown at a payroll report.
        REQUIRE(SecondsElapsedIn(ny, At(2026, mar_8, 2, 30),
                                     At(2026, mar_8, 1, 30)) == 30 * 60);
    }

    SECTION("argument order does not matter, as before")
    {
        const TimeInfo in  = At(2026, mar_7, 22);
        const TimeInfo out = At(2026, mar_8, 6);
        REQUIRE(SecondsElapsedIn(ny, in, out) == SecondsElapsedIn(ny, out, in));
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "WorkEntry and Check durations use the zone-aware path",
                 "[labor][time][dst]")
{
    // The wiring claim. The section above proves the calculation is correct
    // against a pinned zone; this proves the payroll and check-age callers
    // actually reach it, rather than a second copy of the old subtraction.
    //
    // It cannot assert a DST figure end-to-end, because those callers resolve
    // against the machine's zone and the machine's zone is not ours to choose.
    // What it can assert is identity with the zone-aware function, which is
    // what makes the section above evidence about MinutesWorked at all.
    WorkEntry entry = MakeShift(At(2026, 65, 22), At(2026, 66, 6));

    const date::time_zone *machine = nullptr;
    REQUIRE_NOTHROW(machine = date::current_zone());

    REQUIRE(entry.MinutesWorked()
            == SecondsElapsedIn(machine, entry.end, entry.start) / 60);

    // Check::SecondsOpen measures time_open to the latest settle_time, and
    // returns time-to-now while any subcheck is still CHECK_OPEN -- so the
    // subcheck has to be settled for the closed-check figure to be reachable.
    Check check;
    check.time_open = At(2026, 65, 22);
    SubCheck *sub = check.NewSubCheck();
    REQUIRE(sub != nullptr);
    sub->status = CHECK_CLOSED;
    sub->settle_time = At(2026, 66, 6);
    REQUIRE(check.TimeClosed() != nullptr);

    REQUIRE(check.SecondsOpen()
            == SecondsElapsedIn(machine, At(2026, 66, 6), At(2026, 65, 22)));
}
