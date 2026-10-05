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

TEST_CASE("How items look in the kitchen: kitchen names, colors, hidden items and modifiers", "[kitchen][display]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // A dressing the kitchen calls RNCH; "No dressing" left off the ticket.
    int groupIndex = 0;
    for (int i = 0; i < int(pos.shared()->settings.modifierGroups.size()); ++i)
        if (pos.shared()->settings.modifierGroups[i].id == "dressing") groupIndex = i;
    QVariantMap g = pos.adminRecords(u"modifierGroups"_s)[groupIndex].toMap();
    CHECK(g[u"options"_s].toString().startsWith(u"Ranch | RNCH"_s));
    g[u"options"_s] = g[u"options"_s].toString() + u"\nNo dressing | -"_s;
    REQUIRE(pos.adminSave(u"modifierGroups"_s, groupIndex, g));
    CHECK(pos.shared()->settings.modifierGroups[groupIndex].options.back().kitchenHide);

    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"bacon-burger"_s);                 // kitchen name BCN BGR, orange
    REQUIRE(pos.chooseOption(u"temperature"_s, 1)); // Medium Rare: MR
    REQUIRE(pos.chooseOption(u"side"_s, 4));        // No Side: left off the ticket
    REQUIRE(pos.finishChoosing());
    pos.addItem(u"water"_s);                        // hidden item
    pos.addItem(u"house-salad"_s);
    const int last = int(pos.shared()->settings.modifierGroups[groupIndex].options.size()) - 1;
    REQUIRE(pos.chooseOption(u"dressing"_s, last));  // No dressing
    REQUIRE(pos.finishChoosing());
    // The check and receipt keep the real names.
    CHECK(pos.lines().first().toMap()[u"name"_s] == u"Bacon Burger"_s);
    REQUIRE(pos.sendOrder());

    const QVariantList tickets = pos.kitchenTickets();
    REQUIRE(tickets.size() == 1);
    const QVariantList lines = tickets.first().toMap()[u"lines"_s].toList();
    REQUIRE(lines.size() == 2);                       // no water
    CHECK(lines[0].toMap()[u"name"_s] == u"BCN BGR"_s);
    CHECK(lines[0].toMap()[u"color"_s] == u"orange"_s);
    CHECK(lines[0].toMap()[u"modifiers"_s].toStringList() == QStringList{u"MR"_s});   // no "No Side"
    CHECK(lines[1].toMap()[u"modifiers"_s].toStringList().isEmpty());              // no "No dressing"

    const core::Check &c = pos.shared()->open.begin()->second;
    print::TicketContext ctx{pos.shared()->settings, [](std::int64_t) { return std::string("1/1"); },
                             [](std::int64_t) { return std::string("12:00"); }, 0};
    const std::string ticket = print::renderText(print::kitchenTicket(c, c.lines, "Kitchen", false, ctx), 42);
    CHECK(ticket.find("BCN BGR") != std::string::npos);
    CHECK(ticket.find("Water") == std::string::npos);
    CHECK(ticket.find("No dressing") == std::string::npos);
    CHECK(app::checkFromJson(app::toJson(c))->lines[0].kitchenName == "BCN BGR");

    // Set from Manager -> Menu.
    int water = 0;
    for (int i = 0; i < int(pos.shared()->menu.size()); ++i)
        if (pos.shared()->menu[i].id == "water") water = i;
    QVariantMap w = pos.adminRecords(u"menu"_s)[water].toMap();
    CHECK(w[u"kitchenHide"_s].toBool());
    w[u"kitchenHide"_s] = false;
    w[u"kitchenName"_s] = u"H2O"_s;
    REQUIRE(pos.adminSave(u"menu"_s, water, w));
    CHECK(pos.shared()->menu[water].kitchenName == "H2O");
}

TEST_CASE("Expediter: every station's ticket, ready when all made, run out, recall", "[kitchen][expo]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.selectTable(u"T2"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"cobb"_s);                         // kitchen
    pos.addItem(u"draft-beer"_s);                   // bar
    pos.addItem(u"water"_s);                        // nothing to make
    REQUIRE(pos.sendOrder());
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();

    QVariantMap t = pos.expoTickets().first().toMap();
    CHECK_FALSE(t[u"ready"_s].toBool());
    CHECK(t[u"lines"_s].toList().size() == 2);
    CHECK(t[u"waitingOn"_s].toStringList() == QStringList{u"Cold Line"_s, u"Bar"_s});   // in order on the ticket
    const qint64 sentAt = t[u"sentAt"_s].toLongLong();

    REQUIRE(pos.bumpTicket(id, sentAt, u"kitchen"_s));   // the kitchen is done
    t = pos.expoTickets().first().toMap();
    CHECK(t[u"waitingOn"_s].toStringList() == QStringList{u"Bar"_s});
    REQUIRE(pos.bumpTicket(id, sentAt, u"bar"_s));
    CHECK(pos.expoTickets().first().toMap()[u"ready"_s].toBool());
    CHECK(pos.kitchenTickets().isEmpty());               // the stations are clear

    REQUIRE(pos.expoBump(id, sentAt));
    CHECK(pos.expoTickets().isEmpty());
    CHECK_FALSE(pos.expoBump(id, sentAt));
    REQUIRE(pos.expoRecall());
    CHECK(pos.expoTickets().size() == 1);

    // Sent out before a station bumped it: it counts as made.
    pos.addItem(u"caesar"_s);
    REQUIRE(pos.sendOrder());
    const QVariantList all = pos.expoTickets();
    const QVariantMap second = all.last().toMap();
    REQUIRE(pos.expoBump(id, second[u"sentAt"_s].toLongLong()));
    CHECK(pos.kitchenTickets().isEmpty());
}

