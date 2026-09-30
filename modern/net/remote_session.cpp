#include "net/remote_session.hh"

#include "net/protocol.hh"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QSslPreSharedKeyAuthenticator>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcRemote, "vt.remote")

namespace vt::net {

namespace {

constexpr int kReconnectMs = 2000;

// Which change signal each state key belongs to.
enum class Group { Session, Admin, Entry, Qualifier, Check, OpenChecks, Kitchen, Drawer, Day, None };

Group groupOf(const QString &key)
{
    static const QHash<QString, Group> groups = {
        {u"loggedIn"_s, Group::Session}, {u"userName"_s, Group::Session}, {u"userRole"_s, Group::Session},
        {u"permissions"_s, Group::Session}, {u"clockedIn"_s, Group::Session}, {u"clockedInSince"_s, Group::Session},
        {u"storeName"_s, Group::Admin}, {u"currencySymbol"_s, Group::Admin}, {u"adminRevision"_s, Group::Admin},
        {u"mealPeriods"_s, Group::Admin}, {u"pairing"_s, Group::Admin},
        {u"pinLength"_s, Group::Entry}, {u"entry"_s, Group::Entry}, {u"entryAmount"_s, Group::Entry},
        {u"entryGuests"_s, Group::Entry}, {u"textEntry"_s, Group::Entry},
        {u"pendingQualifier"_s, Group::Qualifier},
        {u"pendingTable"_s, Group::Check}, {u"hasCheck"_s, Group::Check}, {u"check"_s, Group::Check},
        {u"lines"_s, Group::Check}, {u"totals"_s, Group::Check}, {u"payments"_s, Group::Check},
        {u"selectedLine"_s, Group::Check}, {u"selectedPayment"_s, Group::Check},
        {u"openChecks"_s, Group::OpenChecks}, {u"checkFilter"_s, Group::OpenChecks},
        {u"kitchenTickets"_s, Group::Kitchen}, {u"drawer"_s, Group::Drawer},
        {u"day"_s, Group::Day}, {u"days"_s, Group::Day}, {u"tipsOwed"_s, Group::Day},
    };
    return groups.value(key, Group::None);
}

} // namespace

RemoteSession::RemoteSession(QString terminalName, QObject *parent)
    : PosSession(parent)
    , terminal_(std::move(terminalName))
    , channel_(std::make_unique<LineChannel>(&socket_))
{
    socket_.setSslConfiguration(tlsConfiguration());
    connect(&socket_, &QSslSocket::preSharedKeyAuthenticationRequired, this, [this](QSslPreSharedKeyAuthenticator *a) {
        a->setIdentity(deviceIdentity(credentials_.terminalId));
        a->setPreSharedKey(credentials_.key);
    });
    connect(&socket_, &QSslSocket::encrypted, this, &RemoteSession::onConnected);
    connect(&socket_, &QSslSocket::readyRead, this, &RemoteSession::onReadyRead);
    connect(&socket_, &QSslSocket::disconnected, this, &RemoteSession::onDisconnected);
    connect(&socket_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        // A refused key shows as a failed handshake. Twice in a row counts
        // (once could be a server restarting mid-handshake).
        const bool refused = e == QAbstractSocket::SslHandshakeFailedError
                             || (e == QAbstractSocket::RemoteHostClosedError && !encrypted_ && !welcomed_);
        if (refused && !welcomed_ && ++refusals_ >= 2) {
            qCWarning(lcRemote) << "the server refused this device's key";
            rejected_ = true;
            reconnect_.stop();
            emit notice(tr("This terminal is not paired with the server. Pair it again."));
            emit rejected();
            return;
        }
        // Can't reach the server: every third try, look for it on the
        // network in case its address changed.
        if (!welcomed_ && ++failures_ % 3 == 0 && !credentials_.serverId.isEmpty())
            finder_.search(discoveryPort_);
        if (!welcomed_ && !reconnect_.isActive())
            reconnect_.start();
    });
    connect(&finder_, &ServerFinder::found, this, [this](const FoundServer &s) {
        // The server answers once per network it hears on: act on the first
        // answer, and not while that attempt is still under way.
        if (welcomed_ || s.id != credentials_.serverId || (s.host == host_ && s.port == port_)
            || socket_.state() != QAbstractSocket::UnconnectedState)
            return;
        qCInfo(lcRemote).noquote() << "the server moved to" << s.host;
        host_ = credentials_.host = s.host;
        port_ = credentials_.port = s.port;
        emit credentialsChanged(credentials_);
        reconnect_.stop();
        encrypted_ = false;
        socket_.connectToHostEncrypted(host_, port_);
    });
    reconnect_.setSingleShot(true);
    reconnect_.setInterval(kReconnectMs);
    connect(&reconnect_, &QTimer::timeout, this, [this] {
        if (socket_.state() == QAbstractSocket::UnconnectedState && !rejected_) {
            encrypted_ = false;
            socket_.connectToHostEncrypted(host_, port_);
        }
    });
}

