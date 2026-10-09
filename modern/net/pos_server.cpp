#include "net/pos_server.hh"

#include "app/pos_service.hh"
#include "net/layout_hub.hh"
#include "net/pairing.hh"
#include "net/protocol.hh"

#include <QDateTime>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QSslPreSharedKeyAuthenticator>
#include <QPointer>
#include <QSslSocket>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcServer, "vt.server")

namespace vt::net {

struct PosServer::Connection {
    QSslSocket *socket = nullptr;
    QString terminalId;   // the paired device, from its key
    QString pairingCode;  // or: a device pairing with this code
    std::unique_ptr<LineChannel> channel;
    std::unique_ptr<app::PosService> session;   // after hello
    bool standby = false;                       // the store's standby server (key "r:")
    bool synced = false;                        // ...has its copy, gets every change
    qint64 since = 0;
    QVariantMap sent;                           // last state sent
    QList<QJsonObject> events;                  // queued until the next flush
    QStringList notices;
    qint64 heard = 0;         // when it last sent anything
    bool answersPings = false;   // a terminal that says "pong" (older ones don't)
};

PosServer::PosServer(app::PosShared *shared, LayoutHub *layouts, QObject *parent)
    : QObject(parent)
    , shared_(shared)
    , layouts_(layouts)
{
    server_.setSslConfiguration(tlsConfiguration());
    connect(&server_, &QTcpServer::pendingConnectionAvailable, this, &PosServer::onNewConnection);
    connect(&server_, &QSslServer::preSharedKeyAuthenticationRequired, this, &PosServer::onPreSharedKey);
    connect(&server_, &QSslServer::errorOccurred, this, [](QSslSocket *socket, QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::SslHandshakeFailedError)
            qCInfo(lcServer).noquote() << "refused a device without a valid key from"
                                       << socket->peerAddress().toString();
    });
    connect(shared_, &app::PosShared::adminChanged, this, &PosServer::dropRevoked);
    // Terminals find this server again by its id after an address change;
    // the standby connects with the store's server key.
    if (shared_->settings.serverId.empty() || shared_->settings.replicaKey.empty()) {
        if (shared_->settings.serverId.empty())
            shared_->settings.serverId = newDeviceId().toStdString();
        if (shared_->settings.replicaKey.empty())
            shared_->settings.replicaKey = newDeviceKey().toBase64().toStdString();
        shared_->saveSettings();
    }
    // Every 3 s: the standby knows the main is alive, and each terminal
    // that it still has the store. A terminal that answers pings and then
    // says nothing for 15 s is gone (out of Wi-Fi range: its socket never
    // closes by itself), so the check it had open is free again.
    pingTimer_.setInterval(3'000);
    connect(&pingTimer_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        std::vector<Connection *> silent;
        for (auto &c : connections_) {
            if ((c->standby && c->synced) || c->session)
                c->channel->send({{u"t"_s, u"ping"_s}});
            if (c->session && c->answersPings && now - c->heard > silentMs_)
                silent.push_back(c.get());
        }
        for (Connection *c : silent) {
            qCWarning(lcServer) << "terminal stopped answering:" << c->session->terminalName();
            c->socket->abort();
            drop(c);
        }
    });
    pingTimer_.start();
    flushTimer_.setSingleShot(true);
    flushTimer_.setInterval(0);
    connect(&flushTimer_, &QTimer::timeout, this, [this] {
        for (auto &c : connections_)
            flush(c.get());
    });
    tickTimer_.setInterval(30'000);
    connect(&tickTimer_, &QTimer::timeout, this, &PosServer::scheduleFlushAll);
    tickTimer_.start();

    // Pages saved anywhere reach every other terminal.
    connect(layouts_, &LayoutHub::layoutChanged, this, [this](const layout::Layout &layout, const void *origin) {
        const QJsonObject msg{{u"t"_s, u"layout"_s}, {u"layout"_s, layout.toJson()}};
        for (auto &c : connections_) {
            if ((c->session && c.get() != origin) || (c->standby && c->synced))
                c->channel->send(msg);
        }
    });
}

PosServer::~PosServer()
{
    for (auto &c : connections_) {
        c->socket->disconnect(this);
        c->socket->abort();
    }
}

bool PosServer::listen(const QHostAddress &address, quint16 port)
{
    if (!tlsAvailable(&error_))
        return false;
    error_.clear();
    return server_.listen(address, port);
}

void PosServer::onPreSharedKey(QSslSocket *socket, QSslPreSharedKeyAuthenticator *auth)
{
    const QByteArray identity = auth->identity();
    QByteArray key;
    if (identity == PairingIdentity) {
        if (const app::PosShared::Pairing *p = shared_->activePairing()) {
            if (!pairingKeys_.contains(p->code))
                pairingKeys_.insert(p->code, pairingKey(p->code));
            key = pairingKeys_.value(p->code);
            socket->setProperty("vtPairingCode", p->code);
        }
    } else if (identity.startsWith("r:")
               && QString::fromLatin1(identity.mid(2)) == QString::fromStdString(shared_->settings.serverId)) {
        key = QByteArray::fromBase64(QByteArray::fromStdString(shared_->settings.replicaKey));
        socket->setProperty("vtStandby", true);
    } else if (identity.startsWith("t:")) {
        const QString id = QString::fromLatin1(identity.mid(2));
        if (const core::TerminalConfig *t = shared_->settings.pairedTerminal(id.toStdString())) {
            key = QByteArray::fromBase64(QByteArray::fromStdString(t->key));
            socket->setProperty("vtTerminalId", id);
        }
    }
    // Unknown device or no pairing open: a key nobody has, so the handshake fails.
    auth->setPreSharedKey(key.isEmpty() ? newDeviceKey() : key);
}

int PosServer::terminalCount() const
{
    return int(std::ranges::count_if(connections_, [](const auto &c) { return c->session != nullptr; }));
}

void PosServer::onNewConnection()
{
    while (auto *socket = qobject_cast<QSslSocket *>(server_.nextPendingConnection())) {
        auto c = std::make_unique<Connection>();
        c->socket = socket;
        c->terminalId = socket->property("vtTerminalId").toString();
        c->pairingCode = socket->property("vtPairingCode").toString();
        c->standby = socket->property("vtStandby").toBool();
        c->since = QDateTime::currentMSecsSinceEpoch();
        c->channel = std::make_unique<LineChannel>(socket);
        Connection *raw = c.get();
        connections_.push_back(std::move(c));
        connect(socket, &QTcpSocket::readyRead, this, [this, raw] { onReadyRead(raw); });
        connect(socket, &QTcpSocket::disconnected, this, [this, raw] { drop(raw); });
    }
}

void PosServer::onReadyRead(Connection *c)
{
    c->heard = QDateTime::currentMSecsSinceEpoch();
    bool overflow = false;
    const QList<QJsonObject> messages = c->channel->receive(&overflow);
    if (overflow) {
        qCWarning(lcServer) << "message too large; dropping terminal";
        c->socket->abort();
        return;
    }
    for (const QJsonObject &m : messages) {
        handle(c, m);
        // handle() may have dropped the connection.
        if (std::ranges::none_of(connections_, [c](const auto &x) { return x.get() == c; }))
            return;
    }
}

void PosServer::handle(Connection *c, const QJsonObject &m)
{
    const QString type = m.value(u"t").toString();

    if (!c->pairingCode.isEmpty()) {   // a device pairing: nothing else is allowed
        if (type == u"pair")
            pair(c, m);
        else
            c->socket->abort();
        return;
    }

    if (c->standby) {   // the store's standby: its copy, then every change
        if (type == u"hello" && !c->synced && snapshot_) {
            const QByteArray db = snapshot_();
            constexpr qsizetype chunk = 1 << 20;
            const int parts = int((db.size() + chunk - 1) / chunk);
            for (int i = 0; i < parts; ++i)
                c->channel->send({{u"t"_s, u"snap"_s}, {u"i"_s, i}, {u"n"_s, parts},
                                  {u"data"_s, QString::fromLatin1(db.mid(i * chunk, chunk).toBase64())}});
            c->channel->send({{u"t"_s, u"layout"_s}, {u"layout"_s, layouts_->layout().toJson()}});
            c->synced = true;
            qCInfo(lcServer) << "standby connected; sent the database," << db.size() << "bytes";
            emit standbyChanged();
        }
        return;
    }

    if (type == u"pong") {
        c->answersPings = true;
        return;
    }
    if (type == u"hello") {
        if (c->session)
            return;
        // The name the device was paired under, not what it says.
        const core::TerminalConfig *paired = shared_->settings.pairedTerminal(c->terminalId.toStdString());
        if (!paired) {
            c->socket->abort();
            return;
        }
        // Set up as a self-order kiosk or a time clock (vtmodern-setup): taken
        // when Manager -> Terminals has no screen for it yet.
        const std::string wanted = m.value(u"screen").toString().toStdString();
        if (paired->screen.empty() && (wanted == "selfOrder" || wanted == "timeClock"))
            for (core::TerminalConfig &t : shared_->settings.terminals)
                if (t.id == paired->id) {
                    t.screen = wanted;
                    shared_->saveSettings();
                    emit shared_->adminChanged();
                }
        const QString name = QString::fromStdString(paired->name);
        c->session = std::make_unique<app::PosService>(shared_, name);
        app::PosService *s = c->session.get();
        if (paired->screen == "selfOrder")   // Manager -> Terminals: a kiosk for guests
            s->enableSelfOrder();
        // Any change to this session's state is sent out on the next flush.
        for (auto signal : {&app::PosSession::sessionChanged, &app::PosSession::entryChanged,
                            &app::PosSession::qualifierChanged, &app::PosSession::checkChanged,
                            &app::PosSession::openChecksChanged, &app::PosSession::kitchenChanged,
                            &app::PosSession::drawerChanged, &app::PosSession::dayChanged,
                            &app::PosSession::adminChanged}) {
            connect(s, signal, this, &PosServer::scheduleFlushAll);
        }
        connect(s, &app::PosSession::notice, this, [c](const QString &text) { c->notices << text; });
        connect(s, &app::PosSession::checkClosed, this, [c](qint64 id) {
            c->events << QJsonObject{{u"t"_s, u"event"_s}, {u"e"_s, u"checkClosed"_s}, {u"v"_s, id}};
        });
        c->sent = s->snapshot();
        c->channel->send({{u"t"_s, u"welcome"_s}, {u"protocol"_s, ProtocolVersion},
                          {u"layout"_s, layouts_->layout().toJson()},
                          {u"state"_s, QJsonObject::fromVariantMap(c->sent)}});
        qCInfo(lcServer) << "terminal connected:" << name;
        emit terminalsChanged();
        return;
    }
    if (!c->session) {
        c->channel->send({{u"t"_s, u"error"_s}, {u"text"_s, u"say hello first"_s}});
        return;
    }

    if (type == u"call") {
        const qint64 id = m.value(u"id").toInteger();
        QVariant result;
        c->session->invoke(m.value(u"m").toString(), m.value(u"a").toArray().toVariantList(),
                           [&result](const QVariant &r) { result = r; });
        flush(c);   // state (and events) before the reply
        c->channel->send({{u"t"_s, u"reply"_s}, {u"id"_s, id}, {u"r"_s, QJsonValue::fromVariant(result)}});
        for (const QString &n : std::exchange(c->notices, {}))
            c->channel->send({{u"t"_s, u"notice"_s}, {u"text"_s, n}});
        scheduleFlushAll();
        return;
    }

    if (type == u"saveLayout") {
        const qint64 id = m.value(u"id").toInteger();
        QStringList errors;
        QString error;
        bool ok = false;
        if (!c->session->can(u"layout.edit"_s)) {
            error = tr("%1 may not edit pages.").arg(c->session->userName());
        } else if (auto layout = layout::Layout::fromJson(m.value(u"layout").toObject(), &errors)) {
            ok = layouts_->save(*layout, &error, c);
        } else {
            error = errors.join(u'\n');
        }
        c->channel->send({{u"t"_s, u"reply"_s}, {u"id"_s, id},
                          {u"r"_s, QJsonObject{{u"ok"_s, ok}, {u"error"_s, error}}}});
        return;
    }
}

void PosServer::pair(Connection *c, const QJsonObject &m)
{
    // The code must still be the open one: used once, and not after it expired.
    const app::PosShared::Pairing *open = shared_->activePairing();
    const QString name = m.value(u"name").toString().trimmed();
    if (!open || open->code != c->pairingCode || name.isEmpty()) {
        c->channel->send({{u"t"_s, u"error"_s},
                          {u"text"_s, name.isEmpty() ? tr("The terminal needs a name.")
                                                     : tr("That pairing code is no longer open.")}});
        c->socket->disconnectFromHost();
        return;
    }
    // A device paired under an existing terminal's name takes its place (a
    // replaced tablet keeps that terminal's printer and drawer settings).
    auto &list = shared_->settings.terminals;
    auto it = std::ranges::find_if(list, [&](const core::TerminalConfig &t) { return t.name == name.toStdString(); });
    if (it == list.end()) {
        list.push_back({});
        it = list.end() - 1;
    }
    it->name = name.toStdString();
    it->id = newDeviceId().toStdString();
    const QByteArray key = newDeviceKey();
    it->key = key.toBase64().toStdString();
    it->pairedAt = shared_->now();
    const QString id = QString::fromStdString(it->id);
    shared_->pairing.reset();   // one device per code
    pairingKeys_.clear();
    shared_->saveSettings();

    QJsonObject paired{{u"t"_s, u"paired"_s}, {u"id"_s, id}, {u"key"_s, QString::fromLatin1(key.toBase64())},
                       {u"name"_s, name}, {u"serverId"_s, QString::fromStdString(shared_->settings.serverId)},
                       {u"serverName"_s, QString::fromStdString(shared_->settings.storeName)}};
    if (m.value(u"role").toString() == u"standby")   // the store's standby server
        paired.insert(u"replicaKey"_s, QString::fromStdString(shared_->settings.replicaKey));
    c->channel->send(paired);
    c->socket->disconnectFromHost();
    qCInfo(lcServer).noquote() << "paired a new device:" << name;
}

void PosServer::replicate(const QJsonObject &op)
{
    for (auto &c : connections_)
        if (c->standby && c->synced)
            c->channel->send(op);
}

int PosServer::standbyCount() const
{
    return int(std::ranges::count_if(connections_, [](const auto &c) { return c->standby && c->synced; }));
}

QVariantList PosServer::connections() const
{
    QVariantList out;
    for (const auto &c : connections_) {
        if (!c->session && !c->standby)
            continue;
        out.append(QVariantMap{{u"name"_s, c->standby ? tr("Standby server") : c->session->terminalName()},
                               {u"address"_s, c->socket->peerAddress().toString().remove(u"::ffff:"_s)},
                               {u"since"_s, c->since}, {u"standby"_s, c->standby},
                               {u"user"_s, c->session ? c->session->userName() : QString()}});
    }
    return out;
}

void PosServer::dropRevoked()
{
    // abort() can drop the connection at once: pick them out first.
    QList<QPointer<QSslSocket>> revoked;
    for (auto &c : connections_) {
        if (!c->terminalId.isEmpty() && !shared_->settings.pairedTerminal(c->terminalId.toStdString())) {
            qCInfo(lcServer).noquote() << "device unpaired; disconnecting"
                                       << (c->session ? c->session->terminalName() : c->terminalId);
            revoked << c->socket;
        }
    }
    for (const QPointer<QSslSocket> &s : revoked) {
        if (s)
            s->abort();
    }
}

void PosServer::flush(Connection *c)
{
    if (!c->session)
        return;
    const QVariantMap now = c->session->snapshot();
    QJsonObject changed;
    for (auto it = now.begin(); it != now.end(); ++it) {
        if (c->sent.value(it.key()) != it.value())
            changed.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    c->sent = now;
    if (!changed.isEmpty())
        c->channel->send({{u"t"_s, u"state"_s}, {u"set"_s, changed}});
    for (const QJsonObject &e : std::exchange(c->events, {}))
        c->channel->send(e);
}

void PosServer::scheduleFlushAll()
{
    if (!flushTimer_.isActive())
        flushTimer_.start();
}

void PosServer::drop(Connection *c)
{
    const auto it = std::ranges::find_if(connections_, [c](const auto &x) { return x.get() == c; });
    if (it == connections_.end())
        return;
    if (c->session)
        qCInfo(lcServer) << "terminal disconnected:" << c->session->terminalName();
    if (c->standby && c->synced) {
        qCWarning(lcServer) << "the standby server disconnected";
        QMetaObject::invokeMethod(this, &PosServer::standbyChanged, Qt::QueuedConnection);
    }
    c->socket->deleteLater();
    // Destroying the session releases the check it had open; tell the rest.
    const bool hadSession = c->session != nullptr;
    connections_.erase(it);
    if (hadSession)
        emit shared_->checksChanged();
    emit terminalsChanged();
    scheduleFlushAll();
}

} // namespace vt::net
