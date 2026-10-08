#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "net/stripe_api.hh"
#include "pos_fixture.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QSignalSpy>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Cards on a reader: preparing the charge, recording the approval (once),
// the reader's connection token, refunds on Undo Payment, the receipt.

namespace {

void openDrawer(PosService &pos)
{
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
}

QVariantMap approval(qint64 checkId, qint64 cents, const QString &ref, qint64 tip = 0)
{
    return {{u"approved"_s, true}, {u"reference"_s, ref}, {u"brand"_s, u"visa"_s}, {u"last4"_s, u"4242"_s},
            {u"amountCents"_s, cents}, {u"tipCents"_s, tip}, {u"checkId"_s, checkId}, {u"tenderId"_s, u"credit"_s},
            {u"processor"_s, u"stripe"_s}};
}

} // namespace

TEST_CASE("Cards on a reader: charge, record once, refund on Undo Payment", "[cards]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"coffee"_s));                        // $2.75 + tax = $2.98
    const qint64 check = pos.checkInfo()[u"id"_s].toLongLong();

    // Not a card: paid as usual.
    CHECK(pos.cardCharge(u"cash"_s)[u"notCard"_s].toBool());
    // The whole balance, or what was typed.
    QVariantMap charge = pos.cardCharge(u"credit"_s);
    REQUIRE(charge[u"ok"_s].toBool());
    CHECK(charge[u"amountCents"_s] == 298);
    CHECK(charge[u"currency"_s] == u"usd"_s);
    CHECK(charge[u"checkId"_s] == check);
    pos.entryKey(u"1"_s);
    pos.entryKey(u"0"_s);
    pos.entryKey(u"0"_s);
    CHECK(pos.cardCharge(u"credit"_s)[u"amountCents"_s] == 100);

    // Approved: on the check, with the card.
    REQUIRE(pos.recordCardPayment(approval(check, 100, u"pi_1"_s)));
    REQUIRE(pos.payments().size() == 1);
    CHECK(pos.payments()[0].toMap()[u"name"_s].toString().endsWith(u"Visa •••• 4242"_s));
    CHECK(pos.totals()[u"balance"_s] == u"$1.98"_s);
    // The same approval again (a resend): once.
    REQUIRE(pos.recordCardPayment(approval(check, 100, u"pi_1"_s)));
    CHECK(pos.payments().size() == 1);
    CHECK_FALSE(pos.recordCardPayment(approval(check, 100, u""_s)));     // which payment?

    // Undo Payment: without the store's key, it can't be refunded here.
    pos.selectPayment(pos.payments()[0].toMap()[u"id"_s].toLongLong());
    CHECK_FALSE(pos.removePayment());
    CHECK(pos.payments().size() == 1);

    // With it: the refund goes out, and the payment comes off when Stripe says so.
    pos.shared()->settings.stripeSecretKey = "sk_test_x";
    std::function<void(const QString &, const QString &)> answer;
    QString refunded;
    std::int64_t refundCents = 0;
    pos.shared()->stripeRefund = [&](const QString &pi, std::int64_t cents, auto done) {
        refunded = pi;
        refundCents = cents;
        answer = done;
    };
    REQUIRE(pos.removePayment());
    CHECK(refunded == u"pi_1"_s);
    CHECK(refundCents == 100);
    CHECK(pos.payments().size() == 1);                                  // not yet
    CHECK(pos.payments()[0].toMap()[u"name"_s].toString().contains(u"refunding"_s));
    CHECK_FALSE(pos.removePayment());                                   // already on its way
    answer(u"re_1"_s, {});
    CHECK(pos.payments().isEmpty());
    CHECK(pos.totals()[u"balance"_s] == u"$2.98"_s);

    // A refund Stripe turns down: the payment stays.
    REQUIRE(pos.recordCardPayment(approval(check, 298, u"pi_2"_s)));
    REQUIRE(pos.removePayment());
    answer({}, u"Charge already refunded"_s);
    CHECK(pos.payments().size() == 1);

    // A simulated card just comes off.
    pos.selectPayment(0);
    REQUIRE(pos.removePayment());   // pi_2 again: refund...
    answer(u"re_2"_s, {});
    QVariantMap sim = approval(check, 298, u"sim_1"_s);
    sim[u"processor"_s] = u"simulated"_s;
    REQUIRE(pos.recordCardPayment(sim));
    REQUIRE(pos.removePayment());
    CHECK(pos.payments().isEmpty());

    // The receipt shows the card and Stripe's payment id.
    REQUIRE(pos.recordCardPayment(approval(check, 298, u"pi_3"_s, 50)));
    const core::Check &c = pos.shared()->open.at(check);
    const std::string receipt = print::renderText(
        print::receipt(c, {pos.shared()->settings, [](std::int64_t) { return std::string("Oct 8"); },
                           [](std::int64_t) { return std::string("10:31 AM"); }, 0}), 42);
    CHECK(receipt.find("VISA **** 4242") != std::string::npos);
    CHECK(receipt.find("pi_3") != std::string::npos);
    CHECK(c.payments.back().tip.cents() == 50);
    CHECK(c.payments.back().amount.cents() == 248);                    // charged 2.98: 2.48 + 0.50 tip
    CHECK(app::checkFromJson(app::toJson(c))->payments == c.payments);
}

