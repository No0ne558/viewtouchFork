#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;
using vt::core::CashMovement;

// Server banks: each person keeps the cash they take, on any terminal, and
// turns it in when they check out.

namespace {

app::PosData bankData()
{
    app::PosData d = test::seedPosData();
    d.settings.cashMode = core::CashMode::ServerBank;
    return d;
}

struct Kicks : app::PosPrinter {
    int drawerKicks = 0;
    void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
    void printReceipt(const core::PosSettings &, const core::Check &, const std::string &) override {}
    void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
    void openDrawer(const core::PosSettings &, const std::string &) override { ++drawerKicks; }
};

// A $6.60 cash sale closed by whoever is logged in on `pos`.
void cashSale(PosService &pos)
{
    pos.addItem(u"draft-beer"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
}

} // namespace

TEST_CASE("Server banks: cash follows the person, on any terminal", "[bank]")
{
    PosShared shared(bankData(), nullptr);
    Kicks kicks;
    shared.printer = &kicks;
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);

    // Sam takes cash on Front: no drawer needed, Sam's bank starts at $0.
    REQUIRE(front.loginWithPin(u"1111"_s));
    CHECK(front.drawerInfo()[u"mode"_s].toString() == u"serverBank"_s);
    CHECK_FALSE(front.drawerInfo()[u"exists"_s].toBool());
    cashSale(front);
    QVariantMap sam = front.drawerInfo();
    CHECK(sam[u"name"_s].toString() == u"Sam's bank"_s);
    CHECK(sam[u"expected"_s].toString() == u"$6.60"_s);
    CHECK(kicks.drawerKicks == 0);                 // no drawer to open

    // Another person on the same terminal has their own bank.
    front.logout();
    REQUIRE(front.loginWithPin(u"2222"_s));
    CHECK_FALSE(front.drawerInfo()[u"exists"_s].toBool());
    cashSale(front);
    CHECK(front.drawerInfo()[u"expected"_s].toString() == u"$6.60"_s);
    CHECK(front.drawerInfo()[u"name"_s].toString() != sam[u"name"_s].toString());

    // Sam moves to the bar: same bank.
    REQUIRE(bar.loginWithPin(u"1111"_s));
    cashSale(bar);
    CHECK(bar.drawerInfo()[u"expected"_s].toString() == u"$13.20"_s);
    CHECK(shared.drawers.size() == 2);
}

TEST_CASE("Server banks: start with cash on hand, check out, over / short", "[bank]")
{
    PosService pos(bankData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    pos.entryKey(u"2000"_s);
    REQUIRE(pos.openDrawerSession());              // Start Bank with $20 of change
    CHECK_FALSE(pos.openDrawerSession());
    cashSale(pos);
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$26.60"_s);

    // Not while a check is still open.
    pos.addItem(u"draft-beer"_s);
    const qint64 open = pos.checkInfo()[u"id"_s].toLongLong();
    pos.releaseCheck();
    pos.entryKey(u"2660"_s);
    CHECK_FALSE(pos.countDrawer());
    REQUIRE(pos.openCheck(open));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    pos.entryKey(u"3000"_s);                       // turns in $30.00 of $33.20
    REQUIRE(pos.countDrawer());
    const QVariantMap d = pos.drawerInfo();
    CHECK_FALSE(d[u"open"_s].toBool());
    CHECK(d[u"overShortCents"_s].toLongLong() == -320);

    // The next cash sale starts a new bank.
    cashSale(pos);
    CHECK(pos.drawerInfo()[u"open"_s].toBool());
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$6.60"_s);
}

TEST_CASE("Server banks: managers count banks left open; end of day waits for them", "[bank]")
{
    PosShared shared(bankData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService office(&shared, u"Office"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    cashSale(front);
    front.logout();                                // Sam leaves without checking out

    REQUIRE(office.loginWithPin(u"1234"_s));
    const QStringList blockers = office.dayInfo()[u"blockers"_s].toStringList();
    CHECK(blockers.contains(u"Sam's bank has not been counted"_s));
    const QVariantList others = office.drawerInfo()[u"others"_s].toList();
    REQUIRE(others.size() == 1);
    CHECK(others[0].toMap()[u"name"_s].toString() == u"Sam's bank"_s);
    CHECK(others[0].toMap()[u"expected"_s].toString() == u"$6.60"_s);

    // Servers can't count someone else's bank.
    REQUIRE(front.loginWithPin(u"2222"_s));
    front.entryKey(u"660"_s);
    CHECK_FALSE(front.countDrawerById(others[0].toMap()[u"id"_s].toLongLong()));

    office.entryKey(u"660"_s);
    REQUIRE(office.countDrawerById(others[0].toMap()[u"id"_s].toLongLong()));
    CHECK(office.drawerInfo()[u"others"_s].toList().isEmpty());
    CHECK(office.dayInfo()[u"ready"_s].toBool());
}

TEST_CASE("Server banks: pay outs and tip cash-outs come from your own bank", "[bank]")
{
    app::PosData data = bankData();
    data.settings.gratuityBp = 1800;
    data.settings.gratuityMinGuests = 2;
    PosService pos(data, nullptr);

    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"500"_s);
    REQUIRE(pos.payout(CashMovement::Kind::Payout));   // the manager's bank goes to -$5
    CHECK(pos.drawerInfo()[u"name"_s].toString() == u"Morgan (Manager)'s bank"_s);
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"-$5.00"_s);
    pos.logout();

    // Sam's card tip is paid from Sam's own cash.
    REQUIRE(pos.loginWithPin(u"1111"_s));
    pos.addItem(u"draft-beer"_s);
    REQUIRE(pos.tender(u"credit"_s));
    pos.entryKey(u"200"_s);
    REQUIRE(pos.addTip(0));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.cashOutTips());
    CHECK(pos.drawerInfo()[u"name"_s].toString() == u"Sam's bank"_s);
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"-$2.00"_s);   // the house owes Sam's bank $2
}

TEST_CASE("Cash handling is a store setting", "[bank][admin]")
{
    PosService pos(test::seedPosData(), nullptr);   // drawer per terminal
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap store = pos.adminRecords(u"store"_s)[0].toMap();
    CHECK(store[u"cashMode"_s].toString() == u"drawer"_s);
    store[u"cashMode"_s] = u"serverBank"_s;
    REQUIRE(pos.adminSave(u"store"_s, 0, store));
    CHECK(pos.shared()->settings.cashMode == core::CashMode::ServerBank);
    cashSale(pos);                                   // no drawer needed now
    CHECK(app::settingsFromJson(app::toJson(pos.shared()->settings)).cashMode == core::CashMode::ServerBank);
}
