// Demo data: two months of service (and the same two months last year, for
// "vs Last Year"), played through the real POS with a pretend clock - staff
// clock in, take orders with their choices, send, the kitchen bumps, guests
// pay by card (with tips), cash, gift card or house account, servers check
// out, End of Day. Then customers, gift cards, a schedule and tonight's
// waitlist. Everything the reports, customers and host stand show is real.

#include "app/pos_demo.hh"

#include "app/pos_json.hh"
#include "core/report.hh"

#include <QDateTime>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr std::int64_t kMinute = 60'000;
// The demo staff: who takes tables, and everyone's PIN and id.
const char *const kServers[] = {"1111", "2222", "4444", "1234"};
const std::pair<const char *, const char *> kStaff[] = {
    {"1111", "sam"}, {"2222", "casey"}, {"4444", "jo"}, {"1234", "morgan"}, {"3333", "riley"}};

struct Demo {
    PosService &pos;
    std::int64_t clock = 0;
    QRandomGenerator rng{20260930};
    std::vector<std::string> sellable;     // menu items guests order
    std::map<std::string, double> stock;   // starting stock, restocked each morning
    std::vector<std::string> cards;        // gift card numbers sold
    int checks = 0;

    int pick(int n) { return n > 0 ? int(rng.bounded(n)) : 0; }
    bool chance(int percent) { return pick(100) < percent; }

    std::int64_t at(QDate day, int hour, int minute = 0) const
    {
        return QDateTime(day, QTime(hour, minute)).toMSecsSinceEpoch();
    }

    void as(const char *pin)
    {
        pos.logout();
        pos.loginWithPin(QString::fromLatin1(pin));
    }

    void amount(std::int64_t cents)
    {
        pos.entryKey(u"clear"_s);
        pos.entryKey(QString::number(std::max<std::int64_t>(cents, 0)));
    }

    // An item, its required choices, and a few optional ones.
    void order(const std::string &id)
    {
        if (!pos.addItem(QString::fromStdString(id)))
            return;
        for (int guard = 0; guard < 8; ++guard) {
            const QVariantMap info = pos.choosingInfo();
            if (!info.value(u"active"_s).toBool())
                break;
            bool chose = false;
            for (const QVariant &gv : info.value(u"groups"_s).toList()) {
                const QVariantMap g = gv.toMap();
                const int options = int(g.value(u"options"_s).toList().size());
                if (!g.value(u"done"_s).toBool() || (g.value(u"chosen"_s).toInt() == 0 && chance(30))) {
                    pos.chooseOption(g.value(u"id"_s).toString(), pick(options));
                    chose = true;
                }
            }
            if (pos.finishChoosing() || !chose)
                break;
        }
        // Burgers: how they're cooked and a side (modifier items).
        if (id.find("burger") != std::string::npos || id == "mushroom-swiss") {
            static const char *temps[] = {"medium-rare", "medium", "medium-well", "well-done"};
            static const char *sides[] = {"fries", "fries", "side-salad", "onion-rings", "sweet-potato-fries"};
            if (id != "kids-burger")
                pos.addItem(QString::fromLatin1(temps[pick(4)]));
            if (id != "kids-burger")
                pos.addItem(QString::fromLatin1(sides[pick(5)]));
        }
    }

