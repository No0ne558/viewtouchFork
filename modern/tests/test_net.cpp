#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// M5: several terminals on one store.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

layout::Layout seedLayout()
{
    auto l = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(l);
    return *l;
}

// A server with its shared store, and a way to add remote terminals.
struct Store {
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    net::LayoutHub hub{seedLayout()};
    net::PosServer server{&shared, &hub};

    Store()
    {
        shared.setClock([] { return std::int64_t(1'700'000'000'000); });
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    }

    // A device already paired under `name`.
    net::Credentials pairedDevice(const QString &name)
    {
        core::TerminalConfig t;
        t.name = name.toStdString();
        t.id = net::newDeviceId().toStdString();
        const QByteArray key = net::newDeviceKey();
        t.key = key.toBase64().toStdString();
        shared.settings.terminals.push_back(t);
        net::Credentials c;
        c.host = u"127.0.0.1"_s;
        c.port = server.port();
        c.terminalId = QString::fromStdString(t.id);
        c.terminalName = name;
        c.key = key;
        return c;
    }

    std::unique_ptr<net::RemoteSession> terminal(const QString &name)
    {
        auto t = std::make_unique<net::RemoteSession>(name);
        t->setCredentials(pairedDevice(name));
        t->connectTo(u"127.0.0.1"_s, server.port());
        REQUIRE(t->waitForWelcome(5000));
        return t;
    }
};

// A remote terminal with its page controller.
struct Screen {
    std::unique_ptr<net::RemoteSession> remote;
    LayoutController c;

    explicit Screen(std::unique_ptr<net::RemoteSession> r)
        : remote(std::move(r))
        , c(remote->layout())
    {
        c.setMealPeriod(u"lunch"_s);
        c.setPos(remote.get());
        c.setSaver([this](const layout::Layout &l, QString *) { remote->saveLayout(l); return true; });
        QObject::connect(remote.get(), &net::RemoteSession::layoutReceived, &c,
                         [this](const layout::Layout &l) { c.replaceLayout(l); });
    }

    void settle() { waitFor([&] { return !c.busy(); }); QCoreApplication::processEvents(); }
    void tap(const QString &zone)
    {
        c.activate(zone);
        settle();
    }
    void pin(const char *digits)
    {
        for (const char *d = digits; *d; ++d)
            remote->pinKey(QString(QChar(*d)));
        c.login();
        REQUIRE(waitFor([&] { return remote->loggedIn(); }));
        settle();
    }
};

} // namespace

TEST_CASE("Two terminals on one store: locks and live updates", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(bar.loginWithPin(u"2222"_s));

    QSignalSpy barSees(&bar, &PosService::openChecksChanged);
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    front.addItem(u"coffee"_s);
    CHECK(barSees.size() >= 1);
    CHECK(bar.openChecks().size() == 1);
    CHECK(bar.tableStatus(u"T1"_s)[u"busyOn"_s].toString() == u"Front"_s);

    // The check is Front's while it is open there.
    QSignalSpy notices(&bar, &PosService::notice);
    CHECK(bar.selectTable(u"T1"_s) == PosService::TableFailed);
    CHECK(notices.last()[0].toString().contains(u"Front"_s));

    front.releaseCheck();
    CHECK(bar.selectTable(u"T1"_s) == PosService::TableOpened);
    CHECK(bar.lines().size() == 1);
    CHECK(front.tableStatus(u"T1"_s)[u"busyOn"_s].toString() == u"Bar"_s);

    // One sequence of check numbers across terminals.
    front.startCheck(core::CheckType::Takeout);
    CHECK(front.checkInfo()[u"id"_s].toLongLong() == 2);
}

TEST_CASE("A terminal going away frees its check", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(bar.loginWithPin(u"2222"_s));
    qint64 id = 0;
    {
        PosService front(&shared, u"Front"_s);
        REQUIRE(front.loginWithPin(u"1111"_s));
        front.startCheck(core::CheckType::Takeout);
        id = front.checkInfo()[u"id"_s].toLongLong();
        CHECK_FALSE(bar.openCheck(id));
    }
    CHECK(bar.openCheck(id));
}

