#include "MetadataReceiver.h"

#include <QNetworkDatagram>
#include <QVariant>

MetadataReceiver::MetadataReceiver(QObject *parent) : QObject(parent)
{
    connect(&socket, &QUdpSocket::readyRead, this, &MetadataReceiver::receive);
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, [this] {
        emit fpsChanged(received * 1000.0 / qMax(qint64(1), clock.restart()));
        received = 0;
    });
}

bool MetadataReceiver::start(quint16 port)
{
    stop();
    if (!port || !socket.bind(QHostAddress::AnyIPv4, port, QAbstractSocket::DontShareAddress)) {
        emit errorOccurred(QString("Cannot receive metadata on UDP %1: %2").arg(port).arg(socket.errorString()));
        return false;
    }
    socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
    received = 0;
    clock.start();
    timer.start();
    return true;
}

void MetadataReceiver::stop()
{
    timer.stop();
    socket.close();
    received = 0;
}

void MetadataReceiver::receive()
{
    // Bound each event-loop turn so a packet burst cannot starve video painting.
    int remaining = 256;
    while (remaining-- && socket.hasPendingDatagrams()) {
        const auto datagram = socket.receiveDatagram();
        TrackingMetadata metadata;
        if (!datagram.isValid() || !decodeTrackingMetadata(datagram.data(), metadata)) continue;
        ++received;
        emit frameReady(metadata);
    }
    if (socket.hasPendingDatagrams())
        QTimer::singleShot(0, this, &MetadataReceiver::receive);
}
