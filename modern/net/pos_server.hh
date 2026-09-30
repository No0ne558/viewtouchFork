#pragma once

#include <QHostAddress>
#include <QHash>
#include <QObject>
#include <QSslServer>
#include <QTimer>

#include <memory>
#include <vector>

namespace vt::app { class PosShared; }

namespace vt::net {

class LayoutHub;

// Serves remote terminals: each connection gets its own PosService session
// on the shared store, and receives its state as it changes. Connections
// are encrypted and only paired devices get in (see net/pairing.hh); a
// device pairing with the manager's code gets its key and is let go.
class PosServer : public QObject {
    Q_OBJECT

public:
    PosServer(app::PosShared *shared, LayoutHub *layouts, QObject *parent = nullptr);
    ~PosServer() override;

    // False when this system can't make encrypted connections (errorString says why).
    bool listen(const QHostAddress &address, quint16 port);
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return error_.isEmpty() ? server_.errorString() : error_; }
    int terminalCount() const;

signals:
    void terminalsChanged();

private:
    struct Connection;

    void onNewConnection();
    void onPreSharedKey(QSslSocket *socket, class QSslPreSharedKeyAuthenticator *auth);
    void pair(Connection *c, const QJsonObject &message);
    // Drop connections of devices that are no longer paired.
    void dropRevoked();
    void onReadyRead(Connection *c);
    void handle(Connection *c, const QJsonObject &message);
    void drop(Connection *c);
    // Send changed state to one / every terminal.
    void flush(Connection *c);
    void scheduleFlushAll();

    app::PosShared *shared_;
    LayoutHub *layouts_;
    QSslServer server_;
    QString error_;
    QHash<QString, QByteArray> pairingKeys_;   // code -> TLS key (slow to derive)
    std::vector<std::unique_ptr<Connection>> connections_;
    QTimer flushTimer_;
    QTimer tickTimer_;   // refresh time-based fields (minutes open)
};

} // namespace vt::net