RemoteSession::~RemoteSession()
{
    socket_.disconnect(this);
    socket_.abort();
}

void RemoteSession::setCredentials(const Credentials &credentials)
{
    credentials_ = credentials;
    if (!credentials.terminalName.isEmpty())
        terminal_ = credentials.terminalName;
}

void RemoteSession::connectTo(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
    rejected_ = false;
    encrypted_ = false;
    socket_.connectToHostEncrypted(host_, port_);
}

bool RemoteSession::waitForWelcome(int msec)
{
    if (welcomed_)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &PosSession::onlineChanged, &loop, &QEventLoop::quit);
    connect(this, &RemoteSession::rejected, &loop, &QEventLoop::quit);
    timeout.start(msec);
    loop.exec();
    return welcomed_;
}

void RemoteSession::onConnected()
{
    encrypted_ = true;
    send({{u"t"_s, u"hello"_s}, {u"terminal"_s, terminal_}, {u"protocol"_s, ProtocolVersion}});
}

void RemoteSession::onReadyRead()
{
    bool overflow = false;
    for (const QJsonObject &m : channel_->receive(&overflow))
        handle(m);
    if (overflow)
        socket_.abort();
}

void RemoteSession::onDisconnected()
{
    const bool was = welcomed_;
    welcomed_ = false;
    // Nothing pending will be answered now.
    const auto pending = std::exchange(replies_, {});
    for (const Reply &r : pending) {
        if (r)
            r(QVariant(false));
    }
    if (was) {
        qCWarning(lcRemote) << "lost the server; reconnecting";
        emit onlineChanged();
        emit notice(tr("Lost the connection to the server. Reconnecting…"));
    }
    encrypted_ = false;
    if (!rejected_)
        reconnect_.start();
}

void RemoteSession::send(const QJsonObject &m)
{
    channel_->send(m);
}

void RemoteSession::handle(const QJsonObject &m)
{
    const QString type = m.value(u"t").toString();
    if (type == u"welcome") {
        if (auto l = layout::Layout::fromJson(m.value(u"layout").toObject())) {
            layout_ = *l;
            emit layoutReceived(layout_);
        }
        applyState(m.value(u"state").toObject(), true);
        cache_.clear();
        welcomed_ = true;
        refusals_ = 0;
        failures_ = 0;
        emit onlineChanged();
    } else if (type == u"state") {
        applyState(m.value(u"set").toObject(), false);
    } else if (type == u"reply") {
        const Reply r = replies_.take(m.value(u"id").toInteger());
        if (r)
            r(m.value(u"r").toVariant());
    } else if (type == u"notice") {
        emit notice(m.value(u"text").toString());
    } else if (type == u"event") {
        if (m.value(u"e").toString() == u"checkClosed")
            emit checkClosed(m.value(u"v").toInteger());
    } else if (type == u"layout") {
        if (auto l = layout::Layout::fromJson(m.value(u"layout").toObject())) {
            layout_ = *l;
            emit layoutReceived(layout_);
        }
    } else if (type == u"error") {
        emit notice(m.value(u"text").toString());
    }
}

