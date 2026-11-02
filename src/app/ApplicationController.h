#pragma once

#include "metadata/FrameSynchronizer.h"
#include "metadata/MetadataReceiver.h"
#include "video/VideoReceiver.h"

struct ReceiverSettings
{
    quint16 videoPort = 5000;
    quint16 metadataPort = 5001;
    VideoCodec codec = VideoCodec::JPEG;
    uint latencyMs = 50;
    QString tracePath;
    QString packetTracePath;
};

// Coordinates PC reception; the future TCP client will report board-authoritative state.
class ApplicationController : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;
    bool start(const ReceiverSettings &settings);
    void stop();
    void resetStream();
    void recordPaint(quint64 deliveryId, qint64 monotonicNs);

signals:
    void frameReady(const QImage &image, const QVector<TrackBox> &tracks, quint64 deliveryId);
    void trackCountsChanged(quint32 wireCount, qsizetype displayedCount, bool metadataAvailable);
    void videoFpsChanged(double fps);
    void metadataFpsChanged(double fps);
    void processingTimeChanged(double milliseconds);
    void streamReset();
    void errorOccurred(const QString &message);

private:
    void fail(const QString &message);
    VideoReceiver video;
    MetadataReceiver metadata;
    FrameSynchronizer synchronizer;
};
