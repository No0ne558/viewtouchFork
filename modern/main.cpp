#include "app/pos_json.hh"
#include "joincontroller.hh"
#include "layoutcontroller.hh"
#include "net/discovery.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/protocol.hh"
#include "net/remote_session.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "storage/async_writer.hh"
#include "storage/backup.hh"
#include "storage/layout_store.hh"
#include "storage/pos_store.hh"

#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

Q_IMPORT_QML_PLUGIN(ViewTouchPlugin)

using namespace Qt::StringLiterals;

namespace {

struct Options {
    QCommandLineOption config{u"config"_s,
        u"Read options from <file>: one \"option = value\" per line (\"serve = yes\" for switches)."_s, u"file"_s};
    QCommandLineOption dataDir{u"data-dir"_s,
        u"Where the database, backups and printouts live (default: %1)."_s.arg(defaultDataDir()), u"dir"_s};
    QCommandLineOption db{u"db"_s, u"SQLite database (default: <data dir>/viewtouch.db)."_s, u"file"_s};
    QCommandLineOption layout{u"layout"_s,
        u"Show pages from <dir> instead of the database. Saving in the editor still writes to the database."_s, u"dir"_s};
    QCommandLineOption resetLayout{u"reset-layout"_s, u"Replace the saved pages with the built-in starter pages."_s};
    QCommandLineOption resetMenu{u"reset-menu"_s,
        u"Replace the menu, employees and settings with the built-in starter data (checks are kept)."_s};
    QCommandLineOption serve{u"serve"_s, u"Also serve remote terminals."_s};
    QCommandLineOption port{u"port"_s, u"Port to serve terminals on (default %1)."_s.arg(vt::net::DefaultPort), u"port"_s};
    QCommandLineOption listen{u"listen"_s, u"Address to serve on (default: all)."_s, u"address"_s};
    QCommandLineOption headless{u"headless"_s, u"Serve without a screen of its own (use with --serve)."_s};
    QCommandLineOption connect{u"connect"_s,
        u"Run as a terminal of the server at <host[:port]>, or \"auto\": the one it paired with, else "
        "find and pair with one on screen."_s, u"host"_s};
    QCommandLineOption pair{u"pair"_s,
        u"Pair this terminal with the --connect server using <code> from Manager → Terminals."_s, u"code"_s};
    QCommandLineOption terminal{u"terminal"_s, u"This terminal's name (default: the computer's name)."_s, u"name"_s};
    QCommandLineOption login{u"login"_s, u"Log in with <pin> at startup (testing)."_s, u"pin"_s};
    QCommandLineOption page{u"page"_s, u"Open page <id> at startup."_s, u"id"_s};
    QCommandLineOption edit{u"edit"_s, u"Start in edit mode (with --login and a manager's PIN)."_s};
    QCommandLineOption select{u"select"_s, u"In edit mode, select these zones (comma separated)."_s, u"ids"_s};
    QCommandLineOption size{u"size"_s, u"Window size, e.g. 1280x720."_s, u"WxH"_s, u"1280x720"_s};
    QCommandLineOption screenshot{u"screenshot"_s, u"Render, save a PNG to <file>, and exit."_s, u"file"_s};
    QCommandLineOption kiosk{u"kiosk"_s, u"Full screen with no mouse pointer and no way out (touch screens)."_s};
    QCommandLineOption windowed{u"windowed"_s,
        u"Start in a window instead of full screen (also with --size). F11 switches either way."_s};
    QCommandLineOption backupDir{u"backup-dir"_s, u"Where backups go (default: <data dir>/backups)."_s, u"dir"_s};
    QCommandLineOption backupKeep{u"backup-keep"_s, u"Backups to keep (default 30; 0 = all)."_s, u"count"_s, u"30"_s};
    QCommandLineOption backupEvery{u"backup-every"_s,
        u"Hours between automatic backups (default 24; 0 = only at End of Day)."_s, u"hours"_s, u"24"_s};
    QCommandLineOption backup{u"backup"_s, u"Back up the database now (safe while ViewTouch runs) and exit."_s};
    QCommandLineOption pairingCode{u"pairing-code"_s,
        u"Ask the ViewTouch server running on this machine (same --data-dir) for a code to pair a "
        "terminal with, print it, and exit."_s};
    QCommandLineOption restore{u"restore"_s,
        u"Put backup <file> in place of the database and exit. ViewTouch must not be running."_s, u"file"_s};

