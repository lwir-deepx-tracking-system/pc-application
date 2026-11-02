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
            if (now >= deadline) { retry("Control ACK timeout"); return; }
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
    received.clear();
    state = {};
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
    setConnection(Control::Connection::Connecting);
    connectionTimer.start(settings.connectTimeoutMs);
    socket.connectToHost(settings.host, settings.port);
}

void ControlClient::retry(const QString &message)
{
    clearConnection();
    emit errorOccurred(message);
    if (active) reconnectTimer.start(settings.reconnectIntervalMs);
}

bool ControlClient::canControl() const
{
    return connection == Control::Connection::Ready && state.gimbal != Control::GimbalState::Homing
        && state.gimbal != Control::GimbalState::Error;
}

bool ControlClient::send(const Control::Message &message)
{
    if (socket.state() != QAbstractSocket::ConnectedState) return false;
    const int id = key(message.category, message.command);
    if (Control::needsAck(message.category, message.command) && pending.contains(id)) return false;
    const auto bytes = Control::encode(message);
    if (bytes.isEmpty()) return false;
    // No deferred command queue. Fail the connection instead of accumulating stale input.
    if (socket.bytesToWrite() > 65536 || socket.write(bytes) != bytes.size()) {
        retry("Control send failed or backlog exceeded");
        return false;
    }
    if (Control::needsAck(message.category, message.command)) pending.insert(id, clock.elapsed() + settings.ackTimeoutMs);
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
        if (result == Control::ParseResult::Invalid || message.category != Control::Category::System) {
            retry(error.isEmpty() ? "Unexpected board message" : error);
            return;
        }
        if (message.command == quint8(Control::System::Ack)) {
            Control::Ack ack;
            if (!Control::decodeAck(message.payload, ack)) { retry("Invalid ACK payload"); return; }
            if (!pending.remove(key(ack.category, ack.command))) continue; // Unsolicited/obsolete ACK.
            emit commandFinished(ack);
        } else if (message.command == quint8(Control::System::Init) || message.command == quint8(Control::System::Status)) {
            Control::BoardState next;
            if (!Control::decodeState(message.payload, next)) { retry("Invalid board state"); return; }
            if (message.command == quint8(Control::System::Init)) {
                pending.clear();
                emit resetRequested();
            }
            state = next;
            connectionTimer.stop();
            emit boardStateChanged(state);
            setConnection(Control::Connection::Ready);
        } else {
            retry("Board sent a PC-only command");
            return;
        }
    }
}
