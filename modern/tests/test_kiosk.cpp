#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "net/layout_hub.hh"
#include "net/pairing.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QDeadlineTimer>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;

// The self-order kiosk: guests order on their own and pay at the counter.

namespace {

QStringList ids(const QVariantList &items)
{
    QStringList out;
    for (const QVariant &v : items)
        out << v.toMap()[u"id"_s].toString();
    return out;
}

const core::Check *openCheck(const app::PosShared &shared, std::int64_t id)
{
    const auto it = shared.open.find(id);
    return it == shared.open.end() ? nullptr : &it->second;
}

// Process events until `done` or the time is up.
bool waitFor(const std::function<bool()> &done, int msec = 5000)
{
    QDeadlineTimer deadline(msec);
    while (!done()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

} // namespace

TEST_CASE("Self-order: a manager turns a screen into a kiosk; only a manager's PIN ends it", "[kiosk]")
{
    app::PosShared shared(test::seedPosData(true), nullptr);
    app::PosService screen(&shared, u"Lobby"_s);
    REQUIRE(screen.loginWithPin(u"1111"_s));   // a server can't
    CHECK_FALSE(screen.setSelfOrder(true));
    screen.logout();
    REQUIRE(screen.loginWithPin(u"1234"_s));
    REQUIRE(screen.setSelfOrder(true));
    CHECK(screen.selfOrderInfo()[u"on"_s].toBool());
    CHECK(screen.userName() == u"Self-order kiosk"_s);
    CHECK_FALSE(screen.can(u"manager"_s));
    CHECK_FALSE(screen.can(u"check.settle"_s));

    // Logging out (the idle timer) doesn't end it.
    screen.logout();
    CHECK(screen.selfOrderInfo()[u"on"_s].toBool());
    CHECK(screen.loggedIn());

    CHECK_FALSE(screen.leaveSelfOrder(u"1111"_s));   // a server's PIN
    CHECK(screen.selfOrderInfo()[u"on"_s].toBool());
    REQUIRE(screen.leaveSelfOrder(u"1234"_s));
    CHECK_FALSE(screen.selfOrderInfo()[u"on"_s].toBool());
    CHECK(screen.userName().startsWith(u"Morgan"_s));   // the manager is logged in here now
    CHECK(screen.kioskMenu().isEmpty());
}

TEST_CASE("Self-order: what guests see, and never alcohol", "[kiosk]")
{
    app::PosShared shared(test::seedPosData(true), nullptr);
    app::PosService kiosk(&shared, u"Lobby"_s);
    kiosk.enableSelfOrder();
    const QVariantMap menu = kiosk.kioskMenu();
    const QStringList items = ids(menu[u"items"_s].toList());
    CHECK(items.contains(u"classic-burger"_s));
    CHECK(items.contains(u"water"_s));
    CHECK_FALSE(items.contains(u"draft-beer"_s));   // alcohol
    CHECK_FALSE(items.contains(u"house-wine"_s));
    CHECK_FALSE(items.contains(u"rare"_s));         // a modifier
    CHECK(menu[u"families"_s].toStringList().contains(u"burgers"_s));

    // Kept off the kiosk by the store; 86'd items show as sold out.
    shared.menu[0].kioskHide = true;
    CHECK_FALSE(ids(kiosk.kioskMenu()[u"items"_s].toList()).contains(QString::fromStdString(shared.menu[0].id)));
    CHECK_FALSE(kiosk.kioskAdd(u"draft-beer"_s));
    CHECK_FALSE(kiosk.kioskAdd(QString::fromStdString(shared.menu[0].id)));
}

TEST_CASE("Self-order: order, choose, name, then pay at the counter", "[kiosk]")
{
    app::PosShared shared(test::seedPosData(true), nullptr);
    app::PosService kiosk(&shared, u"Lobby"_s);
    kiosk.enableSelfOrder();

    REQUIRE(kiosk.kioskStart(true));                       // to go
    const qint64 id = kiosk.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(kiosk.kioskAdd(u"water"_s));
    REQUIRE(kiosk.kioskAdd(u"house-salad"_s));            // asks for a dressing
    const QVariantMap choosing = kiosk.choosingInfo();
    REQUIRE(choosing[u"active"_s].toBool());
    CHECK_FALSE(kiosk.kioskFinish({{u"name"_s, u"Lee"_s}}));   // the dressing isn't chosen yet
    const QVariantMap dressing = choosing[u"groups"_s].toList().value(0).toMap();
    REQUIRE(kiosk.chooseOption(dressing[u"id"_s].toString(), 0));
    REQUIRE(kiosk.finishChoosing());
    CHECK_FALSE(kiosk.kioskFinish({{u"name"_s, u"  "_s}}));    // a name to call it by

    REQUIRE(kiosk.kioskFinish({{u"name"_s, u"Lee"_s}}));
    const QVariantMap last = kiosk.selfOrderInfo()[u"lastOrder"_s].toMap();
    CHECK(last[u"number"_s].toLongLong() == id);
    CHECK(last[u"name"_s] == u"Lee"_s);
    CHECK_FALSE(last[u"sent"_s].toBool());
    CHECK_FALSE(kiosk.selfOrderInfo()[u"ordering"_s].toBool());

    // Waiting at the counter: open, not sent, not locked by the kiosk.
    const core::Check *c = openCheck(shared, id);
    REQUIRE(c);
    CHECK(c->kiosk);
    CHECK(c->type == core::CheckType::Takeout);
    CHECK(c->label == "Kiosk " + std::to_string(id));
    CHECK(c->customer.name == "Lee");
    CHECK(c->unsentCount() == 2);
    CHECK(kiosk.kitchenTickets().isEmpty());

    // The cashier takes the money; closing sends it to the kitchen.
    app::PosService counter(&shared, u"Counter"_s);
    REQUIRE(counter.loginWithPin(u"2222"_s));
    REQUIRE(counter.openCheck(id));
    REQUIRE(counter.openDrawerSession());
    REQUIRE(counter.tender(u"cash"_s));
    REQUIRE(counter.closeCheck());
    CHECK(counter.kitchenTickets().size() == 1);
}

TEST_CASE("Self-order: straight to the kitchen when the store says so; walking away clears it", "[kiosk]")
{
    app::PosShared shared(test::seedPosData(true), nullptr);
    shared.settings.kioskSendNow = true;
    app::PosService kiosk(&shared, u"Lobby"_s);
    kiosk.enableSelfOrder();
    REQUIRE(kiosk.kioskStart(false));                      // for here
    REQUIRE(kiosk.kioskAdd(u"caesar"_s));                  // the protein is optional
    REQUIRE(kiosk.kioskFinish({{u"name"_s, u"Ana"_s}}));
    CHECK(kiosk.selfOrderInfo()[u"lastOrder"_s].toMap()[u"sent"_s].toBool());
    CHECK(kiosk.kitchenTickets().size() == 1);

    // The next guest leaves half way: nothing is left behind.
    const std::size_t before = shared.open.size();
    REQUIRE(kiosk.kioskStart(false));
    REQUIRE(kiosk.kioskAdd(u"water"_s));
    const qint64 id = kiosk.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(kiosk.kioskRemove(kiosk.lines().value(0).toMap()[u"id"_s].toLongLong()));
    REQUIRE(kiosk.kioskAdd(u"water"_s));
    kiosk.logout();                                        // idle
    CHECK_FALSE(openCheck(shared, id));
    CHECK(shared.open.size() == before);
    CHECK(kiosk.selfOrderInfo()[u"on"_s].toBool());
    CHECK(kiosk.kitchenTickets().size() == 1);             // only Ana's
}

TEST_CASE("Self-order: a paired screen set up as a kiosk comes up as one", "[kiosk][net]")
{
    auto layout = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(layout);
    app::PosShared shared(test::seedPosData(), nullptr);
    net::LayoutHub hub(*layout);
    net::PosServer server(&shared, &hub);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    app::PosService office(&shared, u"Office"_s);
    REQUIRE(office.loginWithPin(u"1234"_s));
    REQUIRE(office.startPairing());
    net::Pairer pairer;
    QSignalSpy paired(&pairer, &net::Pairer::finished);
    pairer.start(u"127.0.0.1"_s, server.port(), office.pairingInfo()[u"code"_s].toString(), u"Lobby"_s);
    REQUIRE(paired.wait(10000));
    const auto creds = paired[0][1].value<net::Credentials>();
    // A photo for the Classic Burger, on the server's disk.
    QTemporaryDir dir;
    const QString photo = dir.filePath(u"burger.png"_s);
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::darkYellow);
        REQUIRE(img.save(photo));
    }
    for (core::MenuItem &m : shared.menu)
        if (m.id == "classic-burger")
            m.image = photo.toStdString();
    // Only the store's pictures are handed out, never any other file.
    CHECK_FALSE(office.storeImage(photo).isEmpty());
    CHECK(office.storeImage(dir.filePath(u"other.png"_s)).isEmpty());
    CHECK(office.storeImage(u"/etc/hostname"_s).isEmpty());

