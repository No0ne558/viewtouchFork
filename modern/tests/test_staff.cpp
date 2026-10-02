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

namespace {
QStringList tipRow(PosService &pos, const QString &name)
{
    for (const QVariant &v : pos.report(u"tips"_s)[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == name)
            return cells;
    }
    return {};
}
} // namespace

TEST_CASE("Schedule: shifts, next shift, clock in only on the schedule, manager override", "[staff][schedule]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(15);
    pos.shared()->setClock([&] { return clock; });
    const QString today = QDate::currentDate().toString(u"yyyy-MM-dd"_s);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.addShift({{u"employeeId"_s, u"sam"_s}, {u"start"_s, today + u" 16:00"_s}, {u"end"_s, today + u" 22:00"_s},
                          {u"note"_s, u"patio"_s}}));
    CHECK_FALSE(pos.addShift({{u"employeeId"_s, u"sam"_s}, {u"start"_s, today + u" 20:00"_s},
                              {u"end"_s, today + u" 23:00"_s}}));            // overlaps
    CHECK_FALSE(pos.addShift({{u"employeeId"_s, u"nobody"_s}, {u"start"_s, today + u" 9:00"_s}}));
    // Riley closes: past midnight.
    REQUIRE(pos.addShift({{u"employeeId"_s, u"riley"_s}, {u"start"_s, today + u" 18:00"_s}, {u"end"_s, today + u" 01:00"_s}}));
    CHECK(pos.shared()->shifts.back().hours() == 7);

    const QVariantMap week = pos.scheduleInfo();
    QVariantMap day;
    for (const QVariant &d : week[u"days"_s].toList())
        if (d.toMap()[u"today"_s].toBool()) day = d.toMap();
    REQUIRE(day[u"shifts"_s].toList().size() == 2);
    CHECK(day[u"shifts"_s].toList()[0].toMap()[u"time"_s] == u"4 PM - 10 PM"_s);
    CHECK(week[u"totals"_s].toList().size() == 2);
    pos.logout();

    // Clock in only on the schedule.
    pos.shared()->settings.scheduleRequired = true;
    REQUIRE(pos.loginWithPin(u"1111"_s));                                    // Sam
    CHECK(pos.nextShift() == u"today 4 PM - 10 PM"_s);
    CHECK_FALSE(pos.clockIn());                                               // 3 PM: too early
    clock = todayAt(15, 50);                                                  // 10 minutes before
    REQUIRE(pos.clockIn());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"2222"_s));                                     // Casey: no shift
    CHECK_FALSE(pos.clockIn());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));                                     // managers always can
    REQUIRE(pos.clockIn());
    REQUIRE(pos.clockInEmployee(u"casey"_s));                                 // and can clock others in
    CHECK_FALSE(pos.clockInEmployee(u"casey"_s));
    const qint64 rileyShift = pos.shared()->shifts.back().id;
    REQUIRE(pos.removeShift(rileyShift));
    CHECK_FALSE(pos.removeShift(rileyShift));

    // Saved as it is.
    const core::Shift &s = pos.shared()->shifts.front();
    CHECK(app::shiftFromJson(app::toJson(s)) == s);
}