    static QString defaultDataDir() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
};

// Command-line options, with defaults from a --config file. The command line
// wins over the file.
class Args {
public:
    explicit Args(const QCommandLineParser &cli) : cli_(cli) {}

    bool loadConfig(const QString &file, const QList<QCommandLineOption> &known, QString *error)
    {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *error = u"Cannot read %1: %2"_s.arg(file, f.errorString());
            return false;
        }
        QStringList names;
        for (const QCommandLineOption &o : known)
            names += o.names();
        int lineNo = 0;
        while (!f.atEnd()) {
            ++lineNo;
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u';') || line.startsWith(u'['))
                continue;
            const qsizetype eq = line.indexOf(u'=');
            const QString key = (eq < 0 ? line : line.left(eq)).trimmed();
            QString value = eq < 0 ? u"yes"_s : line.mid(eq + 1).trimmed();
            if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"'))
                value = value.mid(1, value.size() - 2);
            if (!names.contains(key) || key == u"config") {
                *error = u"%1 line %2: unknown option \"%3\""_s.arg(file).arg(lineNo).arg(key);
                return false;
            }
            config_.insert(key, value);
        }
        return true;
    }

    bool isSet(const QCommandLineOption &o) const
    {
        if (cli_.isSet(o))
            return true;
        const QString name = o.names().constFirst();
        if (!config_.contains(name))
            return false;
        const QString v = config_.value(name).toLower();
        if (o.valueName().isEmpty())   // a switch
            return !(v == u"no" || v == u"false" || v == u"off" || v == u"0");
        return !v.isEmpty();
    }

    QString value(const QCommandLineOption &o) const
    {
        if (cli_.isSet(o))
            return cli_.value(o);
        const QString v = config_.value(o.names().constFirst());
        return !v.isEmpty() ? v : o.defaultValues().value(0);
    }

private:
    const QCommandLineParser &cli_;
    QHash<QString, QString> config_;
};

QString dataDirOf(const Args &cli, const Options &o)
{
    return cli.isSet(o.dataDir) ? cli.value(o.dataDir) : Options::defaultDataDir();
}

QString dbPathOf(const Args &cli, const Options &o)
{
    return cli.isSet(o.db) ? cli.value(o.db) : QDir(dataDirOf(cli, o)).filePath(u"viewtouch.db"_s);
}

// Where a running server listens for commands from this machine: in its data
// folder, or (socket paths are limited to ~100 bytes) a short name for it in
// the runtime folder.
QString controlSocketOf(const Args &cli, const Options &o)
{
    const QString dir = QDir(dataDirOf(cli, o)).absolutePath();
    const QString inData = QDir(dir).filePath(u"control.sock"_s);
    if (QFile::encodeName(inData).size() < 100)
        return inData;
    const QByteArray id = QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
        .filePath(u"vtmodern-%1.sock"_s.arg(QString::fromLatin1(id)));
}