    // Manager -> Terminals: the Lobby screen is a self-order kiosk.
    for (core::TerminalConfig &t : shared.settings.terminals)
        if (t.name == "Lobby")
            t.screen = "selfOrder";

    net::RemoteSession lobby(u"Lobby"_s);
    lobby.setCredentials(creds);
    lobby.setImageCache(dir.filePath(u"cache"_s));
    lobby.connectTo(creds.host, creds.port);
    REQUIRE(lobby.waitForWelcome(5000));
    CHECK(lobby.selfOrderInfo()[u"on"_s].toBool());
    CHECK_FALSE(lobby.kioskMenu()[u"items"_s].toList().isEmpty());

    // The photo comes over from the server and is kept on this device.
    const auto burgerImage = [&] {
        for (const QVariant &v : lobby.kioskMenu()[u"items"_s].toList())
            if (v.toMap()[u"id"_s] == u"classic-burger"_s)
                return lobby.imageUrl(v.toMap()[u"image"_s].toString());   // this screen's copy
        return QString();
    };
    REQUIRE(waitFor([&] { return !burgerImage().isEmpty(); }));
    const QString local = QUrl(burgerImage()).toLocalFile();
    CHECK(local.startsWith(dir.filePath(u"cache"_s)));
    QFile a(photo), b(local);
    REQUIRE(a.open(QIODevice::ReadOnly));
    REQUIRE(b.open(QIODevice::ReadOnly));
    CHECK(a.readAll() == b.readAll());

