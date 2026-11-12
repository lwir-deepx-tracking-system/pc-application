#include "ControlClient.h"
#include <QtEndian>

namespace {
int key(Control::Category category, quint8 command) { return (int(category) << 8) | command; }
}

ControlClient::ControlClient(QObject *parent) : QObject(parent)
{
    clock.start();
    socket.setReadBufferSize(8192);
    reconnectTimer.setSingleShot(true);
    connectionTimer.setSingleShot(true);
    deadlineTimer.setInterval(25);
    connect(&reconnectTimer, &QTimer::timeout, this, &ControlClient::connectNow);
    connect(&connectionTimer, &QTimer::timeout, this, [this] { retry("Control connection/state timeout"); });
    connect(&pingTimer, &QTimer::timeout, this, [this] {
        send({Control::Category::System, quint8(Control::System::Ping), {}});
    });
    connect(&deadlineTimer, &QTimer::timeout, this, [this] {
        const auto now = clock.elapsed();
        for (auto deadline : pending) {
            if (now >= deadline) { emit ackTimedOut(); retry("Control ACK timeout"); return; }
        }
        if (countPending && now >= countDeadline) {
            countPending = false;
            model.status = "Model count unavailable";
            emit modelStateChanged(model);
        }
        if (trackerCountPending && now >= trackerCountDeadline) {
            trackerCountPending = false;
            tracker.status = "Tracker count unavailable";
            emit trackerStateChanged(tracker);
        }
        if (tracker.busy && trackerChangeDeadline >= 0 && now >= trackerChangeDeadline) {
            trackerChangeDeadline = -1;
            tracker.currentId = 0;
            tracker.status = "Result unknown; awaiting completion or reconnect";
            emit trackerStateChanged(tracker);
        }
        if (model.busy && changeDeadline >= 0 && now >= changeDeadline) {
            changeDeadline = -1;
            model.currentId = 0;
            model.status = "Result unknown; awaiting completion or reconnect";
            // The board may still be switching. Keep the request locked and accept a late result.
            emit modelStateChanged(model);
        }
    });
    connect(&socket, &QTcpSocket::connected, this, [this] {
        pending.clear();
        received.clear();
        state = {};
        setConnection(Control::Connection::AwaitingState);
        emit resetRequested();
        connectionTimer.start(settings.connectTimeoutMs);
        pingTimer.start(settings.pingIntervalMs);
        deadlineTimer.start();
    });
    connect(&socket, &QTcpSocket::readyRead, this, &ControlClient::receive);
    connect(&socket, &QTcpSocket::disconnected, this, [this] {
        if (active && connection != Control::Connection::Disconnected) retry("Control disconnected");
    });
    connect(&socket, &QTcpSocket::errorOccurred, this, [this] {
        if (active && connection != Control::Connection::Disconnected) retry(socket.errorString());
    });
}

ControlClient::~ControlClient() { stop(); }

void ControlClient::setConnection(Control::Connection next)
{
    if (connection == next) return;
    connection = next;
    emit connectionChanged(next);
}

void ControlClient::clearConnection()
{
    connectionTimer.stop();
    pingTimer.stop();
    deadlineTimer.stop();
    pending.clear();
    pingSentNs = -1;
    received.clear();
    state = {};
    resetModels();
    resetTrackers();
    setConnection(Control::Connection::Disconnected);
    socket.abort();
}

void ControlClient::start(const ControlSettings &config)
{
    stop();
    settings = config;
    if (!settings.enabled) return;
    if (settings.host.trimmed().isEmpty() || !settings.port || settings.pingIntervalMs < 1
        || settings.ackTimeoutMs < 1 || settings.connectTimeoutMs < 1 || settings.reconnectIntervalMs < 1) {
        emit errorOccurred("Invalid control settings");
        return;
    }
    active = true;
    attempted = false;
    connectNow();
}

void ControlClient::stop()
{
    active = false;
    reconnectTimer.stop();
    clearConnection();
}