TEST_CASE("Tip pooling: tip-outs to bussers and bartenders, split by hours", "[staff][tips]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(17);
    pos.shared()->setClock([&] { return clock; });
    pos.shared()->settings.tipOuts = {{"busser", 1500, "tips"}, {"bartender", 200, "sales"}};
    // Riley (busser) and Jo (bartender) work; another busser works half as long.
    REQUIRE(pos.clockInEmployee(u"riley"_s) == false);                       // nobody logged in
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.clockInEmployee(u"riley"_s));
    REQUIRE(pos.clockInEmployee(u"jo"_s));
    pos.logout();

    // Sam sells $100 with a $20 card tip.
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    for (int i = 0; i < 8; ++i)
        pos.addItem(u"cobb"_s);                                              // 8 x $12.50 = $100 subtotal
    REQUIRE(pos.tender(u"credit"_s));
    pos.entryKey(u"2000"_s);
    REQUIRE(pos.addTip(0));
    REQUIRE(pos.closeCheck());
    clock += 4 * 3'600'000;

    // Sam: $20 - 15% ($3.00) to bussers - 2% of $100 sales ($2.00) to the bar.
    QStringList sam = tipRow(pos, u"Sam"_s);
    REQUIRE(sam.size() == 7);
    CHECK(sam[1] == u"$20.00"_s);
    CHECK(sam[3] == u"$5.00"_s);
    CHECK(sam[6] == u"$15.00"_s);
    CHECK(pos.tipsOwed() == u"$15.00"_s);
    CHECK(tipRow(pos, u"Riley"_s)[4] == u"$3.00"_s);
    CHECK(tipRow(pos, u"Jo"_s)[4] == u"$2.00"_s);

    // Sam cashes out what's left; the busser cashes out the pool share.
    REQUIRE(pos.cashOutTips());
    CHECK(tipRow(pos, u"Sam"_s)[6] == u"$0.00"_s);
    pos.logout();

    // A second busser for half the hours: the pool splits 2:1.
    core::Employee second = *pos.shared()->employee("riley");
    second.id = "kai";
    second.name = "Kai";
    pos.shared()->employees.push_back(second);
    pos.shared()->punches.push_back({999, "kai", clock - 2 * 3'600'000, 0, {}});
    CHECK(tipRow(pos, u"Riley"_s)[4] == u"$2.00"_s);
    CHECK(tipRow(pos, u"Kai"_s)[4] == u"$1.00"_s);

    // Without tip-out rules, servers keep their tips.
    pos.shared()->settings.tipOuts.clear();
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.tipsOwed() == u"$5.00"_s);                                     // $20 earned, $15 paid
}

TEST_CASE("Manager approval: a manager's PIN lets one void through, on the spot", "[staff][approval]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));                          // Sam: may not void sent items
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"cobb"_s);
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());

    // Through invoke (as the screens do): it waits for a manager.
    QVariant result;
    pos.invoke(u"voidItem"_s, {}, [&](const QVariant &r) { result = r; });
    CHECK_FALSE(result.toBool());
    QVariantMap approval = pos.approvalInfo();
    REQUIRE(approval[u"needed"_s].toBool());
    CHECK(approval[u"who"_s].toString().startsWith(u"Sam"_s));
    CHECK_FALSE(pos.approve(u"2222"_s));                            // a cashier can't approve it
    CHECK(pos.approvalInfo()[u"needed"_s].toBool());
    REQUIRE(pos.approve(u"1234"_s));                                // Morgan can
    CHECK_FALSE(pos.approvalInfo()[u"needed"_s].toBool());
    CHECK(pos.lines().last().toMap()[u"voided"_s].toBool());        // the coffee is voided
    const QVariantList history = pos.checkHistory();
    REQUIRE_FALSE(history.isEmpty());
    bool approved = false;
    for (const QVariant &h : history)
        approved = approved || h.toMap()[u"what"_s].toString().contains(u"approved by Morgan"_s);
    CHECK(approved);

    // Once only: the next void asks again; Cancel puts it away.
    pos.selectLine(pos.lines().first().toMap()[u"id"_s].toLongLong());
    pos.invoke(u"voidItem"_s, {}, {});
    CHECK(pos.approvalInfo()[u"needed"_s].toBool());
    REQUIRE(pos.cancelApproval());
    CHECK_FALSE(pos.approvalInfo()[u"needed"_s].toBool());
    CHECK_FALSE(pos.lines().first().toMap()[u"voided"_s].toBool());
    CHECK_FALSE(pos.approve(u"1234"_s));                            // nothing waiting
}

