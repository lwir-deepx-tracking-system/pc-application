#pragma once

#include "TrackingMetadata.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

class MetadataReceiver : public QObject
{
    Q_OBJECT
public:
    explicit MetadataReceiver(QObject *parent = nullptr);
    bool start(quint16 port = 5001, const QString &bindAddress = "0.0.0.0");
    void stop();

signals:
    void frameReady(const TrackingMetadata &metadata);
    void fpsChanged(double fps);
    void errorOccurred(const QString &message);

private:
    void receive();
    QUdpSocket socket;
    QTimer timer;
    QElapsedTimer clock;
    quint64 received = 0;
};
