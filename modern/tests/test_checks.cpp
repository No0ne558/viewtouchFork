#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// Managing checks: transfer, move, merge, reopen, and their history.

namespace {

QString idOf(const PosService &pos, const QString &name)
{
    for (const QVariant &v : pos.staff()) {
        if (v.toMap()[u"name"_s].toString().startsWith(name))
            return v.toMap()[u"id"_s].toString();
    }
    return {};
}

qint64 openTable(PosService &pos, const QString &table, const char *item = "coffee")
{
    REQUIRE(pos.selectTable(table) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(QString::fromLatin1(item));
    return pos.checkInfo()[u"id"_s].toLongLong();
}

QStringList history(const PosService &pos)
{
    QStringList out;
    for (const QVariant &v : pos.checkHistory())
        out << v.toMap()[u"what"_s].toString();
    return out;
}

} // namespace

TEST_CASE("Transfer a check to another server", "[checks]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));              // Sam
    const qint64 id = openTable(front, u"T1"_s);
    CHECK(front.staff().size() >= 3);
    const QString morgan = idOf(front, u"Morgan"_s);
    REQUIRE_FALSE(morgan.isEmpty());

    CHECK_FALSE(front.transferCheck(u"nobody"_s));
    REQUIRE(front.transferCheck(morgan));                // Sam hands over his own check
    CHECK(front.checkInfo()[u"server"_s].toString().startsWith(u"Morgan"_s));
    CHECK(history(front).last().contains(u"Transferred from Sam to Morgan"_s));
    front.releaseCheck();

    // Now it's Morgan's: another server can't take it.
    REQUIRE(bar.loginWithPin(u"2222"_s));
    REQUIRE(bar.openCheck(id));
    CHECK_FALSE(bar.transferCheck(idOf(bar, u"Sam"_s)));
    bar.releaseCheck();
    bar.logout();
    REQUIRE(bar.loginWithPin(u"1234"_s));                // a manager can
    REQUIRE(bar.openCheck(id));
    REQUIRE(bar.transferCheck(idOf(bar, u"Sam"_s)));
}

TEST_CASE("Move a check to another table", "[checks]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    openTable(pos, u"T1"_s);
    CHECK_FALSE(pos.moveCheck(u"T1"_s));
    CHECK_FALSE(pos.moveCheck(u"  "_s));
    REQUIRE(pos.moveCheck(u"T5"_s));
    CHECK(pos.checkInfo()[u"label"_s] == u"T5"_s);
    CHECK(pos.tableStatus(u"T5"_s)[u"open"_s].toBool());
    CHECK_FALSE(pos.tableStatus(u"T1"_s)[u"open"_s].toBool());
    CHECK(history(pos).last() == u"Moved from T1 to T5"_s);

    pos.releaseCheck();
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"coffee"_s);
    CHECK_FALSE(pos.moveCheck(u"T2"_s));                  // only table checks
}

TEST_CASE("Merge a check into the current one", "[checks]")
{
    test::RecordingSink sink;
    PosShared shared(test::seedPosData(), &sink);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    const qint64 a = openTable(front, u"T1"_s, "coffee");
    front.releaseCheck();
    const qint64 b = openTable(front, u"T2"_s, "cobb");
    REQUIRE(front.tender(u"cash"_s, 500));
    front.releaseCheck();

    // Not while the other check is open elsewhere.
    REQUIRE(bar.loginWithPin(u"1234"_s));
    REQUIRE(bar.openCheck(b));
    REQUIRE(front.openCheck(a));
    CHECK_FALSE(front.mergeCheck(b));
    bar.releaseCheck();

    CHECK_FALSE(front.mergeCheck(a));                     // not into itself
    REQUIRE(front.mergeCheck(b));
    CHECK(front.lines().size() == 2);
    CHECK(front.payments().size() == 1);
    CHECK(front.checkInfo()[u"guests"_s].toInt() == 2);
    CHECK(history(front).last().contains(u"merged into T1"_s));
    CHECK_FALSE(shared.open.contains(b));
    CHECK(sink.checks.at(b).status == core::CheckStatus::Merged);
    CHECK(sink.checks.at(b).lines.empty());
}

TEST_CASE("Reopen a closed check; its cash leaves the drawer until it closes again", "[checks]")
{
    PosService pos(test::seedPosData(), nullptr);   // drawer per terminal
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1111"_s));
    const qint64 id = openTable(pos, u"T3"_s, "cobb");   // $13.53
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$113.53"_s);
    CHECK(pos.closedChecks().isEmpty());                  // servers don't see the list
    CHECK_FALSE(pos.reopenCheck(id));
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.closedChecks().size() == 1);
    CHECK(pos.closedChecks().first().toMap()[u"label"_s] == u"T3"_s);
    CHECK_FALSE(pos.reopenCheck(9999));
    REQUIRE(pos.reopenCheck(id));
    CHECK(pos.hasCheck());
    CHECK(pos.checkInfo()[u"id"_s].toLongLong() == id);
    CHECK(pos.closedChecks().isEmpty());
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$100.00"_s);
    CHECK(history(pos).last() == u"Reopened"_s);

    // A refund: take the item off, give the cash back, close again.
    REQUIRE(pos.voidItem());                              // sent: voided by a manager
    pos.selectPayment(pos.payments().first().toMap()[u"id"_s].toLongLong());
    REQUIRE(pos.removePayment());
    REQUIRE(pos.closeCheck());
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$100.00"_s);
    REQUIRE(pos.closedChecks().size() == 1);
}

TEST_CASE("Check history: kept with the check, voids and discounts included", "[checks]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    openTable(pos, u"T4"_s, "cobb");
    REQUIRE(pos.sendOrder());
    REQUIRE(pos.voidItem());
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.tender(u"discount"_s));
    const QStringList h = history(pos);
    REQUIRE(h.size() == 2);
    CHECK(h[0].startsWith(u"Voided"_s));
    CHECK(h[1].startsWith(u"Discount"_s));
    CHECK(pos.checkHistory().first().toMap()[u"who"_s].toString().startsWith(u"Morgan"_s));

    const core::Check &c = pos.shared()->open.begin()->second;
    const auto back = app::checkFromJson(app::toJson(c));
    REQUIRE(back);
    CHECK(back->events == c.events);
}
