#pragma once

#include "TrackingMetadata.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>

class FrameSynchronizer : public QObject
{
    Q_OBJECT
public:
    explicit FrameSynchronizer(QObject *parent = nullptr);
    void submitVideo(const QImage &image, qint64 rtpTimestamp, quint64 deliveryId);
    void submitMetadata(const TrackingMetadata &metadata);
    void clear();

signals:
    void frameReady(const QImage &image, const QVector<TrackBox> &tracks, quint64 deliveryId);
    void trackCountChanged(int count);

private:
    void expire();
    void display();
    struct Entry { TrackingMetadata metadata; qint64 receivedMs; };
    static constexpr qint64 LifetimeMs = 500;
    static constexpr int Capacity = 128;
    QHash<quint32, Entry> cache;
    QElapsedTimer clock;
    QTimer timer;
    QImage image;
    qint64 timestamp = -1;
    quint64 frameId = 0;
    bool hasOverlay = false;
};
