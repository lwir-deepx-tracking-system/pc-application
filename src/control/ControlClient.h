#pragma once
#include "ControlProtocol.h"
#include "ControlSettings.h"
#include <QElapsedTimer>
#include <QMap>
#include <QTcpSocket>
#include <QTimer>

class ControlClient : public QObject
{
    Q_OBJECT
public:
    explicit ControlClient(QObject *parent = nullptr);
    ~ControlClient() override;
    void start(const ControlSettings &settings);
    void stop();
    bool requestAuto(quint32 trackId);
    bool requestManual();
    bool requestHome();
    bool sendMove(qint8 x, qint8 y);
    bool canControl() const;
    const Control::BoardState &boardState() const { return state; }

signals:
    void connectionChanged(Control::Connection connection);
    void boardStateChanged(const Control::BoardState &state);
    void commandFinished(const Control::Ack &ack);
    void resetRequested(); // A fresh connection or board INIT, not a mode inference.
    void errorOccurred(const QString &message);

private:
    void connectNow();
    void receive();
    bool send(const Control::Message &message);
    void retry(const QString &message);
    void setConnection(Control::Connection connection);
    void clearConnection();
    QTcpSocket socket;
    QTimer reconnectTimer, connectionTimer, pingTimer, deadlineTimer;
    QElapsedTimer clock;
    ControlSettings settings;
    Control::BoardState state;
    Control::Connection connection = Control::Connection::Disconnected;
    QMap<int, qint64> pending;
    QByteArray received;
    bool active = false;
};
