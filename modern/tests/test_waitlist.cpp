#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QDateTime>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The host stand: waitlist and reservations.

namespace {

qint64 todayAt(int hour, int minute = 0)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute)).toMSecsSinceEpoch();
}

QVariantList waiting(const PosService &pos) { return pos.waitlistInfo()[u"waiting"_s].toList(); }
QVariantList booked(const PosService &pos) { return pos.waitlistInfo()[u"booked"_s].toList(); }

QString idOf(const PosService &pos, const QString &name)
{
    for (const QVariant &v : pos.staff()) {
        if (v.toMap()[u"name"_s].toString().startsWith(name))
            return v.toMap()[u"id"_s].toString();
    }
    return {};
}

} // namespace

TEST_CASE("Waitlist: quotes, the table-ready text, seating for a server", "[waitlist]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(18);
    pos.shared()->setClock([&] { return clock; });
    QStringList texts;
    pos.shared()->sendText = [&](const QString &phone, const QString &message) { texts << phone + u": "_s + message; };
    REQUIRE(pos.loginWithPin(u"1234"_s));

    CHECK(pos.addToWaitlist({{u"size"_s, 2}}) == 0);              // a name, please
    const qint64 dana = pos.addToWaitlist({{u"name"_s, u"Dana"_s}, {u"phone"_s, u"555-0101"_s}, {u"size"_s, 4}});
    const qint64 alex = pos.addToWaitlist({{u"name"_s, u"Alex"_s}, {u"size"_s, 2}});
    const qint64 sam = pos.addToWaitlist({{u"name"_s, u"Sam"_s}, {u"size"_s, 3}, {u"quote"_s, 45}});
    REQUIRE(dana > 0);
    QVariantList line = waiting(pos);
    REQUIRE(line.size() == 3);
    CHECK(line[0].toMap()[u"quoted"_s] == 10);                    // 10 minutes a party ahead
    CHECK(line[1].toMap()[u"quoted"_s] == 20);
    CHECK(line[2].toMap()[u"quoted"_s] == 45);                    // the host's own quote
    CHECK(pos.waitlistInfo()[u"nextQuote"_s].toInt() == 40);

    clock += 25 * 60'000;
    line = waiting(pos);
    CHECK(line[0].toMap()[u"waited"_s] == 25);
    CHECK(line[0].toMap()[u"late"_s].toBool());                   // told 10, waited 25

    REQUIRE(pos.notifyParty(dana));
    REQUIRE(texts.size() == 1);
    CHECK(texts[0].startsWith(u"555-0101: Hi Dana, your table at"_s));
    CHECK(waiting(pos)[0].toMap()[u"status"_s] == u"notified"_s);
    REQUIRE(pos.notifyParty(alex));                               // no phone: no text
    CHECK(texts.size() == 1);

    // Seat Dana at T5 for Sam (the server): the table's check is Sam's.
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));             // T1 is busy
    pos.releaseCheck();
    CHECK_FALSE(pos.seatParty(dana, u"T1"_s));
    CHECK_FALSE(pos.seatParty(dana, u""_s));
    REQUIRE(pos.seatParty(dana, u"T5"_s, idOf(pos, u"Sam"_s)));
    CHECK_FALSE(pos.hasCheck());                                  // the host doesn't keep it
    const QVariantMap t5 = pos.tableStatus(u"T5"_s);
    CHECK(t5[u"open"_s].toBool());
    const core::Check *check = nullptr;
    for (const auto &[id, c] : pos.shared()->open)
        if (c.label == "T5") check = &c;
    REQUIRE(check);
    CHECK(check->serverName == "Sam");
    CHECK(check->guests == 4);
    CHECK(check->customer.name == "Dana");
    CHECK_FALSE(pos.seatParty(dana, u"T6"_s));                    // already seated

    REQUIRE(pos.partyGone(sam));
    CHECK(waiting(pos).size() == 1);
    CHECK(pos.waitlistInfo()[u"seatedToday"_s].toInt() == 1);
    CHECK(pos.waitlistInfo()[u"averageWait"_s].toInt() == 25);
}

TEST_CASE("Reservations: booked, checked in, seated; no-shows", "[waitlist][reservations]")
{
    PosService pos(test::seedPosData(), nullptr);
    qint64 clock = todayAt(17);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));

    CHECK(pos.addReservation({{u"name"_s, u"Lee"_s}, {u"size"_s, 6}}) == 0);           // when?
    CHECK(pos.addReservation({{u"name"_s, u"Lee"_s}, {u"at"_s, todayAt(9)}}) == 0);   // in the past
    const QString tomorrow = QDate::currentDate().addDays(1).toString(u"yyyy-MM-dd"_s) + u" 19:30"_s;
    const qint64 later = pos.addReservation({{u"name"_s, u"Kim"_s}, {u"size"_s, 2}, {u"at"_s, tomorrow}});
    const qint64 lee = pos.addReservation({{u"name"_s, u"Lee"_s}, {u"size"_s, 6}, {u"at"_s, todayAt(19)},
                                           {u"note"_s, u"birthday"_s}});
    const qint64 park = pos.addReservation({{u"name"_s, u"Park"_s}, {u"size"_s, 2}, {u"at"_s, todayAt(18)}});
    REQUIRE(later > 0);
    QVariantList list = booked(pos);
    REQUIRE(list.size() == 3);
    CHECK(list[0].toMap()[u"name"_s] == u"Park"_s);               // soonest first
    CHECK(list[2].toMap()[u"time"_s].toString().startsWith(u"Tomorrow"_s));
    CHECK(waiting(pos).isEmpty());                                // bookings aren't in the line

    clock = todayAt(18, 20);
    CHECK(booked(pos)[0].toMap()[u"overdue"_s].toBool());         // Park is 20 minutes late
    REQUIRE(pos.partyGone(park, true));
    CHECK(pos.waitlistInfo()[u"noShows"_s].toInt() == 1);

    clock = todayAt(18, 55);
    REQUIRE(pos.checkInParty(lee));
    CHECK_FALSE(pos.checkInParty(lee));
    REQUIRE(waiting(pos).size() == 1);
    CHECK(waiting(pos)[0].toMap()[u"reservation"_s].toBool());
    clock = todayAt(19, 5);
    REQUIRE(pos.seatParty(lee, u"T5"_s));
    CHECK(booked(pos).size() == 1);
    CHECK(pos.waitlistInfo()[u"averageWait"_s].toInt() == 10);

    const core::Party &p = pos.shared()->parties[1];
    CHECK(app::partyFromJson(app::toJson(p)) == p);
}

TEST_CASE("The waitlist is saved and comes back", "[waitlist][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*store.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1111"_s));
        REQUIRE(pos.addToWaitlist({{u"name"_s, u"Dana"_s}, {u"size"_s, 4}}) > 0);
        const QString tomorrow = QDate::currentDate().addDays(1).toString(u"yyyy-MM-dd"_s) + u" 19:30"_s;
        REQUIRE(pos.addReservation({{u"name"_s, u"Kim"_s}, {u"at"_s, tomorrow}}) > 0);
        const qint64 gone = pos.addToWaitlist({{u"name"_s, u"Alex"_s}});
        REQUIRE(pos.partyGone(gone));
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    const auto data = store.load();
    REQUIRE(data);
    REQUIRE(data->parties.size() == 3);
    CHECK(data->lastPartyId == 3);
    PosService pos(*data, nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(waiting(pos).size() == 1);
    CHECK(booked(pos).size() == 1);
}