void ControlClient::connectNow()
{
    if (!active) return;
    if (attempted) emit reconnectAttempted();
    attempted = true;
    setConnection(Control::Connection::Connecting);
    connectionTimer.start(settings.connectTimeoutMs);
    socket.connectToHost(settings.host, settings.port);
}

void ControlClient::retry(const QString &message)
{
    if (connection == Control::Connection::Ready || connection == Control::Connection::AwaitingState)
        emit connectionLost();
    clearConnection();
    emit errorOccurred(message);
    if (active) reconnectTimer.start(settings.reconnectIntervalMs);
}

bool ControlClient::canControl() const
{
    return connection == Control::Connection::Ready && !model.busy && !tracker.busy && state.gimbal != Control::GimbalState::Homing
        && state.gimbal != Control::GimbalState::Error;
}

bool ControlClient::send(const Control::Message &message)
{
    if (socket.state() != QAbstractSocket::ConnectedState) return false;
    const int id = key(message.category, message.command);
    if (Control::needsAck(message.category, message.command) && pending.contains(id)) return false;
    const auto bytes = Control::encode(message);
    if (bytes.isEmpty()) return false;
    const bool ping = message.category == Control::Category::System && message.command == quint8(Control::System::Ping);
    const qint64 sentNs = ping ? clock.nsecsElapsed() : -1;
    // No deferred command queue. Fail the connection instead of accumulating stale input.
    if (socket.bytesToWrite() > 65536 || socket.write(bytes) != bytes.size()) {
        retry("Control send failed or backlog exceeded");
        return false;
    }
    if (Control::needsAck(message.category, message.command)) pending.insert(id, clock.elapsed() + settings.ackTimeoutMs);
    if (ping) pingSentNs = sentNs;
    return true;
}

bool ControlClient::requestAuto(quint32 id)
{
    if (!id || !canControl()) return false;
    QByteArray payload(4, '\0');
    qToBigEndian(id, reinterpret_cast<uchar *>(payload.data()));
    return send({Control::Category::Mode, quint8(Control::ModeCommand::Auto), payload});
}

bool ControlClient::requestManual()
{
    return canControl() && send({Control::Category::Mode, quint8(Control::ModeCommand::Manual), {}});
}

bool ControlClient::requestHome()
{
    return canControl() && send({Control::Category::Gimbal, quint8(Control::GimbalCommand::Home), {}});
}

bool ControlClient::sendMove(qint8 x, qint8 y)
{
    if (!canControl() || x < -1 || x > 1 || y < -1 || y > 1) return false;
    QByteArray payload;
    payload += char(x);
    payload += char(y);
    return send({Control::Category::Gimbal, quint8(Control::GimbalCommand::Move), payload});
}

void ControlClient::resetModels()
{
    const bool unknown = model.busy;
    model = {};
    model.status = unknown ? "Connection lost; change result unknown" : "Current model unknown";
    countPending = false;
    modelAccepted = false;
    countDeadline = changeDeadline = -1;
    emit modelStateChanged(model);
}

void ControlClient::resetTrackers()
{
    const bool unknown = tracker.busy;
    tracker = {};
    tracker.status = unknown ? "Connection lost; change result unknown" : "Current tracker unknown";
    trackerCountPending = false;
    trackerAccepted = false;
    trackerCountDeadline = trackerChangeDeadline = -1;
    emit trackerStateChanged(tracker);
}

void ControlClient::queryTrackerCount()
{
    if (!settings.trackerSwitchEnabled) return;
    trackerCountRequestId = ++nextRequestId;
    QByteArray payload(4, '\0');
    qToBigEndian(trackerCountRequestId, reinterpret_cast<uchar *>(payload.data()));
    if (send({Control::Category::Pipeline, quint8(Control::PipelineCommand::TrackerCount), payload})) {
        trackerCountPending = true;
        trackerCountDeadline = clock.elapsed() + settings.ackTimeoutMs;
        tracker.status = "Querying trackers; current tracker unknown";
        emit trackerStateChanged(tracker);
    }
}