TEST_CASE("Cards on a reader: the connection token, the key kept secret", "[cards]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));

    // No key: the reader hears why.
    pos.requestReaderToken();
    CHECK(pos.readerToken()[u"token"_s].toString().isEmpty());
    CHECK(pos.readerToken()[u"error"_s].toString().contains(u"Stripe key"_s));
    const int first = pos.readerToken()[u"seq"_s].toInt();

    // The key, set on Store Settings: never shown again.
    const QVariantMap store = pos.adminRecords(u"store"_s).value(0).toMap();
    const auto save = [&](const QString &key) {
        QVariantMap r = store;
        r[u"stripeSecretKey"_s] = key;
        return pos.adminSave(u"store"_s, 0, r);
    };
    CHECK_FALSE(save(u"pk_live_oops"_s));                       // a publishable key
    REQUIRE(save(u"sk_test_abcd1234"_s));
    CHECK(pos.shared()->settings.stripeSecretKey == "sk_test_abcd1234");
    CHECK(pos.adminRecords(u"store"_s).value(0).toMap()[u"stripeSecretKey"_s].toString().isEmpty());
    bool hinted = false;
    for (const QVariant &f : pos.adminFields(u"store"_s))
        if (f.toMap()[u"path"_s] == u"stripeSecretKey"_s)
            hinted = f.toMap()[u"hint"_s].toString().contains(u"1234"_s);
    CHECK(hinted);
    REQUIRE(save(u""_s));                                       // empty: kept
    CHECK(pos.shared()->settings.stripeSecretKey == "sk_test_abcd1234");

    pos.shared()->stripeConnectionToken = [](auto done) { done(u"pst_test_123"_s, {}); };
    pos.requestReaderToken();
    CHECK(pos.readerToken()[u"token"_s] == u"pst_test_123"_s);
    CHECK(pos.readerToken()[u"seq"_s].toInt() > first);

    REQUIRE(save(u"none"_s));
    CHECK(pos.shared()->settings.stripeSecretKey.empty());
}

TEST_CASE("StripeApi: connection tokens and refunds, with the key", "[cards][stripeapi]")
{
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QStringList requests;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [&, s] {
            const QByteArray req = s->readAll();
            requests << QString::fromUtf8(req);
            QByteArray body;
            if (req.contains("/v1/terminal/connection_tokens"))
                body = R"({"object":"terminal.connection_token","secret":"pst_test_abc"})";
            else if (req.contains("payment_intent=pi_bad"))
                body = R"({"error":{"message":"No such payment_intent: pi_bad"}})";
            else
                body = R"({"id":"re_123","status":"succeeded"})";
            const QByteArray status = body.contains("error") ? "400 Bad Request" : "200 OK";
            s->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                     + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            s->disconnectFromHost();
        });
    });
    const auto wait = [](const std::function<bool()> &ready) {
        for (int i = 0; i < 500 && !ready(); ++i)
            QTest::qWait(10);
    };
    QString key = u"sk_test_key"_s;
    net::StripeApi api([&] { return key; });
    api.setBaseUrl(QUrl(u"http://127.0.0.1:%1"_s.arg(server.serverPort())));

    QString token, error = u"-"_s;
    api.connectionToken([&](const QString &t, const QString &e) { token = t; error = e; });
    wait([&] { return !token.isEmpty(); });
    CHECK(token == u"pst_test_abc"_s);
    CHECK(error.isEmpty());
    CHECK(requests.last().contains(u"Authorization: Bearer sk_test_key"_s));

    QString refund;
    api.refund(u"pi_123"_s, 250, [&](const QString &id, const QString &e) { refund = id; error = e; });
    wait([&] { return !refund.isEmpty(); });
    CHECK(refund == u"re_123"_s);
    CHECK(requests.last().contains(u"payment_intent=pi_123&amount=250"_s));

    bool done = false;
    api.refund(u"pi_bad"_s, 0, [&](const QString &, const QString &e) { error = e; done = true; });
    wait([&] { return done; });
    CHECK(done);
    CHECK(error == u"No such payment_intent: pi_bad"_s);

    // No key: it says so, without asking Stripe.
    key.clear();
    const int asked = int(requests.size());
    done = false;
    api.connectionToken([&](const QString &, const QString &e) { error = e; done = true; });
    CHECK(done);
    CHECK(error.contains(u"key"_s));
    CHECK(requests.size() == asked);
}