    // One table or takeout, from sitting down to paying.
    void serve(QDate day, const char *serverPin, std::int64_t start, bool breakfast)
    {
        clock = start;
        as(serverPin);
        const bool takeout = chance(25);
        const int guests = takeout ? 1 : 1 + pick(5);
        if (takeout) {
            pos.startCheck(chance(70) ? CheckType::Takeout : CheckType::Delivery);
            const auto &customers = pos.shared()->customers;
            if (!customers.empty() && chance(60))
                pos.useCustomer(QString::fromStdString(customers[pick(int(customers.size()))].id));
        } else {
            static const char *tables[] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "Bar 1", "Bar 2", "Bar 3"};
            pos.selectTable(QString::fromLatin1(tables[pick(10)]));
            amount(guests);
            if (!pos.startCheck(CheckType::DineIn))
                return;
        }
        if (chance(3))
            pos.toggleFlag(u"rush"_s);
        // Breakfast items in the morning, the rest later.
        const int items = std::max(1, guests + pick(3) - 1);
        for (int i = 0; i < items; ++i) {
            std::string id;
            for (int tries = 0; tries < 6; ++tries) {
                id = sellable[pick(int(sellable.size()))];
                const MenuItem *m = nullptr;
                for (const MenuItem &x : pos.shared()->menu)
                    if (x.id == id) m = &x;
                if (m && (m->family == "breakfast") == breakfast)
                    break;
            }
            order(id);
            if (chance(50))   // something to drink
                order(chance(60) ? "soda" : chance(50) ? "coffee" : "draft-beer");
        }
        clock += 2 * kMinute;
        pos.sendOrder();
        const std::int64_t checkId = pos.checkInfo().value(u"id"_s).toLongLong();
        // The kitchen makes it.
        clock += (6 + pick(14)) * kMinute;
        for (const QVariant &tv : pos.kitchenTickets()) {
            const QVariantMap t = tv.toMap();
            if (t.value(u"checkId"_s).toLongLong() == checkId)
                pos.bumpTicket(checkId, t.value(u"sentAt"_s).toLongLong());
        }
        clock += (takeout ? 3 : 25 + pick(35)) * kMinute;
        if (chance(3))
            pos.tender(u"discount"_s);
        pay(takeout);
        if (!pos.closeCheck()) {   // whatever is left, in cash
            pos.tender(u"cash"_s);
            pos.closeCheck();
        }
        ++checks;
    }

    void pay(bool takeout)
    {
        const std::int64_t due = pos.totals().value(u"balanceCents"_s).toLongLong();
        const int how = pick(100);
        const QVariantMap check = pos.checkInfo();
        if (how < 6 && !cards.empty()) {   // a gift card
            pos.lookupGiftCard(QString::fromStdString(cards[pick(int(cards.size()))]));
            pos.payWithGiftCard();
        } else if (how < 10) {             // the house account, when they have one
            std::vector<const CustomerRecord *> open;
            for (const CustomerRecord &r : pos.shared()->customers)
                if (r.houseAccount && r.accountBalance + Money::fromCents(due) <= r.accountLimit) open.push_back(&r);
            const CustomerRecord *c = open.empty() ? nullptr : open[pick(int(open.size()))];
            if (c) {
                pos.useCustomer(QString::fromStdString(c->id));
                pos.tender(u"house"_s);
            }
        }
        if (pos.totals().value(u"balanceCents"_s).toLongLong() <= 0)
            return;
        if (how < 70) {                    // card, usually with a tip
            pos.tender(u"credit"_s);
            const int tipPercent = takeout ? (chance(50) ? 0 : 10) : 15 + pick(9);
            if (tipPercent > 0) {
                amount(due * tipPercent / 100);
                pos.addTip(0);
            }
        } else {                           // cash, rounded up
            const std::int64_t given = (due + 499) / 500 * 500;
            pos.tender(u"cash"_s, given);
        }
        Q_UNUSED(check)
    }

    // Everyone out: tips, banks counted, clock out, End of Day.
    void closeDay()
    {
        for (const auto &[pin, id] : kStaff) {
            as(pin);
            pos.cashOutTips();
            const Employee *me = pos.shared()->employee(id);
            for (const DrawerSession &d : pos.shared()->drawers) {
                if (me && d.open() && d.employeeId == me->id) {
                    const Money expected = expectedCash(d, pos.shared()->closedToday, pos.shared()->settings.tax);
                    amount(expected.cents() + (chance(85) ? 0 : (pick(2) ? 1 : -1) * (100 + pick(400))));
                    pos.countDrawer();
                }
            }
            pos.clockOut();
        }
        as("1234");
        for (const DrawerSession &d : pos.shared()->drawers) {   // anything left open
            if (d.open()) {
                amount(expectedCash(d, pos.shared()->closedToday, pos.shared()->settings.tax).cents());
                pos.countDrawerById(d.id);
            }
        }
        pos.endOfDay();
    }

    // Stores with a drawer on the terminal: the manager opens it with $200.
    void openDrawer()
    {
        as("1234");
        if (pos.shared()->settings.cashMode != CashMode::ServerBank && !pos.drawerInfo().value(u"open"_s).toBool()) {
            amount(20000);
            pos.openDrawerSession();
        }
    }

    void restock()
    {
        as("1234");
        const QVariantList records = pos.adminRecords(u"inventory"_s);
        for (int i = 0; i < int(records.size()); ++i) {
            QVariantMap r = records[i].toMap();
            const std::string id = r.value(u"id"_s).toString().toStdString();
            if (stock.contains(id) && r.value(u"onHand"_s).toDouble() < stock[id]) {
                r[u"onHand"_s] = stock[id];
                pos.adminSave(u"inventory"_s, i, r);
            }
        }
    }

    void playDay(QDate day, double busy)
    {
        clock = at(day, 9, 30);
        restock();
        // Staff in; Sam takes a break mid-afternoon.
        for (const char *pin : {"1234", "1111", "2222", "4444", "3333"}) {
            as(pin);
            pos.clockIn();
        }
        openDrawer();
        // Mondays the house accounts pay what they owe (by card).
        if (day.dayOfWeek() == 1) {
            as("1234");
            for (const CustomerRecord &c : std::vector<CustomerRecord>(pos.shared()->customers)) {
                if (c.houseAccount && c.accountBalance.cents() > 0) {
                    pos.selectCustomer(QString::fromStdString(c.id));
                    pos.payOnAccount(u"card"_s);
                }
            }
        }
        const bool weekend = day.dayOfWeek() >= 6;
        const int tables = int(std::lround((weekend ? 34 : 24) * busy)) + pick(8);
        for (int i = 0; i < tables; ++i) {
            // Breakfast and lunch rushes, a dinner rush.
            const int slot = pick(100);
            const int hour = slot < 18 ? 10 + pick(2) : slot < 55 ? 12 + pick(2) : slot < 65 ? 14 + pick(3) : 17 + pick(4);
            serve(day, kServers[pick(4)], at(day, hour, pick(60)), hour < 11);
            if (i == tables / 2) {
                as("1111");
                clock = at(day, 15);
                pos.toggleBreak();
                clock = at(day, 15, 30);
                pos.toggleBreak();
            }
        }
        clock = at(day, 22, 30);
        closeDay();
    }
};

} // namespace