void ControlClient::queryModelCount()
{
    if (!settings.modelSwitchEnabled) return;
    countRequestId = ++nextRequestId;
    QByteArray payload(4, '\0');
    qToBigEndian(countRequestId, reinterpret_cast<uchar *>(payload.data()));
    if (send({Control::Category::Pipeline, quint8(Control::PipelineCommand::ModelCount), payload})) {
        countPending = true;
        countDeadline = clock.elapsed() + settings.ackTimeoutMs;
        model.status = "Querying models; current model unknown";
        emit modelStateChanged(model);
    }
}

bool ControlClient::requestModel(quint32 id)
{
    if (!settings.modelSwitchEnabled || !canControl() || !model.countKnown || !id || id > model.count) return false;
    changeRequestId = ++nextRequestId;
    QByteArray payload(8, '\0');
    auto *data = reinterpret_cast<uchar *>(payload.data());
    qToBigEndian(changeRequestId, data);
    qToBigEndian(id, data + 4);
    if (!send({Control::Category::Pipeline, quint8(Control::PipelineCommand::ChangeModel), payload})) return false;
    model.requestedId = id;
    model.busy = true;
    modelAccepted = false;
    changeDeadline = clock.elapsed() + settings.modelSwitchTimeoutMs;
    model.status = "Request sent";
    emit modelStateChanged(model);
    return true;
}

bool ControlClient::requestTracker(quint32 id)
{
    if (!settings.trackerSwitchEnabled || !canControl() || !tracker.countKnown || !id || id > tracker.count) return false;
    trackerChangeRequestId = ++nextRequestId;
    QByteArray payload(8, '\0');
    auto *data = reinterpret_cast<uchar *>(payload.data());
    qToBigEndian(trackerChangeRequestId, data);
    qToBigEndian(id, data + 4);
    if (!send({Control::Category::Pipeline, quint8(Control::PipelineCommand::ChangeTracker), payload})) return false;
    tracker.requestedId = id;
    tracker.busy = true;
    trackerAccepted = false;
    trackerChangeDeadline = clock.elapsed() + settings.trackerSwitchTimeoutMs;
    tracker.status = "Request sent";
    emit trackerStateChanged(tracker);
    return true;
}

bool ControlClient::receivePipeline(const Control::Message &message)
{
    using namespace Control;
    if (message.command != quint8(PipelineCommand::ModelCountReply)
        && message.command != quint8(PipelineCommand::ModelResult)
        && message.command != quint8(PipelineCommand::TrackerCountReply)
        && message.command != quint8(PipelineCommand::TrackerResult)) return false;
    const auto *data = reinterpret_cast<const uchar *>(message.payload.constData());
    const auto requestId = qFromBigEndian<quint32>(data);
    if (message.command == quint8(PipelineCommand::TrackerCountReply)) {
        if (!trackerCountPending || requestId != trackerCountRequestId) return true;
        trackerCountPending = false;
        tracker.count = qFromBigEndian<quint32>(data + 4);
        tracker.countKnown = true;
        tracker.status = tracker.count ? "Select a tracker; current tracker unknown" : "No trackers available";
        emit trackerStateChanged(tracker);
        return true;
    }
    if (message.command == quint8(PipelineCommand::TrackerResult)) {
        if (data[4] < 1 || data[4] > 2 || data[5] > quint8(PipelineResult::NotReady)
            || (data[4] == 1 && data[5] != 0)) return false;
        if (!tracker.busy || requestId != trackerChangeRequestId) return true;
        const auto result = PipelineResult(data[5]);
        if (PipelinePhase(data[4]) == PipelinePhase::Accepted) {
            trackerAccepted = true;
            tracker.status = "Switching tracker";
        } else {
            if (result == PipelineResult::Ok && !trackerAccepted) return false;
            tracker.busy = false;
            trackerChangeDeadline = -1;
            if (result == PipelineResult::Ok) tracker.currentId = tracker.requestedId;
            else if (result == PipelineResult::RestoreFailed || result == PipelineResult::CreationFailed) tracker.currentId = 0;
            tracker.requestedId = 0;
            tracker.status = pipelineResultName(result, true);
        }
        emit trackerStateChanged(tracker);
        return true;
    }
    if (message.command == quint8(PipelineCommand::ModelCountReply)) {
        if (!countPending || requestId != countRequestId) return true;
        countPending = false;
        model.count = qFromBigEndian<quint32>(data + 4);
        model.countKnown = true;
        model.status = model.count ? "Select a model; current model unknown" : "No models available";
    } else {
        if (data[4] < 1 || data[4] > 2 || data[5] > quint8(PipelineResult::NotReady)
            || (data[4] == 1 && data[5] != 0)) return false;
        if (!model.busy || requestId != changeRequestId) return true;
        const auto result = PipelineResult(data[5]);
        if (PipelinePhase(data[4]) == PipelinePhase::Accepted) {
            modelAccepted = true;
            model.status = "Switching model";
        } else {
            if (result == PipelineResult::Ok && !modelAccepted) return false;
            model.busy = false;
            changeDeadline = -1;
            if (result == PipelineResult::Ok) model.currentId = model.requestedId;
            else if (result == PipelineResult::RestoreFailed || result == PipelineResult::CreationFailed) model.currentId = 0;
            model.requestedId = 0;
            model.status = pipelineResultName(result);
        }
    }
    emit modelStateChanged(model);
    return true;
}

