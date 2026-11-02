#include "MockControlServer.h"
#include <QtEndian>

MockControlServer::MockControlServer(QObject *parent) : QObject(parent)
{
    clock.start();
    homeTimer.setSingleShot(true);
    homeTimer.setInterval(800); // Simulation only; not a physical HOME completion rule.
    connect(&homeTimer, &QTimer::timeout, this, [this] { publishState({}); });
    heartbeatTimer.setInterval(25);
    connect(&heartbeatTimer, &QTimer::timeout, this, [this] {
        if (state.gimbal == Control::GimbalState::ManualMoving && clock.elapsed() - lastPing > heartbeatTimeoutMs)
            publishState({});
    });
    connect(&server, &QTcpServer::newConnection, this, [this] {
        auto *socket = server.nextPendingConnection();
        if (client) { socket->abort(); socket->deleteLater(); return; }
        client = socket;
        socket->setReadBufferSize(8192);
        received.clear();
        lastPing = clock.elapsed();
        connect(socket, &QTcpSocket::readyRead, this, &MockControlServer::receive);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            if (client == socket) {
                client = nullptr;
                received.clear();
                if (state.gimbal == Control::GimbalState::ManualMoving) publishState({});
            }
            socket->deleteLater();
        });
        send({Control::Category::System, quint8(Control::System::Status), Control::encodeState(state)});
    });
}

MockControlServer::~MockControlServer()
{
    homeTimer.stop();
    heartbeatTimer.stop();
    if (client) { client->disconnect(this); client->abort(); }
    server.close();
}

bool MockControlServer::listen(const QHostAddress &host, quint16 port)
{
    if (!server.listen(host, port)) return false;
    heartbeatTimer.start();
    return true;
}

void MockControlServer::send(const Control::Message &message)
{
    if (!client) return;
    const auto bytes = Control::encode(message);
    if (client->bytesToWrite() > 65536 || client->write(bytes) != bytes.size()) client->abort();
}

void MockControlServer::ack(const Control::Message &message, Control::Result result)
{
    if (!Control::needsAck(message.category, message.command)) return;
    QByteArray payload;
    payload += char(message.category);
    payload += char(message.command);
    payload += char(result);
    send({Control::Category::System, quint8(Control::System::Ack), payload});
}

void MockControlServer::publishState(const Control::BoardState &next)
{
    if (next.gimbal != Control::GimbalState::Homing) homeTimer.stop();
    if (next == state) return;
    state = next;
    send({Control::Category::System, quint8(Control::System::Status), Control::encodeState(state)});
}

void MockControlServer::reinitialize()
{
    homeTimer.stop();
    state = {};
    send({Control::Category::System, quint8(Control::System::Init), Control::encodeState(state)});
}

void MockControlServer::receive()
{
    int processed = 0;
    while (client && (client->bytesAvailable() || !received.isEmpty())) {
        if (++processed > 256) { QMetaObject::invokeMethod(this, &MockControlServer::receive, Qt::QueuedConnection); return; }
        received += client->read(qMin(qint64(4096 - received.size()), client->bytesAvailable()));
        Control::Message message;
        QString error;
        const auto result = Control::takeMessage(received, message, error);
        if (result == Control::ParseResult::Incomplete) {
            if (client->bytesAvailable()) continue;
            return;
        }
        if (result == Control::ParseResult::Invalid) { client->abort(); return; }
        emit commandReceived(message);
        apply(message);
    }
}

void MockControlServer::apply(const Control::Message &message)
{
    using namespace Control;
    if (message.category == Category::System) {
        if (message.command != quint8(System::Ping)) { client->abort(); return; }
        lastPing = clock.elapsed();
        if (pingReplies) ack(message, Result::Ok);
        return;
    }
    if (state.gimbal == GimbalState::Homing) { ack(message, Result::InvalidState); return; }
    if (state.gimbal == GimbalState::Error) { ack(message, Result::NotReady); return; }
    if (message.category == Category::Mode) {
        if (message.command == quint8(ModeCommand::Auto)) {
            const auto id = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(message.payload.constData()));
            if (!id || !trackIds.contains(id)) { ack(message, Result::InvalidTarget); return; }
            ack(message, Result::Ok);
            publishState({Mode::Auto, TargetState::Tracking, GimbalState::AutoTracking, 0, id});
        } else {
            ack(message, Result::Ok);
            if (state.mode == Mode::Auto || state.selectedTargetId) publishState({});
        }
    } else if (message.command == quint8(GimbalCommand::Move)) {
        const auto x = qint8(message.payload[0]), y = qint8(message.payload[1]);
        if (x < -1 || x > 1 || y < -1 || y > 1) return;
        if (!x && !y && state.mode == Mode::Auto) return;
        BoardState next;
        next.gimbal = x || y ? GimbalState::ManualMoving : GimbalState::Idle;
        publishState(next);
    } else {
        ack(message, Result::Ok);
        BoardState next;
        next.gimbal = GimbalState::Homing;
        publishState(next);
        homeTimer.start();
    }
}