TEST_CASE("Deactivating someone logs them out everywhere", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService manager(&shared, u"Office"_s);
    PosService floor(&shared, u"Floor"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));
    REQUIRE(floor.loginWithPin(u"1111"_s));
    QSignalSpy loggedOut(&floor, &PosService::loggedInChanged);

    int sam = -1;
    for (int i = 0; i < int(shared.employees.size()); ++i)
        if (shared.employees[i].id == "sam") sam = i;
    REQUIRE(manager.adminDelete(u"employees"_s, sam));
    CHECK_FALSE(floor.loggedIn());
    REQUIRE(loggedOut.size() == 1);
    CHECK_FALSE(loggedOut[0][0].toBool());
}

TEST_CASE("Kitchen display: tickets, bump, recall", "[net][kitchen]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"classic-burger"_s);
    pos.addItem(u"medium"_s);
    pos.addItem(u"draft-beer"_s);
    CHECK(pos.kitchenTickets().isEmpty());          // nothing sent yet
    REQUIRE(pos.sendOrder());
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());

    QVariantList tickets = pos.kitchenTickets();
    REQUIRE(tickets.size() == 2);                    // one per send
    const QVariantMap first = tickets[0].toMap();
    const QVariantList lines = first[u"lines"_s].toList();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].toMap()[u"name"_s].toString() == u"Classic Burger"_s);
    CHECK(lines[0].toMap()[u"modifiers"_s].toStringList() == QStringList{u"M"_s});
    CHECK(lines[1].toMap()[u"printer"_s].toString() == u"bar"_s);

    REQUIRE(pos.bumpTicket(first[u"checkId"_s].toLongLong(), first[u"sentAt"_s].toLongLong()));
    CHECK(pos.kitchenTickets().size() == 1);
    CHECK_FALSE(pos.bumpTicket(first[u"checkId"_s].toLongLong(), first[u"sentAt"_s].toLongLong()));
    REQUIRE(pos.recallTicket());
    CHECK(pos.kitchenTickets().size() == 2);

    // Paying does not take tickets off the screen; bumping does.
    pos.entryKey(u"0"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.tender(u"cash"_s, 100000));
    REQUIRE(pos.closeCheck());
    tickets = pos.kitchenTickets();
    CHECK(tickets.size() == 2);
    for (const QVariant &t : tickets)
        REQUIRE(pos.bumpTicket(t.toMap()[u"checkId"_s].toLongLong(), t.toMap()[u"sentAt"_s].toLongLong()));
    CHECK(pos.kitchenTickets().isEmpty());
    CHECK(sink.checks.begin()->second.lines[0].made);
}

TEST_CASE("Takeout and delivery carry customer details", "[net][customer]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.startCheck(core::CheckType::Delivery));
    CHECK(pos.checkInfo()[u"label"_s].toString().startsWith(u"Delivery"_s));
    REQUIRE(pos.setCustomer({{u"name"_s, u" Ana Ruiz "_s}, {u"phone"_s, u"555-0100"_s},
                             {u"address"_s, u"12 Oak St"_s}, {u"note"_s, u"Ring twice"_s}}));
    const QVariantMap customer = pos.checkInfo()[u"customer"_s].toMap();
    CHECK(customer[u"name"_s].toString() == u"Ana Ruiz"_s);   // trimmed
    CHECK(customer[u"address"_s].toString() == u"12 Oak St"_s);
    CHECK(pos.openChecks()[0].toMap()[u"customer"_s].toString() == u"Ana Ruiz"_s);

    pos.releaseCheck();
    CHECK_FALSE(pos.setCustomer({{u"name"_s, u"x"_s}}));
}