    // A store picture (Manager added it on another screen) and the logo: fetched by ref, kept here.
    QByteArray logo;
    {
        QImage img(30, 30, QImage::Format_RGB32);
        img.fill(Qt::blue);
        QBuffer buffer(&logo);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
    }
    REQUIRE(office.addStoreImage(u"Logo.png"_s, QString::fromLatin1(logo.toBase64())));
    shared.settings.displayLogo = "store:logo.png";
    ++shared.adminRevision;
    emit shared.adminChanged();
    REQUIRE(waitFor([&] { return !lobby.imageUrl(u"logo:"_s).isEmpty(); }));
    QFile kept(QUrl(lobby.imageUrl(u"logo:"_s)).toLocalFile());
    REQUIRE(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll() == logo);

    lobby.kioskStart(true);
    CHECK(waitFor([&] { return lobby.selfOrderInfo()[u"ordering"_s].toBool(); }));
    lobby.kioskAdd(u"caesar"_s);
    CHECK(waitFor([&] { return lobby.lines().size() == 1; }));
}

TEST_CASE("Self-order: the guest's slip, from the kiosk's own printer", "[kiosk][slip]")
{
    app::PosData data = test::seedPosData(true);
    core::PrinterConfig lobby;
    lobby.id = "lobby";
    lobby.name = "Lobby printer";
    lobby.type = "file";
    lobby.path = "lobby.txt";
    data.settings.printers.push_back(lobby);
    core::TerminalConfig t;
    t.name = "Lobby";
    t.receiptPrinter = "lobby";
    data.settings.terminals.push_back(t);
    app::PosShared shared(data, nullptr);
    struct Slips : app::PosPrinter {
        std::vector<std::tuple<std::int64_t, std::string, bool>> slips;
        int receipts = 0;
        void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
        void printReceipt(const core::PosSettings &, const core::Check &, const std::string &) override { ++receipts; }
        void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
        void openDrawer(const core::PosSettings &, const std::string &) override {}
        void printOrderSlip(const core::PosSettings &, const core::Check &c, const std::string &p, bool sent) override
        {
            slips.emplace_back(c.id, p, sent);
        }
    } printer;
    shared.printer = &printer;
    app::PosService kiosk(&shared, u"Lobby"_s);
    kiosk.enableSelfOrder();
    REQUIRE(kiosk.kioskStart(false));
    const qint64 id = kiosk.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(kiosk.kioskAdd(u"water"_s));
    REQUIRE(kiosk.kioskFinish({{u"name"_s, u"Lee"_s}}));
    REQUIRE(printer.slips.size() == 1);
    CHECK(std::get<0>(printer.slips[0]) == id);
    CHECK(std::get<1>(printer.slips[0]) == "lobby");   // the kiosk's own printer
    CHECK_FALSE(std::get<2>(printer.slips[0]));        // waits for the counter
    CHECK(printer.receipts == 0);
    CHECK(kiosk.selfOrderInfo()[u"lastOrder"_s].toMap()[u"slip"_s].toBool());

    // Turned off: none.
    shared.settings.kioskSlip = false;
    kiosk.kioskCancel();
    REQUIRE(kiosk.kioskStart(false));
    REQUIRE(kiosk.kioskAdd(u"water"_s));
    REQUIRE(kiosk.kioskFinish({{u"name"_s, u"Sam"_s}}));
    CHECK(printer.slips.size() == 1);
    CHECK_FALSE(kiosk.selfOrderInfo()[u"lastOrder"_s].toMap()[u"slip"_s].toBool());
}