QString backupDirOf(const Args &cli, const Options &o)
{
    return cli.isSet(o.backupDir) ? cli.value(o.backupDir) : QDir(dataDirOf(cli, o)).filePath(u"backups"_s);
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

// Full screen unless asked for a window (or a window size, or a picture).
void present(QQuickWindow *window, const Args &cli, const Options &o)
{
    const bool inWindow = !cli.isSet(o.kiosk)
                          && (cli.isSet(o.windowed) || cli.isSet(o.size) || cli.isSet(o.screenshot));
    if (!inWindow)
        window->showFullScreen();
    if (cli.isSet(o.kiosk) && !QGuiApplication::overrideCursor())
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
}

// Startup page/edit options, then the window. Returns the engine (null when
// the window could not be created).
std::unique_ptr<QQmlApplicationEngine> showUi(const Args &cli, const Options &o, LayoutController &controller)
{
    if (cli.isSet(o.page) && !controller.showPage(cli.value(o.page)))
        qWarning().noquote() << "Cannot open page" << cli.value(o.page);
    if (cli.isSet(o.edit) && controller.requestEditMode()) {   // needs --login with a manager PIN
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
        {u"kiosk"_s, cli.isSet(o.kiosk)},
        {u"width"_s, width > 0 ? width : 1280},
        {u"height"_s, height > 0 ? height : 720},
    });
    engine->loadFromModule("ViewTouch", "Main");

    auto *window = engine->rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    if (!window)
        return nullptr;
    present(window, cli, o);
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

// The Join screen: find a store, pair with the manager's code. Nothing when
// the window is closed.
std::optional<vt::net::Credentials> joinStore(const Args &cli, const Options &o, const QString &address,
                                              const QString &problem)
{
    const QString name = cli.isSet(o.terminal) ? cli.value(o.terminal) : QSysInfo::machineHostName();
    JoinController join(name, address, problem);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"join"_s, QVariant::fromValue(&join)}, {u"kiosk"_s, cli.isSet(o.kiosk)}});
    engine.loadFromModule("ViewTouch", "JoinWindow");
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return std::nullopt;
    present(window, cli, o);
    if (cli.isSet(o.screenshot)) {
        const QString file = cli.value(o.screenshot);
        QTimer::singleShot(1500, window, [window, file] {
            window->grabWindow().save(file);
            QCoreApplication::exit(0);
        });
    }
    bool joined = false;
    QEventLoop loop;   // closing the window quits the application, and this loop
    QObject::connect(&join, &JoinController::joined, &loop, [&] { joined = true; loop.quit(); });
    loop.exec();
    return joined ? std::optional(join.credentials()) : std::nullopt;
}

constexpr int kRejoin = 75;   // qApp->exit code: the server refused this device

