#include "net/pos_server.hh"

#include "app/pos_service.hh"
#include "net/layout_hub.hh"
#include "net/protocol.hh"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QTcpSocket>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcServer, "vt.server")

namespace vt::net {

struct PosServer::Connection {
    QTcpSocket *socket = nullptr;
    std::unique_ptr<LineChannel> channel;
    std::unique_ptr<app::PosService> session;   // after hello
    QVariantMap sent;                           // last state sent
    QList<QJsonObject> events;                  // queued until the next flush
    QStringList notices;
};

PosServer::PosServer(app::PosShared *shared, LayoutHub *layouts, QObject *parent)
    : QObject(parent)
    , shared_(shared)
    , layouts_(layouts)
{
    connect(&server_, &QTcpServer::newConnection, this, &PosServer::onNewConnection);
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
            if (c->session && c.get() != origin)
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
    return server_.listen(address, port);
}

int PosServer::terminalCount() const
{
    return int(std::ranges::count_if(connections_, [](const auto &c) { return c->session != nullptr; }));
}

void PosServer::onNewConnection()
{
    while (QTcpSocket *socket = server_.nextPendingConnection()) {
        auto c = std::make_unique<Connection>();
        c->socket = socket;
        c->channel = std::make_unique<LineChannel>(socket);
        Connection *raw = c.get();
        connections_.push_back(std::move(c));
        connect(socket, &QTcpSocket::readyRead, this, [this, raw] { onReadyRead(raw); });
        connect(socket, &QTcpSocket::disconnected, this, [this, raw] { drop(raw); });
    }
}

void PosServer::onReadyRead(Connection *c)
{
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

    if (type == u"hello") {
        if (c->session)
            return;
        const QString name = m.value(u"terminal").toString(u"Terminal"_s);
        c->session = std::make_unique<app::PosService>(shared_, name);
        app::PosService *s = c->session.get();
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