TEST_CASE("Self-order: what the slip says, on any paper", "[kiosk][slip]")
{
    const app::PosData data = test::seedPosData(true);
    core::Check c;
    c.id = 4127;
    c.type = core::CheckType::Takeout;
    c.customer.name = "Lee";
    core::OrderLine l;
    l.id = 1;
    l.itemId = "burger";
    l.name = "Cheeseburger";
    l.unitPrice = Money::fromCents(1150);
    l.quantity = 2;
    core::Modifier rare;
    rare.name = "Medium rare";
    core::Modifier bacon;
    bacon.name = "Extra bacon";
    bacon.unitPrice = Money::fromCents(200);
    l.modifiers = {rare, bacon};
    c.lines.push_back(l);
    print::TicketContext ctx{data.settings, [](std::int64_t) { return std::string("Oct 7, 2026 6:30 PM"); },
                             [](std::int64_t) { return std::string("6:30 PM"); }, 0, nullptr};
    for (const bool sent : {false, true}) {
        for (const int width : {32, 42, 48}) {
            const std::string text = print::renderText(print::orderSlip(c, sent, ctx), width);
            INFO(width << "\n" << text);
            CHECK(text.find("4127") != std::string::npos);
            CHECK(text.find("Lee") != std::string::npos);
            CHECK(text.find("To go") != std::string::npos);
            CHECK(text.find("2 x Cheeseburger") != std::string::npos);
            CHECK(text.find("Extra bacon") != std::string::npos);
            CHECK(text.find("Medium rare") == std::string::npos);   // free: as on receipts
            CHECK(text.find("Please pay at the counter") != std::string::npos);
            CHECK(text.find(sent ? "We're making it now" : "once it's paid") != std::string::npos);
            std::istringstream in(text);
            for (std::string row; std::getline(in, row);)
                CHECK(print::displayWidth(row) <= std::size_t(width));
        }
    }
}