int runTerminal(const Args &cli, const Options &o)
{
    const QString dataDir = dataDirOf(cli, o);
    QDir().mkpath(dataDir);
    const QString credentialFile = QDir(dataDir).filePath(u"terminal.json"_s);
    const QString address = cli.value(o.connect) == u"auto" ? QString() : cli.value(o.connect);
    auto split = [](const QString &a) {
        QString host = a;
        quint16 port = vt::net::DefaultPort;
        if (const qsizetype colon = host.lastIndexOf(u':'); colon > 0) {
            port = quint16(host.mid(colon + 1).toUInt());
            host = host.left(colon);
        }
        return std::pair{host, port};
    };
    auto save = [&](const vt::net::Credentials &c) {
        QString error;
        if (!c.save(credentialFile, &error))
            qWarning().noquote() << "Could not save the pairing to" << credentialFile << ":" << error;
    };

    std::optional<vt::net::Credentials> creds = vt::net::Credentials::load(credentialFile);

    // --pair CODE: pair from the command line (setup scripts).
    if (cli.isSet(o.pair)) {
        if (address.isEmpty()) {
            qCritical().noquote() << "--pair needs --connect <server address>.";
            return 1;
        }
        const auto [host, port] = split(address);
        vt::net::Pairer pairer;
        bool ok = false;
        QString error;
        QEventLoop loop;
        QObject::connect(&pairer, &vt::net::Pairer::finished, &loop,
                         [&](bool success, const vt::net::Credentials &c, const QString &e) {
            ok = success;
            error = e;
            if (success)
                creds = c;
            loop.quit();
        });
        pairer.start(host, port, cli.value(o.pair),
                     cli.isSet(o.terminal) ? cli.value(o.terminal) : QSysInfo::machineHostName());
        loop.exec();
        if (!ok) {
            qCritical().noquote() << "Pairing failed:" << error;
            return 1;
        }
        save(*creds);
        qInfo().noquote() << "Paired with" << creds->serverName << "as" << creds->terminalName;
    }

    QString problem;
    for (;;) {
        if (!creds) {
            creds = joinStore(cli, o, address, problem);
            if (!creds)
                return 0;   // the Join window was closed
            save(*creds);
        }
        // An address on the command line wins over the saved one.
        if (!address.isEmpty())
            std::tie(creds->host, creds->port) = split(address);

        vt::net::RemoteSession remote(creds->terminalName);
        remote.setCredentials(*creds);
        QObject::connect(&remote, &vt::net::RemoteSession::credentialsChanged, &remote, save);
        remote.connectTo(creds->host, creds->port);
        qInfo().noquote() << "Connecting to" << creds->host << "port" << creds->port << "as" << creds->terminalName
                          << "...";
        if (!remote.waitForWelcome(15000)) {
            if (remote.isRejected()) {
                QFile::remove(credentialFile);
                problem = QCoreApplication::translate("main",
                    "%1 no longer accepts this terminal (it was removed in Manager → Terminals). Pair it again.")
                              .arg(creds->serverName.isEmpty() ? creds->host : creds->serverName);
                creds.reset();
                continue;
            }
            qCritical().noquote() << "No ViewTouch server answered at" << creds->host << "port" << creds->port;
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
        // Unpaired while running: back to the Join screen.
        QObject::connect(&remote, &vt::net::RemoteSession::rejected, qApp, [] { QCoreApplication::exit(kRejoin); });

        if (cli.isSet(o.login)) {
            remote.invoke(u"loginWithPin"_s, {cli.value(o.login)});
            waitUntil([&] { return remote.loggedIn(); }, 5000);
        }
        const auto engine = showUi(cli, o, controller);
        if (!engine)
            return 1;
        const int result = qApp->exec();
        if (result != kRejoin)
            return result;
        QFile::remove(credentialFile);
        problem = QCoreApplication::translate("main",
            "%1 no longer accepts this terminal (it was removed in Manager → Terminals). Pair it again.")
                      .arg(creds->serverName.isEmpty() ? creds->host : creds->serverName);
        creds.reset();
    }
}

// --- this machine holds the data (standalone, or serving terminals) -----------------------

int runStore(const Args &cli, const Options &o)
{
    const QString dbPath = dbPathOf(cli, o);
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    // One ViewTouch per database: a second one would split the sales.
    QLockFile lock(dbPath + u".lock"_s);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        qint64 pid = 0;
        QString host, app;
        lock.getLockInfo(&pid, &host, &app);
        qCritical().noquote() << u"The database %1 is already in use by another ViewTouch (process %2). "
                                 "Join it as a terminal with --connect instead."_s.arg(dbPath).arg(pid);
        return 1;
    }

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
    vt::print::TicketPrinter ticketPrinter(spooler, QDir(dataDirOf(cli, o)).filePath(u"printouts"_s));
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

    // Backups: every --backup-every hours and after each End of Day.
    std::unique_ptr<vt::storage::BackupScheduler> backups;
    if (havePosStore) {
        backups = std::make_unique<vt::storage::BackupScheduler>(
            dbPath, backupDirOf(cli, o), cli.value(o.backupKeep).toInt(), cli.value(o.backupEvery).toInt());
        QObject::connect(backups.get(), &vt::storage::BackupScheduler::finished, &pos,
                         [&pos](bool ok, const QString &, const QString &error) {
            if (!ok)
                say(pos, QCoreApplication::translate("main", "The database backup failed: %1").arg(error));
        });
        QObject::connect(shared, &vt::app::PosShared::dayChanged, backups.get(),
                         [shared, &backups, &writer, closedDays = shared->pastDays.size()]() mutable {
            if (shared->pastDays.size() > closedDays) {
                closedDays = shared->pastDays.size();
                writer->flush();   // the day's final writes go in the backup
                backups->backupNow();
            }
        });
        backups->start();
    }

    // Every save of the pages goes through the hub, which tells the terminals.
    vt::net::LayoutHub hub(*layout, haveStore ? &store : nullptr);
    std::unique_ptr<vt::net::PosServer> server;
    std::unique_ptr<vt::net::DiscoveryResponder> discovery;
    std::unique_ptr<QLocalServer> control;
    if (cli.isSet(o.serve) || cli.isSet(o.headless)) {
        server = std::make_unique<vt::net::PosServer>(shared, &hub);
        const quint16 port = cli.isSet(o.port) ? quint16(cli.value(o.port).toUInt()) : vt::net::DefaultPort;
        const QHostAddress address = cli.isSet(o.listen) ? QHostAddress(cli.value(o.listen)) : QHostAddress::Any;
        if (!server->listen(address, port)) {
            qCritical().noquote() << "Cannot serve terminals on port" << port << ":" << server->errorString();
            return 1;
        }
        qInfo().noquote() << "Serving terminals on port" << server->port();
        // Terminals looking for a server on the network find this one.
        discovery = std::make_unique<vt::net::DiscoveryResponder>(
            [shared] { return std::pair{QString::fromStdString(shared->settings.serverId),
                                        QString::fromStdString(shared->settings.storeName)}; },
            server->port());
        if (!discovery->listen(server->port()))
            qWarning().noquote() << "Terminals can't find this server by themselves (UDP port" << server->port()
                                 << "):" << discovery->errorString();
        // `vtmodern --pairing-code` on this machine: the first terminal of a
        // server without a screen gets paired this way.
        control = std::make_unique<QLocalServer>();
        control->setSocketOptions(QLocalServer::UserAccessOption);
        const QString controlPath = controlSocketOf(cli, o);
        QLocalServer::removeServer(controlPath);   // left over from a crash (we hold the lock)
        if (!control->listen(controlPath))
            qWarning().noquote() << "No local control socket:" << control->errorString();
        QObject::connect(control.get(), &QLocalServer::newConnection, control.get(), [&control, shared] {
            while (QLocalSocket *s = control->nextPendingConnection()) {
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
                QObject::connect(s, &QLocalSocket::readyRead, s, [s, shared] {
                    if (!s->canReadLine())
                        return;
                    if (s->readLine().trimmed() == "pair") {
                        const QString code = shared->startPairing();
                        const QString until = QLocale().toString(
                            QDateTime::fromMSecsSinceEpoch(shared->pairing->expires).time(), QLocale::ShortFormat);
                        qInfo().noquote() << "Pairing opened from this machine's command line";
                        s->write(QJsonDocument(QJsonObject{{u"code"_s, code}, {u"until"_s, until}})
                                     .toJson(QJsonDocument::Compact) + '\n');
                    }
                    s->disconnectFromServer();
                });
            }
        });
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

// --- commands to the server running on this machine ------------------------------------

int runPairingCode(const Args &cli, const Options &o)
{
    QLocalSocket socket;
    socket.connectToServer(controlSocketOf(cli, o));
    if (!socket.waitForConnected(3000)) {
        qCritical().noquote() << "No ViewTouch server is running with the data in" << dataDirOf(cli, o)
                              << "(it must run with --serve or --headless):" << socket.errorString();
        return 1;
    }
    socket.write("pair\n");
    socket.flush();
    QByteArray answer;
    while (!answer.contains('\n') && socket.waitForReadyRead(5000))
        answer += socket.readAll();
    const QJsonObject o2 = QJsonDocument::fromJson(answer.trimmed()).object();
    if (o2.value(u"code").toString().isEmpty()) {
        qCritical().noquote() << "The server did not give a code.";
        return 1;
    }
    qInfo().noquote() << u"Pairing code: %1  (one device, until %2)"_s.arg(o2.value(u"code").toString(),
                                                                          o2.value(u"until").toString());
    qInfo().noquote() << "Type it on the new terminal's Join screen.";
    return 0;
}

// --- backups from the command line ---------------------------------------------------------

int runBackup(const Args &cli, const Options &o)
{
    const QString dir = backupDirOf(cli, o);
    const QString target = QDir(dir).filePath(vt::storage::backupFileName(QDateTime::currentDateTime()));
    QString error;
    if (!QDir().mkpath(dir) || !vt::storage::backupDatabase(dbPathOf(cli, o), target, &error)) {
        qCritical().noquote() << "Backup failed:" << (error.isEmpty() ? u"cannot create "_s + dir : error);
        return 1;
    }
    vt::storage::pruneBackups(dir, cli.value(o.backupKeep).toInt());
    qInfo().noquote() << "Backed up to" << target;
    return 0;
}

int runRestore(const Args &cli, const Options &o)
{
    const QString dbPath = dbPathOf(cli, o);
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    QLockFile lock(dbPath + u".lock"_s);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        qCritical().noquote() << u"ViewTouch is running on %1. Stop it first "
                                 "(installed as a service: sudo systemctl stop vtmodern vtmodern-kiosk)."_s.arg(dbPath);
        return 1;
    }
    QString keptAs, error;
    if (!vt::storage::restoreDatabase(cli.value(o.restore), dbPath, &keptAs, &error)) {
        qCritical().noquote() << "Restore failed:" << error;
        return 1;
    }
    qInfo().noquote() << "Restored" << cli.value(o.restore) << "to" << dbPath;
    if (!keptAs.isEmpty())
        qInfo().noquote() << "The previous database is kept as" << keptAs;
    return 0;
}

// Environment that must be set before the application starts:
// - a server without a screen (and --help, --backup...) needs no display, so
//   it gets the offscreen platform unless the user chose one;
// - a kiosk must exit when its compositor dies (systemd then restarts both)
//   instead of waiting to reconnect, as KDE asks Qt apps to.
void prepareEnvironment(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArrayView arg(argv[i]);
        const bool command = arg == "--backup" || arg == "--restore" || arg.startsWith("--restore=")
                             || arg == "--pairing-code" || arg == "-h"
                             || arg == "--help" || arg == "--help-all" || arg == "-v" || arg == "--version";
        // One-shot commands answer on the terminal, even through a pipe
        // (Qt would otherwise send the messages to the journal).
        if (command)
            qputenv("QT_FORCE_STDERR_LOGGING", "1");
        if ((command || arg == "--headless") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        if (arg == "--kiosk")
            qunsetenv("QT_WAYLAND_RECONNECT");
    }
}

#ifdef Q_OS_UNIX
// systemctl stop (SIGTERM), Ctrl+C and SIGHUP end the event loop normally, so
// queued database writes are flushed and the database lock is released. The
// handler only writes to a socket; the event loop does the rest.
int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const auto n = ::write(signalPipe[0], &byte, 1);
}

