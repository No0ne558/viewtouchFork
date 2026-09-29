#include "app/pos_json.hh"
#include "layoutcontroller.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/protocol.hh"
#include "net/remote_session.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "storage/async_writer.hh"
#include "storage/layout_store.hh"
#include "storage/pos_store.hh"

#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ViewTouchPlugin)

using namespace Qt::StringLiterals;

namespace {

struct Options {
    QCommandLineOption db{u"db"_s, u"SQLite database (default: <app data>/viewtouch.db)."_s, u"file"_s};
    QCommandLineOption layout{u"layout"_s,
        u"Show pages from <dir> instead of the database. Saving in the editor still writes to the database."_s, u"dir"_s};
    QCommandLineOption resetLayout{u"reset-layout"_s, u"Replace the saved pages with the built-in starter pages."_s};
    QCommandLineOption resetMenu{u"reset-menu"_s,
        u"Replace the menu, employees and settings with the built-in starter data (checks are kept)."_s};
    QCommandLineOption serve{u"serve"_s, u"Also serve remote terminals."_s};
    QCommandLineOption port{u"port"_s, u"Port to serve terminals on (default %1)."_s.arg(vt::net::DefaultPort), u"port"_s};
    QCommandLineOption listen{u"listen"_s, u"Address to serve on (default: all)."_s, u"address"_s};
    QCommandLineOption headless{u"headless"_s, u"Serve without a screen of its own (use with --serve)."_s};
    QCommandLineOption connect{u"connect"_s, u"Run as a terminal of the server at <host[:port]>."_s, u"host"_s};
    QCommandLineOption terminal{u"terminal"_s, u"This terminal's name (default: the computer's name)."_s, u"name"_s};
    QCommandLineOption login{u"login"_s, u"Log in with <pin> at startup (testing)."_s, u"pin"_s};
    QCommandLineOption page{u"page"_s, u"Open page <id> at startup."_s, u"id"_s};
    QCommandLineOption edit{u"edit"_s, u"Start in edit mode."_s};
    QCommandLineOption select{u"select"_s, u"In edit mode, select these zones (comma separated)."_s, u"ids"_s};
    QCommandLineOption size{u"size"_s, u"Window size, e.g. 1280x720."_s, u"WxH"_s, u"1280x720"_s};
    QCommandLineOption screenshot{u"screenshot"_s, u"Render, save a PNG to <file>, and exit."_s, u"file"_s};
};

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void say(vt::app::PosSession &pos, const QString &text)
{
    emit pos.notice(text);
}

// Wait (running events) until `condition` holds or `msec` pass.
template <typename Condition>
bool waitUntil(Condition condition, int msec)
{
    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(20);
    timeout.start(msec);
    if (!condition())
        loop.exec();
    return condition();
}

// Startup page/edit options, then the window. Returns the engine (null when
// the window could not be created).
std::unique_ptr<QQmlApplicationEngine> showUi(QCommandLineParser &cli, const Options &o, LayoutController &controller)
{
    if (cli.isSet(o.page) && !controller.showPage(cli.value(o.page)))
        qWarning().noquote() << "Cannot open page" << cli.value(o.page);
    if (cli.isSet(o.edit)) {
        controller.enterEditMode();
        if (cli.isSet(o.select))
            controller.editor()->selectOnly(cli.value(o.select).split(u','));
    }

    const QStringList size = cli.value(o.size).split(u'x');
    const int width = size.value(0).toInt();
    const int height = size.value(1).toInt();
    auto engine = std::make_unique<QQmlApplicationEngine>();
    QObject::connect(engine.get(), &QQmlApplicationEngine::objectCreationFailed,
                     qApp, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine->setInitialProperties({
        {u"controller"_s, QVariant::fromValue(&controller)},
        {u"width"_s, width > 0 ? width : 1280},
        {u"height"_s, height > 0 ? height : 720},
    });
    engine->loadFromModule("ViewTouch", "Main");

    auto *window = engine->rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    if (!window)
        return nullptr;
    if (cli.isSet(o.screenshot)) {
        const QString file = cli.value(o.screenshot);
        QTimer::singleShot(1000, window, [window, file] {
            const bool ok = window->grabWindow().save(file);
            if (!ok)
                qCritical().noquote() << "Could not write" << file;
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return engine;
}

// --- a terminal of a remote server ---------------------------------------------------------

int runTerminal(QCommandLineParser &cli, const Options &o)
{
    QString host = cli.value(o.connect);
    quint16 port = vt::net::DefaultPort;
    if (const qsizetype colon = host.lastIndexOf(u':'); colon > 0) {
        port = quint16(host.mid(colon + 1).toUInt());
        host = host.left(colon);
    }
    const QString name = cli.isSet(o.terminal) ? cli.value(o.terminal) : QSysInfo::machineHostName();

    vt::net::RemoteSession remote(name);
    remote.connectTo(host, port);
    qInfo().noquote() << "Connecting to" << host << "port" << port << "as" << name << "...";
    if (!remote.waitForWelcome(15000)) {
        qCritical().noquote() << "No ViewTouch server answered at" << host << "port" << port;
        return 1;
    }

    LayoutController controller(remote.layout());
    controller.setPos(&remote);
    // Page edits are saved on the server, which passes them on to everyone.
    controller.setSaver([&remote](const vt::layout::Layout &layout, QString *) {
        remote.saveLayout(layout);
        return true;
    });
    QObject::connect(&remote, &vt::net::RemoteSession::layoutSaved, &controller, [&remote](bool ok, const QString &error) {
        say(remote, ok ? QCoreApplication::translate("main", "Pages saved on the server")
                       : QCoreApplication::translate("main", "The server did not save the pages: %1").arg(error));
    });
    QObject::connect(&remote, &vt::net::RemoteSession::layoutReceived, &controller,
                     [&controller](const vt::layout::Layout &layout) { controller.replaceLayout(layout); });

    if (cli.isSet(o.login)) {
        remote.invoke(u"loginWithPin"_s, {cli.value(o.login)});
        waitUntil([&] { return remote.loggedIn(); }, 5000);
    }
    const auto engine = showUi(cli, o, controller);
    return engine ? qApp->exec() : 1;
}

// --- this machine holds the data (standalone, or serving terminals) -----------------------

int runStore(QCommandLineParser &cli, const Options &o)
{
    const QString dbPath = cli.isSet(o.db) ? cli.value(o.db) : QDir(dataDir()).filePath(u"viewtouch.db"_s);

    vt::storage::LayoutStore store(dbPath);
    QString dbError;
    const bool haveStore = store.open(&dbError);
    if (!haveStore)
        qWarning().noquote() << "Pages will not be saved; cannot open database" << store.path() << ":" << dbError;

    // Pages: --layout dir, else the database, else the built-in seed.
    QStringList errors;
    std::optional<vt::layout::Layout> layout;
    if (cli.isSet(o.layout)) {
        layout = vt::layout::Layout::loadDirectory(cli.value(o.layout), &errors);
    } else if (haveStore && store.hasLayout() && !cli.isSet(o.resetLayout)) {
        layout = store.load(&errors);
    }
    if (!layout && !cli.isSet(o.layout)) {
        layout = vt::layout::Layout::loadDirectory(u":/seed"_s, &errors);
        if (layout && haveStore) {
            QString error;
            if (!store.save(*layout, &error))
                qWarning().noquote() << "Could not store starter pages:" << error;
        }
    }
    for (const QString &e : std::as_const(errors))
        qWarning().noquote() << "layout:" << e;
    if (!layout) {
        qCritical().noquote() << "Could not load any pages.";
        return 1;
    }
    for (const QString &issue : layout->validate())
        qWarning().noquote() << "layout issue:" << issue;

    // POS data: same database; writes go through a background thread.
    vt::storage::PosStore posStore(dbPath);
    QString posError;
    const bool havePosStore = posStore.open(&posError);
    if (!havePosStore)
        qWarning().noquote() << "Sales will not be saved; cannot open database:" << posError;

    auto readSeed = [](const QString &name) {
        QFile f(u":/seed/pos/"_s + name);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()) : QJsonDocument();
    };
    vt::app::PosData seedData;
    seedData.settings = vt::app::settingsFromJson(readSeed(u"settings.json"_s).object());
    seedData.menu = vt::app::menuFromJson(readSeed(u"menu.json"_s).array());
    seedData.employees = vt::app::employeesFromJson(readSeed(u"employees.json"_s).array());

    std::optional<vt::app::PosData> posData;
    if (havePosStore) {
        if (!posStore.hasMenu() || cli.isSet(o.resetMenu)) {
            QString error;
            if (!posStore.seed(seedData.settings, seedData.menu, seedData.employees, &error))
                qWarning().noquote() << "Could not store starter menu:" << error;
        }
        QStringList posErrors;
        posData = posStore.load(&posErrors);
        for (const QString &e : std::as_const(posErrors))
            qWarning().noquote() << "pos:" << e;
    }
    if (!posData)
        posData = seedData;

    std::unique_ptr<vt::storage::AsyncWriter> writer;
    std::unique_ptr<vt::storage::SqlPosSink> sink;
    if (havePosStore) {
        writer = std::make_unique<vt::storage::AsyncWriter>(dbPath);
        sink = std::make_unique<vt::storage::SqlPosSink>(*writer);
    }
    vt::app::PosService pos(std::move(*posData), sink.get());
    vt::app::PosShared *shared = pos.shared();

    // Printing: a worker thread delivers tickets; "file" printers write under
    // <app data>/printouts so tickets are visible without hardware.
    vt::print::PrintSpooler spooler;
    vt::print::TicketPrinter ticketPrinter(spooler, QDir(dataDir()).filePath(u"printouts"_s));
    pos.setPrinter(&ticketPrinter);
    QObject::connect(&spooler, &vt::print::PrintSpooler::jobFailed, &pos,
                     [&pos](const QString &printer, const QString &what, const QString &error) {
        say(pos, QCoreApplication::translate("main", "%1 did not print on %2: %3").arg(what, printer, error));
    });
    if (writer) {
        QObject::connect(writer.get(), &vt::storage::AsyncWriter::writeFailed, &pos, [&pos](const QString &error) {
            say(pos, QCoreApplication::translate("main", "Could not save to the database (will retry): %1").arg(error));
        });
    }

    // Every save of the pages goes through the hub, which tells the terminals.
    vt::net::LayoutHub hub(*layout, haveStore ? &store : nullptr);
    std::unique_ptr<vt::net::PosServer> server;
    if (cli.isSet(o.serve) || cli.isSet(o.headless)) {
        server = std::make_unique<vt::net::PosServer>(shared, &hub);
        const quint16 port = cli.isSet(o.port) ? quint16(cli.value(o.port).toUInt()) : vt::net::DefaultPort;
        const QHostAddress address = cli.isSet(o.listen) ? QHostAddress(cli.value(o.listen)) : QHostAddress::Any;
        if (!server->listen(address, port)) {
            qCritical().noquote() << "Cannot serve terminals on port" << port << ":" << server->errorString();
            return 1;
        }
        qInfo().noquote() << "Serving terminals on port" << server->port();
    }
    if (cli.isSet(o.headless))
        return qApp->exec();

    LayoutController controller(*layout);
    controller.setPos(&pos);
    controller.setSaver([&hub, &controller](const vt::layout::Layout &l, QString *error) {
        return hub.save(l, error, &controller);
    });
    QObject::connect(&hub, &vt::net::LayoutHub::layoutChanged, &controller,
                     [&controller](const vt::layout::Layout &l, const void *origin) {
        if (origin != &controller)
            controller.replaceLayout(l);
    });

    if (cli.isSet(o.login) && !pos.loginWithPin(cli.value(o.login))) {
        qCritical().noquote() << "That PIN is not recognized.";
        return 1;
    }
    const auto engine = showUi(cli, o, controller);
    return engine ? qApp->exec() : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"ViewTouch"_s);
    QGuiApplication::setOrganizationName(u"ViewTouch"_s);
    QQuickStyle::setStyle(u"Fusion"_s);   // editor chrome; POS pages draw themselves

    const Options o;
    QCommandLineParser cli;
    cli.setApplicationDescription(u"ViewTouch point of sale.\n\n"
        "One machine keeps the data (standalone, or --serve for other terminals);\n"
        "other terminals run with --connect <server>."_s);
    cli.addHelpOption();
    cli.addOptions({o.db, o.layout, o.resetLayout, o.resetMenu, o.serve, o.port, o.listen, o.headless, o.connect,
                    o.terminal, o.login, o.page, o.edit, o.select, o.size, o.screenshot});
    cli.process(app);

    return cli.isSet(o.connect) ? runTerminal(cli, o) : runStore(cli, o);
}