void ControlClient::receive()
{
    if (!active || socket.state() != QAbstractSocket::ConnectedState) return;
    // Consume bounded chunks; a declared length is validated before waiting for its body.
    int processed = 0;
    while (socket.bytesAvailable() || !received.isEmpty()) {
        if (++processed > 256) { QMetaObject::invokeMethod(this, &ControlClient::receive, Qt::QueuedConnection); return; }
        received += socket.read(qMin(qint64(4096 - received.size()), socket.bytesAvailable()));
        Control::Message message;
        QString error;
        const auto result = Control::takeMessage(received, message, error);
        if (result == Control::ParseResult::Incomplete) {
            if (socket.bytesAvailable()) continue;
            return;
        }
        if (result == Control::ParseResult::Invalid) {
            retry(error.isEmpty() ? "Unexpected board message" : error);
            return;
        }
        if (message.category == Control::Category::Pipeline) {
            if (!receivePipeline(message)) { retry("Invalid pipeline response"); return; }
            continue;
        }
        if (message.category != Control::Category::System) { retry("Unexpected board message"); return; }
        if (message.command == quint8(Control::System::Ack)) {
            Control::Ack ack;
            if (!Control::decodeAck(message.payload, ack)) { retry("Invalid ACK payload"); return; }
            if (!pending.remove(key(ack.category, ack.command))) continue; // Unsolicited/obsolete ACK.
            if (ack.category == Control::Category::System && ack.command == quint8(Control::System::Ping)) {
                if (pingSentNs >= 0 && ack.result == Control::Result::Ok)
                    emit pingResponseMeasured(double(clock.nsecsElapsed() - pingSentNs) / 1e6);
                pingSentNs = -1;
            }
            emit commandFinished(ack);
        } else if (message.command == quint8(Control::System::Init) || message.command == quint8(Control::System::Status)) {
            const bool initializeModels = connection != Control::Connection::Ready
                || message.command == quint8(Control::System::Init);
            Control::BoardState next;
            if (!Control::decodeState(message.payload, next)) { retry("Invalid board state"); return; }
            if (message.command == quint8(Control::System::Init)) {
                pending.clear();
                pingSentNs = -1;
                resetModels();
                resetTrackers();
                emit resetRequested();
            }
            state = next;
            connectionTimer.stop();
            emit boardStateChanged(state);
            setConnection(Control::Connection::Ready);
            if (initializeModels) { queryModelCount(); queryTrackerCount(); }
        } else {
            retry("Board sent a PC-only command");
            return;
        }
    }
}