void RemoteSession::applyState(const QJsonObject &set, bool replaceAll)
{
    const bool wasLoggedIn = loggedIn();
    QSet<Group> changed;
    if (replaceAll) {
        state_.clear();
        for (int g = 0; g < int(Group::None); ++g)
            changed.insert(Group(g));
    }
    for (auto it = set.begin(); it != set.end(); ++it) {
        state_.insert(it.key(), it.value().toVariant());
        changed.insert(groupOf(it.key()));
    }

    // Live reports follow sales; admin answers follow admin changes.
    const bool salesMoved = changed.contains(Group::OpenChecks) || changed.contains(Group::Day)
                            || changed.contains(Group::Drawer);
    bool invalidated = false;
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if ((salesMoved && it.key().startsWith(u"report|")) ||
            (changed.contains(Group::Admin) && (it.key().startsWith(u"adminRecords|") || it.key().startsWith(u"adminFields|")))) {
            it->stale = true;
            invalidated = true;
        }
    }

    if (changed.contains(Group::Session)) emit sessionChanged();
    if (changed.contains(Group::Admin)) emit adminChanged();
    if (changed.contains(Group::Entry)) emit entryChanged();
    if (changed.contains(Group::Qualifier)) emit qualifierChanged();
    if (changed.contains(Group::Check)) emit checkChanged();
    if (changed.contains(Group::OpenChecks)) emit openChecksChanged();
    if (changed.contains(Group::Kitchen)) emit kitchenChanged();
    if (changed.contains(Group::Drawer)) emit drawerChanged();
    if (changed.contains(Group::Day)) emit dayChanged();
    if (invalidated) {
        ++queryRevision_;
        emit queriesChanged();
    }
    if (loggedIn() != wasLoggedIn)
        emit loggedInChanged(loggedIn());
}

void RemoteSession::invoke(const QString &method, const QVariantList &args, Reply reply)
{
    if (!welcomed_) {
        emit notice(tr("Not connected to the server."));
        if (reply)
            reply(QVariant(false));
        return;
    }
    const qint64 id = nextId_++;
    if (reply)
        replies_.insert(id, std::move(reply));
    send({{u"t"_s, u"call"_s}, {u"id"_s, id}, {u"m"_s, method}, {u"a"_s, QJsonArray::fromVariantList(args)}});
}

void RemoteSession::saveLayout(const layout::Layout &layout)
{
    if (!welcomed_) {
        emit layoutSaved(false, tr("Not connected to the server."));
        return;
    }
    const qint64 id = nextId_++;
    replies_.insert(id, [this, layout](const QVariant &r) {
        const QVariantMap m = r.toMap();
        const bool ok = m.value(u"ok"_s).toBool();
        if (ok)
            layout_ = layout;
        emit layoutSaved(ok, m.value(u"error"_s).toString());
    });
    send({{u"t"_s, u"saveLayout"_s}, {u"id"_s, id}, {u"layout"_s, layout.toJson()}});
}

QVariant RemoteSession::query(const QString &key, const QString &method, const QVariantList &args)
{
    Cached &c = cache_[key];
    if ((c.stale || !c.value.isValid()) && !c.inFlight && welcomed_) {
        c.inFlight = true;
        invoke(method, args, [this, key](const QVariant &r) {
            Cached &entry = cache_[key];
            entry.inFlight = false;
            entry.stale = false;
            entry.value = r;
            ++queryRevision_;
            emit queriesChanged();
        });
    }
    return c.value;
}

QVariantMap RemoteSession::report(const QString &id, qint64 dayId)
{
    return query(u"report|%1|%2"_s.arg(id).arg(dayId), u"report"_s, {id, dayId}).toMap();
}

QVariantList RemoteSession::adminFields(const QString &panel)
{
    return query(u"adminFields|"_s + panel, u"adminFields"_s, {panel}).toList();
}

QVariantList RemoteSession::adminRecords(const QString &panel)
{
    return query(u"adminRecords|"_s + panel, u"adminRecords"_s, {panel}).toList();
}

QVariantMap RemoteSession::adminNewRecord(const QString &panel)
{
    return query(u"adminNewRecord|"_s + panel, u"adminNewRecord"_s, {panel}).toMap();
}

} // namespace vt::net