void quitOnSignals(QCoreApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(signalPipe[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier] {
        notifier->setEnabled(false);
        char byte;
        [[maybe_unused]] const auto n = ::read(signalPipe[1], &byte, 1);
        qInfo("Stopping");
        QCoreApplication::quit();
    });
    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(sig, &sa, nullptr);
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    prepareEnvironment(argc, argv);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationVersion(QStringLiteral(VTM_VERSION));
    QGuiApplication::setApplicationName(u"ViewTouch"_s);
    QGuiApplication::setOrganizationName(u"ViewTouch"_s);
    QQuickStyle::setStyle(u"Fusion"_s);   // editor chrome; POS pages draw themselves
#ifdef Q_OS_UNIX
    quitOnSignals(app);
#endif

    const Options o;
    QCommandLineParser cli;
    cli.setApplicationDescription(u"ViewTouch point of sale.\n\n"
        "One machine keeps the data (standalone, or --serve for other terminals);\n"
        "other terminals run with --connect <server>."_s);
    cli.addHelpOption();
    cli.addVersionOption();
    const QList<QCommandLineOption> all = {o.config, o.dataDir, o.db, o.layout, o.resetLayout, o.resetMenu, o.serve,
        o.port, o.listen, o.headless, o.connect, o.pair, o.terminal, o.kiosk, o.windowed, o.login, o.page, o.edit, o.select, o.size,
        o.screenshot, o.backupDir, o.backupKeep, o.backupEvery, o.backup, o.restore, o.pairingCode};
    cli.addOptions(all);
    cli.process(app);

    Args args(cli);
    if (cli.isSet(o.config)) {
        QString error;
        if (!args.loadConfig(cli.value(o.config), all, &error)) {
            qCritical().noquote() << error;
            return 1;
        }
    }
    if (args.isSet(o.backup))
        return runBackup(args, o);
    if (args.isSet(o.restore))
        return runRestore(args, o);
    if (args.isSet(o.pairingCode))
        return runPairingCode(args, o);
    return args.isSet(o.connect) ? runTerminal(args, o) : runStore(args, o);
}
