#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Staff: permissions per person, breaks, overtime.

namespace {

int indexOf(PosService &pos, const QString &name)
{
    const QVariantList staff = pos.adminRecords(u"employees"_s);
    for (int i = 0; i < staff.size(); ++i) {
        if (staff[i].toMap()[u"name"_s].toString().startsWith(name))
            return i;
    }
    return -1;
}

} // namespace

TEST_CASE("Permissions per person: yes or no over the role", "[staff][permissions]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const int sam = indexOf(pos, u"Sam"_s);
    REQUIRE(sam >= 0);
    QVariantMap r = pos.adminRecords(u"employees"_s)[sam].toMap();
    CHECK(r[u"perm:order.void"_s].toString().isEmpty());       // as the role
    r[u"perm:order.void"_s] = u"allow"_s;                       // Sam may void sent items
    r[u"perm:check.discount"_s] = u"deny"_s;                    // but not give discounts
    REQUIRE(pos.adminSave(u"employees"_s, sam, r));
    const core::Employee &e = pos.shared()->employees[sam];
    CHECK(e.can(core::perm::Void));
    CHECK_FALSE(e.can(core::perm::Discount));
    CHECK(e.can(core::perm::Order));                           // the rest as a server
    CHECK(pos.adminRecords(u"employees"_s)[sam].toMap()[u"perm:order.void"_s] == u"allow"_s);
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.permissions().contains(u"order.void"_s));
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());
    CHECK(pos.voidItem());                                     // allowed for Sam
    pos.addItem(u"tea"_s);
    CHECK_FALSE(pos.tender(u"discount"_s));                    // denied for Sam
    CHECK(pos.tender(u"cash"_s, 100));                         // payments still fine
    pos.logout();

    // A manager can't take their own manager rights away.
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const int morgan = indexOf(pos, u"Morgan"_s);
    QVariantMap me = pos.adminRecords(u"employees"_s)[morgan].toMap();
    me[u"perm:manager"_s] = u"deny"_s;
    CHECK_FALSE(pos.adminSave(u"employees"_s, morgan, me));

    // Saved with the employee.
    CHECK(app::employeeFromJson(app::toJson(e)) == e);
}

namespace {
std::int64_t todayAt(int hour, int minute = 0)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute)).toMSecsSinceEpoch();
}

std::vector<std::string> row(const core::Report &r, const std::string &first, bool last = false)
{
    std::vector<std::string> found;
    for (const core::ReportRow &x : r.rows) {
        if (!x.cells.empty() && x.cells.front() == first) {
            found = x.cells;
            if (!last)
                break;
        }
    }
    return found;
}
} // namespace

TEST_CASE("Breaks: unpaid by default, ended by clocking out", "[staff][breaks]")
{
    PosService pos(test::seedPosData(), nullptr);
    std::int64_t clock = todayAt(9);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK_FALSE(pos.toggleBreak());                         // not on the clock
    REQUIRE(pos.clockIn());
    clock = todayAt(12);
    REQUIRE(pos.toggleBreak());
    CHECK_FALSE(pos.onBreakSince().isEmpty());
    clock = todayAt(12, 30);
    REQUIRE(pos.toggleBreak());                             // back
    CHECK(pos.onBreakSince().isEmpty());
    clock = todayAt(15);
    REQUIRE(pos.toggleBreak());
    clock = todayAt(15, 15);
    REQUIRE(pos.clockOut());                                // ends the break too
    const core::TimePunch &p = pos.shared()->punches.front();
    REQUIRE(p.breaks.size() == 2);
    CHECK(p.breaks.back().end == todayAt(15, 15));

    // 6h15m on the clock, 45 min on breaks: 5.50 h worked.
    const auto labor = pos.buildReport(u"labor"_s);
    CHECK(row(labor, "Sam")[3] == "0.75");
    CHECK(row(labor, "Sam")[4] == "5.50");
    pos.shared()->settings.paidBreaks = true;
    CHECK(row(pos.buildReport(u"labor"_s), "Sam")[4] == "6.25");

    // Saved with the punch.
    CHECK(app::punchFromJson(app::toJson(p)) == p);
}

TEST_CASE("Overtime: daily and weekly rules, the larger counts", "[staff][overtime]")
{
    app::PosData data = test::seedPosData();
    const std::string sam = [&] {
        for (const core::Employee &e : data.employees)
            if (e.name == "Sam") return e.id;
        return std::string();
    }();
    // Earlier this week: four 9-hour days (36 h).
    const std::int64_t day = 24LL * 3'600'000;
    for (int i = 1; i <= 4; ++i)
        data.earlierPunches.push_back({100 + i, sam, todayAt(8) - i * day, todayAt(17) - i * day, {}});
    PosService pos(data, nullptr);
    std::int64_t clock = todayAt(8);
    pos.shared()->setClock([&] { return clock; });
    // A week that started long enough ago to include those days.
    pos.shared()->settings.weekStartsOn = QDate::currentDate().addDays(-5).dayOfWeek() % 7;
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.clockIn());
    clock = todayAt(18);                                     // 10 h today
    REQUIRE(pos.clockOut());

    // Weekly 40: 46 h this week -> 6 h overtime.
    auto r = pos.buildReport(u"labor"_s);
    auto sams = row(r, "Sam", true);                         // the overtime line
    REQUIRE(sams.size() == 5);
    CHECK(sams[1] == "10.00");
    CHECK(sams[2] == "46.00");
    CHECK(sams[3] == "40.00");
    CHECK(sams[4] == "6.00");

    // Daily 8 as well: 1 h on each earlier day + 2 h today = 6 h; weekly also 6.
    pos.shared()->settings.overtimeDailyHours = 8;
    pos.shared()->settings.overtimeWeeklyHours = 0;
    sams = row(pos.buildReport(u"labor"_s), "Sam", true);
    CHECK(sams[4] == "6.00");

    // No rules: no overtime, and the report says so.
    pos.shared()->settings.overtimeDailyHours = 0;
    r = pos.buildReport(u"labor"_s);
    CHECK(row(r, "Sam", true)[4] == "-");
    CHECK_FALSE(row(r, "No overtime rule is set (Manager -> Settings).").empty());
}