QString fillDemoData(PosService &pos, std::int64_t realNow)
{
    PosShared *s = pos.shared();
    if (!s->closedToday.empty() || !s->pastDays.empty() || !s->open.empty())
        return QObject::tr("This store already has sales. Run --factory-reset first (it takes a backup).");
    Demo d{pos};
    d.clock = realNow;
    s->setClock([&d] { return d.clock; });
    for (const MenuItem &m : s->menu) {
        if (!m.isModifier && m.available)
            d.sellable.push_back(m.id);
    }
    for (const Ingredient &g : s->ingredients)
        d.stock[g.id] = g.onHand;

    const QDate today = QDateTime::fromMSecsSinceEpoch(realNow).date();
    const QDate lastYear = today.addYears(-1).addDays(-60);
    const QDate thisYear = today.addDays(-60);

    // The store "opened" the morning of the first day.
    d.clock = d.at(lastYear, 9);
    s->day.openedAt = d.clock;
    if (s->sink)
        s->sink->saveDay(s->day, {});

    // Regulars, two with house accounts; tip-outs to the busser and the bar.
    d.as("1234");
    static const char *people[][3] = {
        {"Dana Lee", "555-010-1234", "no onions"}, {"Alex Kim", "555-777-0000", ""},
        {"Robin Park", "555-222-3333", "gate code 1942"}, {"Jordan Diaz", "555-414-2020", ""},
        {"Taylor Brooks", "555-303-9090", "peanut allergy"}, {"Morgan Reyes", "555-818-4545", ""},
        {"Chris Novak", "555-626-1111", ""}, {"Sky Patel", "555-505-7272", "extra napkins"},
        {"Jamie Cole", "555-121-3434", ""}, {"Pat Quinn", "555-999-0101", ""},
        {"Acme Office", "555-600-1000", "lunch orders, deliver to suite 4"},
        {"City Hall", "555-600-2000", "charge to the clerk's account"},
    };
    for (const auto &p : people) {
        pos.saveCustomer({{u"name"_s, QString::fromLatin1(p[0])}, {u"phone"_s, QString::fromLatin1(p[1])},
                          {u"note"_s, QString::fromLatin1(p[2])},
                          {u"address"_s, d.chance(50) ? u"%1 Main St"_s.arg(100 + d.pick(800)) : QString()}});
        if (QString::fromLatin1(p[0]).contains(u"Office"_s) || QString::fromLatin1(p[0]).contains(u"Hall"_s))
            pos.saveCustomer({{u"id"_s, QString::fromStdString(s->customers.back().id)},
                              {u"name"_s, QString::fromLatin1(p[0])}, {u"phone"_s, QString::fromLatin1(p[1])},
                              {u"note"_s, QString::fromLatin1(p[2])}, {u"houseAccount"_s, true},
                              {u"accountLimit"_s, 50000}});
    }
    pos.releaseCheck();
    s->settings.tipOuts = {{"busser", 1500, "tips"}, {"bartender", 200, "sales"}};
    if (s->sink)
        s->sink->saveSettings(s->settings);

    // Gift cards sold on the first days.
    d.clock = d.at(lastYear, 9, 15);
    d.as("1234");
    pos.entryKey(u"clear"_s);
    pos.entryKey(u"20000"_s);
    pos.openDrawerSession();
    for (int i = 0; i < 8; ++i) {
        const QString number = u"6000%1"_s.arg(100000 + i * 7919);
        if (pos.sellGiftCard(number, (2 + d.pick(4)) * 2500)) {
            pos.tender(u"credit"_s);
            pos.closeCheck();
            d.cards.push_back(number.toStdString());
        }
    }

    for (int i = 0; i < 60; ++i)
        d.playDay(lastYear.addDays(i), 0.8);
    for (int i = 0; i < 60; ++i)
        d.playDay(thisYear.addDays(i), 1.0);

    // Today so far.
    const QTime nowTime = QDateTime::fromMSecsSinceEpoch(realNow).time();
    d.clock = d.at(today, 9, 30);
    d.restock();
    for (const char *pin : {"1234", "1111", "2222", "4444", "3333"}) {
        d.as(pin);
        pos.clockIn();
    }
    d.openDrawer();
    for (int hour = 10; hour < std::min(22, nowTime.hour()); ++hour) {
        for (int k = 0; k < 2 + d.pick(3); ++k)
            d.serve(today, kServers[d.pick(3)], d.at(today, hour, d.pick(50)), hour < 11);
    }

    // The schedule for this week and next.
    d.clock = realNow;
    d.as("1234");
    const int back = (today.dayOfWeek() % 7 - s->settings.weekStartsOn + 7) % 7;
    const QDate week = today.addDays(-back);
    static const char *crew[][3] = {{"sam", "16:00", "22:00"}, {"casey", "10:00", "16:00"}, {"jo", "17:00", "23:30"},
                                    {"riley", "11:00", "19:00"}, {"morgan", "09:00", "17:00"}};
    for (int dd = 0; dd < 14; ++dd) {
        const QString day = week.addDays(dd).toString(u"yyyy-MM-dd"_s);
        for (const auto &c : crew) {
            if (d.chance(78))
                pos.addShift({{u"employeeId"_s, QString::fromLatin1(c[0])}, {u"start"_s, day + u' ' + QString::fromLatin1(c[1])},
                              {u"end"_s, day + u' ' + QString::fromLatin1(c[2])}});
        }
    }

    // Tonight's host stand: a short line and some bookings.
    static const char *waiting[][2] = {{"Nguyen", "4"}, {"Okafor", "2"}, {"Silva", "6"}};
    for (int i = 0; i < 3; ++i) {
        d.clock = realNow - (25 - i * 9) * kMinute;
        pos.addToWaitlist({{u"name"_s, QString::fromLatin1(waiting[i][0])}, {u"size"_s, QString::fromLatin1(waiting[i][1]).toInt()},
                           {u"phone"_s, u"555-%1"_s.arg(3000 + i * 111)}});
    }
    d.clock = realNow;
    static const char *booked[][3] = {{"Hughes", "2", "19:00"}, {"Martinez", "8", "19:30"}, {"Lee", "4", "20:00"}};
    for (int dd = 0; dd < 2; ++dd) {
        for (const auto &b : booked) {
            const QDate day = today.addDays(dd);
            const QDateTime when(day, QTime::fromString(QString::fromLatin1(b[2]), u"HH:mm"_s));
            if (when.toMSecsSinceEpoch() > realNow)
                pos.addReservation({{u"name"_s, QString::fromLatin1(b[0])}, {u"size"_s, QString::fromLatin1(b[1]).toInt()},
                                    {u"at"_s, day.toString(u"yyyy-MM-dd"_s) + u' ' + QString::fromLatin1(b[2])},
                                    {u"note"_s, dd == 0 && b[0][0] == 'M' ? u"birthday"_s : QString()}});
        }
    }
    pos.logout();
    s->setClock([] { return QDateTime::currentMSecsSinceEpoch(); });
    return QObject::tr("Added %1 checks over 120 days, %2 customers, %3 gift cards, a schedule and tonight's waitlist.")
        .arg(d.checks).arg(s->customers.size()).arg(d.cards.size());
}

} // namespace vt::app