TEST_CASE("Remote terminal: full order flow through the server", "[net][remote]")
{
    Store store;
    Screen front(store.terminal(u"Front"_s));
    CHECK(store.server.terminalCount() == 1);
    CHECK(front.remote->terminalName() == u"Front"_s);
    CHECK(front.c.pageId() == u"login"_s);

    front.pin("1111");
    CHECK(front.remote->userName() == u"Sam"_s);
    CHECK(front.c.pageId() == u"tables"_s);

    front.c.selectTable(u"T2"_s);
    front.settle();
    CHECK(front.c.pageId() == u"guest-count"_s);
    front.remote->entryKey(u"3"_s);
    front.settle();
    front.tap(u"start"_s);                             // startCheck, then the menu
    CHECK(front.c.pageId() == u"index-lunch"_s);
    CHECK(front.remote->checkInfo()[u"guests"_s].toInt() == 3);

    front.tap(u"cat-items-burgers"_s);
    front.tap(u"item-1"_s);                            // Classic Burger -> its choices
    CHECK(front.c.pageId() == u"modifiers"_s);
    front.remote->chooseOption(u"temperature"_s, 1);   // Medium Rare
    front.settle();
    front.c.finishChoosing();
    front.settle();
    CHECK(front.c.pageId() == u"items-burgers"_s);
    REQUIRE(front.remote->lines().size() == 1);
    CHECK(front.remote->lines()[0].toMap()[u"modifiers"_s].toList().size() == 1);

    // The server holds the truth.
    REQUIRE(store.shared.open.size() == 1);
    CHECK(store.shared.open.begin()->second.label == "T2");

    front.tap(u"flow-send"_s);
    CHECK(front.remote->kitchenTickets().size() == 1);

    // A second terminal sees the table busy on Front.
    Screen bar(store.terminal(u"Bar"_s));
    bar.pin("2222");
    CHECK(bar.remote->tableStatus(u"T2"_s)[u"busyOn"_s].toString() == u"Front"_s);
    bar.c.selectTable(u"T2"_s);
    bar.settle();
    CHECK(bar.c.pageId() == u"tables"_s);
    CHECK(bar.c.statusText().contains(u"Front"_s));

    // Pay and close on Front.
    front.remote->entryKey(u"0"_s);
    front.settle();
    front.remote->openDrawerSession();
    front.settle();
    front.tap(u"flow-pay"_s);
    CHECK(front.c.pageId() == u"settle"_s);
    front.tap(u"tender-credit"_s);
    front.tap(u"close"_s);
    REQUIRE(waitFor([&] { return front.c.pageId() == u"tables"_s; }));
    CHECK(store.shared.open.empty());
    CHECK(store.shared.closedToday.size() == 1);
    CHECK(waitFor([&] { return bar.remote->openChecks().isEmpty(); }));
}

TEST_CASE("Remote terminal: reports and admin answers arrive asynchronously", "[net][remote]")
{
    Store store;
    Screen office(store.terminal(u"Office"_s));
    office.pin("1234");

    QSignalSpy answered(office.remote.get(), &app::PosSession::queriesChanged);
    const QVariantMap first = office.remote->report(u"sales"_s);
    CHECK(first.isEmpty());                            // not here yet
    REQUIRE(waitFor([&] { return !office.remote->report(u"sales"_s).isEmpty(); }));
    CHECK(office.remote->report(u"sales"_s)[u"title"_s].toString() == u"Sales Summary"_s);

    REQUIRE(waitFor([&] { return !office.remote->adminRecords(u"menu"_s).isEmpty(); }));
    const int items = int(office.remote->adminRecords(u"menu"_s).size());
    QVariantMap nachos = {{u"name"_s, u"Nachos"_s}, {u"price"_s, 7.5}, {u"taxClass"_s, u"food"_s}};
    office.remote->adminSave(u"menu"_s, -1, nachos);
    REQUIRE(waitFor([&] { return office.remote->adminRecords(u"menu"_s).size() == items + 1; }));
    CHECK(store.shared.menu.back().name == "Nachos");
}

