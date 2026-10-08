#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QTest>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// The guest's receipt: printed where the server chooses (a handheld),
// printed or offered after paying (per terminal), emailed by Stripe.

namespace {

struct Printed : app::PosPrinter {
    std::vector<std::pair<std::int64_t, std::string>> receipts;   // check, printer
    void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
    void printReceipt(const core::PosSettings &, const core::Check &c, const std::string &p) override { receipts.emplace_back(c.id, p); }
    void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
    void openDrawer(const core::PosSettings &, const std::string &) override {}
};

app::PosData store(const std::string &receiptPrinter, const std::string &afterPaying)
{
    app::PosData data = test::seedPosData();
    core::PrinterConfig host;
    host.id = "host";
    host.name = "Host Stand";
    host.type = "file";
    host.receipts = true;
    data.settings.printers.push_back(host);
    core::TerminalConfig t;
    t.name = "Handheld";
    t.receiptPrinter = receiptPrinter;
    t.afterPaying = afterPaying;
    data.settings.terminals.push_back(t);
    return data;
}

qint64 payCash(PosService &pos)
{
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"coffee"_s));
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    return id;
}

QStringList printerNames(const QVariantMap &offer)
{
    QStringList out;
    for (const QVariant &p : offer[u"printers"_s].toList())
        out << p.toMap()[u"name"_s].toString();
    return out;
}

} // namespace

TEST_CASE("Receipts: a handheld chooses the printer each time", "[receipts]")
{
    PosShared shared(store("ask", ""), nullptr);
    Printed printed;
    shared.printer = &printed;
    PosService pos(&shared, u"Handheld"_s);
    const qint64 id = payCash(pos);
    CHECK(pos.receiptOffer().isEmpty());          // nothing after paying (this terminal's choice)
    CHECK(printed.receipts.empty());

    REQUIRE(pos.printReceipt());                  // Print Receipt: where?
    const QVariantMap offer = pos.receiptOffer();
    CHECK(offer[u"checkId"_s].toLongLong() == id);
    CHECK(printerNames(offer) == QStringList{u"Receipt"_s, u"Host Stand"_s});   // not the kitchen's
    CHECK_FALSE(offer[u"canEmail"_s].toBool());   // cash: nothing for Stripe to email
    CHECK(printed.receipts.empty());
    REQUIRE(pos.printReceiptOn(id, u"host"_s));
    CHECK(printed.receipts == decltype(printed.receipts){{id, "host"}});
    CHECK(pos.receiptOffer().isEmpty());
    CHECK_FALSE(pos.printReceiptOn(id, u"nowhere"_s));

    // A copy from Find a Check asks too.
    pos.searchChecks(u"#%1"_s.arg(id), 0);
    for (int i = 0; i < 300 && pos.checkSearch()[u"loading"_s].toBool(); ++i)
        QTest::qWait(10);
    REQUIRE(pos.reprintCheck(id));
    CHECK(pos.receiptOffer()[u"checkId"_s].toLongLong() == id);
    pos.noReceipt();
    CHECK(pos.receiptOffer().isEmpty());
    CHECK(printed.receipts.size() == 1);
}

TEST_CASE("Receipts: printed or offered after paying, per terminal", "[receipts]")
{
    SECTION("print: on the terminal's printer, by itself")
    {
        PosShared shared(store("host", "print"), nullptr);
        Printed printed;
        shared.printer = &printed;
        PosService pos(&shared, u"Handheld"_s);
        const qint64 id = payCash(pos);
        CHECK(printed.receipts == decltype(printed.receipts){{id, "host"}});
        CHECK(pos.receiptOffer().isEmpty());
    }
    SECTION("print, but the printer is chosen each time: asked where")
    {
        PosShared shared(store("ask", "print"), nullptr);
        Printed printed;
        shared.printer = &printed;
        PosService pos(&shared, u"Handheld"_s);
        const qint64 id = payCash(pos);
        CHECK(printed.receipts.empty());
        CHECK(pos.receiptOffer()[u"checkId"_s].toLongLong() == id);
    }
    SECTION("ask: print, email or none")
    {
        PosShared shared(store("", "ask"), nullptr);
        Printed printed;
        shared.printer = &printed;
        PosService pos(&shared, u"Handheld"_s);
        const qint64 id = payCash(pos);
        CHECK(pos.receiptOffer()[u"checkId"_s].toLongLong() == id);
        pos.noReceipt();
        CHECK(printed.receipts.empty());
    }
    SECTION("other terminals: as before")
    {
        PosShared shared(store("", "ask"), nullptr);
        Printed printed;
        shared.printer = &printed;
        PosService pos(&shared, u"Register"_s);
        payCash(pos);
        CHECK(pos.receiptOffer().isEmpty());
        CHECK(printed.receipts.empty());
    }
}

TEST_CASE("Receipts: Stripe emails the receipt for a card from a Stripe reader", "[receipts][cards]")
{
    PosShared shared(store("", "ask"), nullptr);
    shared.settings.stripeSecretKey = "sk_test_x";
    QStringList calls;
    shared.stripeCall = [&](const QString &method, const QString &path, const QString &form,
                            std::function<void(const QJsonObject &, const QString &)> done) {
        calls << method + u' ' + path + (form.isEmpty() ? QString() : u" ?"_s + form);
        if (path == u"/v1/payment_intents/pi_7")
            return done({{u"latest_charge"_s, u"ch_7"_s}}, {});
        return done({}, {});
    };
    PosService pos(&shared, u"Handheld"_s);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"coffee"_s));
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(pos.recordCardPayment({{u"reference"_s, u"pi_7"_s}, {u"amountCents"_s, 298}, {u"tipCents"_s, 0},
                                   {u"checkId"_s, id}, {u"tenderId"_s, u"credit"_s}, {u"processor"_s, u"stripe"_s}}));
    if (pos.hasCheck())
        REQUIRE(pos.closeCheck());
    const QVariantMap offer = pos.receiptOffer();
    REQUIRE(offer[u"checkId"_s].toLongLong() == id);
    CHECK(offer[u"canEmail"_s].toBool());

    CHECK_FALSE(pos.emailReceipt(id, u"not an email"_s));
    REQUIRE(pos.emailReceipt(id, u"guest@example.com"_s));
    CHECK(calls.contains(u"GET /v1/payment_intents/pi_7"_s));
    CHECK(calls.contains(u"POST /v1/charges/ch_7 ?receipt_email=guest%40example.com"_s));
    CHECK(pos.receiptOffer().isEmpty());
}

TEST_CASE("Receipts: a check paid in cash can't be emailed", "[receipts]")
{
    PosShared shared(store("", "ask"), nullptr);
    shared.settings.stripeSecretKey = "sk_test_x";
    shared.stripeCall = [](const QString &, const QString &, const QString &,
                           std::function<void(const QJsonObject &, const QString &)> done) { done({}, {}); };
    PosService pos(&shared, u"Handheld"_s);
    const qint64 id = payCash(pos);
    CHECK_FALSE(pos.emailReceipt(id, u"guest@example.com"_s));
}
