#pragma once
#include "control/ControlProtocol.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

// Simulates board authority. No gimbal hardware is accessed.
class MockControlServer : public QObject
{
    Q_OBJECT
public:
    explicit MockControlServer(QObject *parent = nullptr);
    ~MockControlServer() override;
    bool listen(const QHostAddress &host = QHostAddress::LocalHost, quint16 port = 5002);
    quint16 port() const { return server.serverPort(); }
    QString errorString() const { return server.errorString(); }
    const Control::BoardState &boardState() const { return state; }
    void setTrackIds(const QSet<quint32> &ids) { trackIds = ids; }
    void setHomeDuration(int ms) { homeTimer.setInterval(ms); }
    void setHeartbeatTimeout(int ms) { heartbeatTimeoutMs = ms; }
    void setPingReplies(bool enabled) { pingReplies = enabled; }
    void reinitialize();
    void publishState(const Control::BoardState &next);

signals:
    void commandReceived(const Control::Message &message);

private:
    void receive();
    void apply(const Control::Message &message);
    void send(const Control::Message &message);
    void ack(const Control::Message &message, Control::Result result);
    QTcpServer server;
    QPointer<QTcpSocket> client;
    QByteArray received;
    Control::BoardState state;
    QSet<quint32> trackIds{3, 7, 15, 22};
    QTimer homeTimer, heartbeatTimer;
    QElapsedTimer clock;
    qint64 lastPing = 0;
    int heartbeatTimeoutMs = 2000;
    bool pingReplies = true;
};
