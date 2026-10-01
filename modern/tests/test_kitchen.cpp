#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Kitchen display upgrades: rush and VIP, timers, ticket times.

namespace {

qint64 todayAt(int hour, int minute = 0)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute)).toMSecsSinceEpoch();
}

QStringList cellsOf(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return cells;
    }
    return {};
}

} // namespace

TEST_CASE("Rush and VIP: flagged on the check, first on the kitchen display, on the ticket", "[kitchen]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(12);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.sendOrder());
    pos.releaseCheck();
    clock += 60'000;
    REQUIRE(pos.selectTable(u"T2"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"caesar"_s);
    REQUIRE(pos.sendOrder());
    CHECK(pos.kitchenTickets().first().toMap()[u"label"_s] == u"T1"_s);   // oldest first
    REQUIRE(pos.toggleFlag(u"rush"_s));
    REQUIRE(pos.toggleFlag(u"vip"_s));
    CHECK(pos.checkInfo()[u"rush"_s].toBool());
    CHECK_FALSE(pos.toggleFlag(u"spicy"_s));
    QVariantMap first = pos.kitchenTickets().first().toMap();
    CHECK(first[u"label"_s] == u"T2"_s);                                 // the rush jumps the line
    CHECK(first[u"rush"_s].toBool());
    CHECK(first[u"vip"_s].toBool());
    CHECK(first[u"warnMinutes"_s] == 8);
    CHECK(first[u"lateMinutes"_s] == 15);

    const core::Check &c = pos.shared()->open.rbegin()->second;
    print::TicketContext ctx{pos.shared()->settings, [](std::int64_t) { return std::string("1/1"); },
                             [](std::int64_t) { return std::string("12:00"); }, 0};
    const std::string ticket = print::renderText(print::kitchenTicket(c, c.lines, "Kitchen", false, ctx), 42);
    CHECK(ticket.find("RUSH") != std::string::npos);
    CHECK(ticket.find("VIP") != std::string::npos);
    CHECK(app::checkFromJson(app::toJson(c))->rush);

    REQUIRE(pos.toggleFlag(u"rush"_s));                                  // off again
    CHECK(pos.kitchenTickets().first().toMap()[u"label"_s] == u"T1"_s);
}

TEST_CASE("Kitchen times: average, longest and late tickets by station", "[kitchen][reports]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(12);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(cellsOf(pos.report(u"kitchen"_s), u"No orders have gone to the kitchen yet."_s).size() == 1);

    const auto order = [&](const QString &table, const char *item) {
        REQUIRE(pos.selectTable(table) == PosService::TableNeedsGuests);
        REQUIRE(pos.startCheck(core::CheckType::DineIn));
        pos.addItem(QString::fromLatin1(item));
        REQUIRE(pos.sendOrder());
        const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
        pos.releaseCheck();
        return id;
    };
    const auto bump = [&](qint64 checkId) {
        for (const QVariant &v : pos.kitchenTickets()) {
            const QVariantMap t = v.toMap();
            if (t[u"checkId"_s].toLongLong() == checkId)
                REQUIRE(pos.bumpTicket(checkId, t[u"sentAt"_s].toLongLong(), {}));
        }
    };
    const qint64 a = order(u"T1"_s, "cobb");
    const qint64 b = order(u"T2"_s, "caesar");
    const qint64 c = order(u"T3"_s, "cobb");
    clock += 6 * 60'000;
    bump(a);                                       // 6:00
    clock += 14 * 60'000;
    bump(b);                                       // 20:00 - late (over 15)
    // c is still being made.

    const QVariantMap report = pos.report(u"kitchen"_s);
    const QStringList kitchen = cellsOf(report, u"Kitchen"_s);
    REQUIRE(kitchen.size() == 5);
    CHECK(kitchen[1] == u"2"_s);
    CHECK(kitchen[2] == u"13:00"_s);               // (6 + 20) / 2
    CHECK(kitchen[3] == u"20:00"_s);
    CHECK(kitchen[4] == u"1"_s);
    CHECK(cellsOf(report, u"1 ticket is still being made."_s).size() == 1);
    CHECK(cellsOf(report, QString::fromStdString("T2 #" + std::to_string(b) + " (kitchen)")).size() == 5);
    Q_UNUSED(c)
}
