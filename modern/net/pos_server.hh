#pragma once

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTimer>

#include <memory>
#include <vector>

namespace vt::app { class PosShared; }

namespace vt::net {

class LayoutHub;

// Serves remote terminals: each connection gets its own PosService session
// on the shared store, and receives its state as it changes.
class PosServer : public QObject {
    Q_OBJECT

public:
    PosServer(app::PosShared *shared, LayoutHub *layouts, QObject *parent = nullptr);
    ~PosServer() override;

    bool listen(const QHostAddress &address, quint16 port);
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return server_.errorString(); }
    int terminalCount() const;

signals:
    void terminalsChanged();

private:
    struct Connection;

    void onNewConnection();
    void onReadyRead(Connection *c);
    void handle(Connection *c, const QJsonObject &message);
    void drop(Connection *c);
    // Send changed state to one / every terminal.
    void flush(Connection *c);
    void scheduleFlushAll();

    app::PosShared *shared_;
    LayoutHub *layouts_;
    QTcpServer server_;
    std::vector<std::unique_ptr<Connection>> connections_;
    QTimer flushTimer_;
    QTimer tickTimer_;   // refresh time-based fields (minutes open)
};

} // namespace vt::net