TEST_CASE("Page edits on one terminal reach the others", "[net][remote]")
{
    Store store;
    Screen a(store.terminal(u"A"_s));
    Screen b(store.terminal(u"B"_s));
    a.pin("1234");
    QSignalSpy bGot(b.remote.get(), &net::RemoteSession::layoutReceived);

    a.c.enterEditMode();
    const QString page = a.c.pageId();
    a.c.editor()->addZone(u"button"_s);
    REQUIRE(a.c.saveEdits());
    REQUIRE(waitFor([&] { return bGot.size() == 1; }));
    CHECK(store.hub.layout().page(page)->zone(u"button"_s));
    CHECK(b.c.layout().page(page)->zone(u"button"_s));

    // Saving needs the layout.edit permission on the server too.
    b.pin("1111");
    QSignalSpy saved(b.remote.get(), &net::RemoteSession::layoutSaved);
    b.remote->saveLayout(b.c.layout());
    REQUIRE(waitFor([&] { return saved.size() == 1; }));
    CHECK_FALSE(saved[0][0].toBool());
}

TEST_CASE("Remote terminal reconnects and starts at the login page", "[net][remote]")
{
    auto store = std::make_unique<Store>();
    const quint16 port = store->server.port();
    Screen t(store->terminal(u"Front"_s));
    t.pin("1111");
    CHECK(t.c.pageId() == u"tables"_s);
    const auto pairedDevices = store->shared.settings.terminals;

    // Server goes away: the terminal says so and keeps trying.
    store.reset();
    REQUIRE(waitFor([&] { return !t.remote->online(); }));
    QSignalSpy notices(t.remote.get(), &app::PosSession::notice);
    t.c.selectTable(u"T1"_s);
    t.settle();
    CHECK_FALSE(notices.isEmpty());

    // The server restarts on the same port (its paired devices are saved):
    // back online, logged out.
    test::RecordingSink sink;
    PosShared shared(test::seedPosData(), &sink);
    shared.settings.terminals = pairedDevices;
    net::LayoutHub hub(seedLayout());
    net::PosServer server(&shared, &hub);
    REQUIRE(server.listen(QHostAddress::LocalHost, port));
    REQUIRE(waitFor([&] { return t.remote->online(); }, 8000));
    CHECK_FALSE(t.remote->loggedIn());
    CHECK(waitFor([&] { return t.c.pageId() == u"login"_s; }));
}

TEST_CASE("Kitchen display: a station bumps only its own lines", "[net][kitchen]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"cobb"_s);          // kitchen
    pos.addItem(u"house-wine"_s);    // bar
    REQUIRE(pos.sendOrder());
    const QVariantMap t = pos.kitchenTickets()[0].toMap();

    REQUIRE(pos.bumpTicket(t[u"checkId"_s].toLongLong(), t[u"sentAt"_s].toLongLong(), u"bar"_s));
    QVariantList left = pos.kitchenTickets();
    REQUIRE(left.size() == 1);
    REQUIRE(left[0].toMap()[u"lines"_s].toList().size() == 1);
    CHECK(left[0].toMap()[u"lines"_s].toList()[0].toMap()[u"name"_s].toString() == u"Cobb"_s);

    REQUIRE(pos.recallTicket());     // brings the wine back only
    CHECK(pos.kitchenTickets()[0].toMap()[u"lines"_s].toList().size() == 2);
}

TEST_CASE("An empty takeout that is put away is discarded", "[net][customer]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.releaseCheck();
    CHECK(pos.openChecks().isEmpty());
    CHECK(sink.checks.begin()->second.status == core::CheckStatus::Discarded);

    // A table keeps its (still empty) check.
    pos.selectTable(u"T1"_s);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.releaseCheck();
    CHECK(pos.openChecks().size() == 1);
}