TEST_CASE("Stations: each screen its station's lines, a combo's fries at the fryer, expo waits for all",
          "[kitchen][stations]")
{
    app::PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.selectTable(u"T1"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"burger-combo"_s);
    REQUIRE(pos.chooseOption(u"temperature"_s, 2));
    REQUIRE(pos.chooseOption(u"side"_s, 0));       // Fries: the fryer's
    REQUIRE(pos.chooseOption(u"combo-drink"_s, 0)); // Soda: the bar's, not the kitchen's
    REQUIRE(pos.finishChoosing());
    pos.addItem(u"cobb"_s);                         // the cold line's
    REQUIRE(pos.sendOrder());
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();

    const auto at = [&](const QString &station) {
        QStringList out;
        for (const QVariant &t : pos.kitchenTickets())
            for (const QVariant &v : t.toMap()[u"lines"_s].toList()) {
                const QVariantMap l = v.toMap();
                if (l[u"station"_s] == station || l[u"printer"_s] == station)
                    out << l[u"name"_s].toString();
            }
        return out;
    };
    CHECK(at(u"grill"_s) == QStringList{u"COMBO BGR"_s});
    CHECK(at(u"fryer"_s) == QStringList{u"Fries"_s});
    CHECK(at(u"cold"_s) == QStringList{u"Cobb"_s});
    CHECK(at(u"kitchen"_s).size() == 3);             // the whole kitchen printer's screen

    const QVariantMap burger = pos.kitchenTickets().first().toMap()[u"lines"_s].toList().first().toMap();
    CHECK_FALSE(burger[u"modifiers"_s].toStringList().contains(u"Fries"_s));   // the fryer makes those
    const qint64 sentAt = pos.kitchenTickets().first().toMap()[u"sentAt"_s].toLongLong();

    REQUIRE(pos.bumpTicket(id, sentAt, u"fryer"_s));
    CHECK(at(u"fryer"_s).isEmpty());
    CHECK(at(u"grill"_s) == QStringList{u"COMBO BGR"_s});
    QVariantMap expo = pos.expoTickets().first().toMap();
    CHECK(expo[u"waitingOn"_s].toStringList() == QStringList{u"Grill"_s, u"Cold Line"_s});

    REQUIRE(pos.recallTicket());                     // the fries weren't done after all
    CHECK(at(u"fryer"_s) == QStringList{u"Fries"_s});
    CHECK(pos.expoTickets().first().toMap()[u"waitingOn"_s].toStringList().contains(u"Fryer"_s));

    REQUIRE(pos.bumpTicket(id, sentAt, u"grill"_s));
    CHECK(at(u"fryer"_s) == QStringList{u"Fries"_s}); // still the fryer's
    REQUIRE(pos.bumpTicket(id, sentAt, u"fryer"_s));
    REQUIRE(pos.bumpTicket(id, sentAt, u"cold"_s));
    CHECK(pos.expoTickets().first().toMap()[u"ready"_s].toBool());

    // Saved with the check.
    const auto back = app::checkFromJson(app::toJson(pos.shared()->open.at(id)));
    CHECK(back->lines.front().station == "grill");
    CHECK(back->lines.front().modifiers[1].station == "fryer");
    CHECK(back->lines.front().modifiers[1].made);
}

TEST_CASE("Stations: a screen keeps the station it was set to", "[kitchen][stations]")
{
    app::PosService pos(test::seedPosData(), nullptr);
    CHECK(pos.kitchenStation().isEmpty());
    CHECK(pos.kitchenStations().size() >= 3);
    REQUIRE(pos.setKitchenStation(u"fryer"_s));        // nobody logged in: kitchen screens
    CHECK(pos.kitchenStation() == u"fryer"_s);
    CHECK(app::settingsFromJson(app::toJson(pos.shared()->settings)).terminals.back().station == "fryer");
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap s = pos.adminRecords(u"store"_s).first().toMap();
    CHECK(s[u"kitchenStations"_s] == u"Grill\nFryer\nCold Line"_s);
    s[u"kitchenStations"_s] = u"Grill\nFryer\nCold Line\nPizza Oven"_s;
    REQUIRE(pos.adminSave(u"store"_s, 0, s));
    CHECK(pos.shared()->settings.stations.back().id == "pizza-oven");
    CHECK(pos.shared()->settings.stations.front().id == "grill");
}