TEST_CASE("Practice mode: checks that never reach the kitchen, sales, stock or cash", "[staff][training]")
{
    PosService pos(test::seedPosData(), nullptr);
    // Riley is in training.
    REQUIRE(pos.loginWithPin(u"1234"_s));
    int sam = -1;
    for (int i = 0; i < int(pos.shared()->employees.size()); ++i)
        if (pos.shared()->employees[i].id == "sam") sam = i;
    QVariantMap r = pos.adminRecords(u"employees"_s)[sam].toMap();
    r[u"training"_s] = true;
    REQUIRE(pos.adminSave(u"employees"_s, sam, r));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    const QString drawerBefore = pos.drawerInfo()[u"expected"_s].toString();
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.training());
    const double buns = pos.shared()->ingredient("bun")->onHand;
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    CHECK(pos.checkInfo()[u"label"_s] == u"T1 (practice)"_s);
    CHECK_FALSE(pos.tableStatus(u"T1"_s)[u"open"_s].toBool());      // the real table stays free
    pos.addItem(u"classic-burger"_s);
    REQUIRE(pos.sendOrder());
    CHECK(pos.kitchenTickets().isEmpty());
    CHECK(pos.expoTickets().isEmpty());
    CHECK(pos.shared()->ingredient("bun")->onHand == buns);
    CHECK_FALSE(pos.tender(u"house"_s));                            // no real accounts
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(pos.shared()->closedToday.empty());                       // not a sale
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == drawerBefore);
    pos.logout();

    // A manager can switch their own screen to practice; End of Day clears
    // practice checks left open.
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK_FALSE(pos.training());
    REQUIRE(pos.setTraining(true));
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"coffee"_s);
    pos.releaseCheck();
    REQUIRE(pos.setTraining(false));
    REQUIRE(pos.countDrawer() == false);                            // (needs a count)
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.countDrawer());
    REQUIRE(pos.endOfDay());
    CHECK(pos.shared()->open.empty());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"2222"_s));
    CHECK_FALSE(pos.setTraining(true));                             // managers only
}

TEST_CASE("Messages between screens: sent with or without a login, the last hour's", "[staff][messages]")
{
    app::PosShared shared(test::seedPosData(), nullptr);
    qint64 clock = todayAt(12);
    shared.setClock([&] { return clock; });
    PosService kitchen(&shared, u"Kitchen"_s);
    PosService floor(&shared, u"Front"_s);
    CHECK_FALSE(kitchen.sendMessage(u"floor"_s, u"   "_s));
    REQUIRE(kitchen.sendMessage(u"floor"_s, u"86 salmon"_s));      // a kitchen screen, no login
    REQUIRE(floor.loginWithPin(u"1111"_s));
    REQUIRE(floor.sendMessage(u"kitchen"_s, u"Allergy: please check T4"_s));
    REQUIRE(floor.sendMessage(u"Morgan (Manager)"_s, u"Manager please"_s));
    const QVariantList all = floor.messages();
    REQUIRE(all.size() == 3);
    CHECK(all.first().toMap()[u"text"_s] == u"Manager please"_s);  // newest first
    CHECK(all.last().toMap()[u"from"_s] == u"Kitchen"_s);
    CHECK(all.first().toMap()[u"from"_s] == u"Sam"_s);
    clock += 61 * 60'000;
    CHECK(floor.messages().isEmpty());                              // an hour later
}

TEST_CASE("Table turns: minutes from seated to paid, by party size and table", "[staff][turns]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(12);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    const auto seat = [&](const QString &table, int guests, int minutes) {
        REQUIRE(pos.selectTable(table) == PosService::TableNeedsGuests);
        pos.entryKey(QString::number(guests));
        REQUIRE(pos.startCheck(core::CheckType::DineIn));
        pos.addItem(u"cobb"_s);
        clock += minutes * 60'000;
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    };
    seat(u"T1"_s, 2, 40);
    seat(u"T1"_s, 2, 60);
    seat(u"T5"_s, 6, 90);
    const QVariantMap r = pos.report(u"turns"_s);
    QStringList t1, small, all;
    for (const QVariant &v : r[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (cells.value(0) == u"T1"_s) t1 = cells;
        if (cells.value(0) == u"1 - 2 guests"_s) small = cells;
        if (cells.value(0) == u"All tables"_s) all = cells;
    }
    REQUIRE(t1.size() == 5);
    CHECK(t1[1] == u"2"_s);
    CHECK(t1[2] == u"50"_s);
    CHECK(small[2] == u"50"_s);
    CHECK(all[1] == u"3"_s);
    CHECK(all[2] == u"63"_s);                                       // (40 + 60 + 90) / 3

    // The floor plan knows when each table was seated.
    REQUIRE(pos.selectTable(u"T3"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    const QVariantMap t3 = pos.tableStatus(u"T3"_s);
    CHECK(t3[u"since"_s].toLongLong() == clock);
    CHECK(t3[u"longAfter"_s].toInt() == 90);
}
