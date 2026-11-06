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
    bool requestModel(quint32 modelId);
    bool canControl() const;
    const Control::BoardState &boardState() const { return state; }

signals:
    void connectionChanged(Control::Connection connection);
    void boardStateChanged(const Control::BoardState &state);
    void commandFinished(const Control::Ack &ack);
    void resetRequested(); // A fresh connection or board INIT, not a mode inference.
    void errorOccurred(const QString &message);
    void pingResponseMeasured(double milliseconds);
    void connectionLost();
    void reconnectAttempted();
    void ackTimedOut();
    void modelStateChanged(const Control::ModelState &state);

private:
    void connectNow();
    void receive();
    bool send(const Control::Message &message);
    void retry(const QString &message);
    void setConnection(Control::Connection connection);
    void clearConnection();
    void queryModelCount();
    bool receivePipeline(const Control::Message &message);
    void resetModels();
    QTcpSocket socket;
    QTimer reconnectTimer, connectionTimer, pingTimer, deadlineTimer;
    QElapsedTimer clock;
    ControlSettings settings;
    Control::BoardState state;
    Control::Connection connection = Control::Connection::Disconnected;
    QMap<int, qint64> pending;
    QByteArray received;
    bool active = false;
    bool attempted = false;
    qint64 pingSentNs = -1;
    Control::ModelState model;
    quint32 nextRequestId = 0, countRequestId = 0, changeRequestId = 0;
    bool countPending = false, modelAccepted = false;
    qint64 countDeadline = -1, changeDeadline = -1;
};
