#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <map>
#include "VideoTrace.h"
#include <gst/gst.h>

class VideoReceiver : public QObject
{
    Q_OBJECT
public:
    enum class Codec { JPEG, H264 };
    explicit VideoReceiver(QObject *parent = nullptr);
    ~VideoReceiver() override;
    bool start(quint16 port = 5000, const QString &tracePath = {}, uint latencyMs = 50,
               const QString &packetTracePath = {}, Codec codec = Codec::JPEG);
    void stop();
    void recordPaint(quint64 frameId, qint64 monotonicNs);

signals:
    void frameReady(const QImage &image, quint64 ptsNs, qint64 rtpTimestamp, quint64 frameId);
    void fpsChanged(double fps);
    // Depayloader's complete-frame arrival to owned QImage, including poll wait.
    void processingTimeChanged(double milliseconds);
    void errorOccurred(const QString &message);

private:
    static GstPadProbeReturn inspectRtp(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static GstPadProbeReturn inspectIncomingRtp(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    void poll();

    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    GstBus *bus = nullptr;
    QTimer timer;
    QElapsedTimer fpsClock;
    quint64 fpsFrames = 0;
    qint64 processingNs = 0;
    quint64 processingFrames = 0;
    quint64 deliveryId = 0; // Never reset across receiver restarts.
    VideoTrace trace;
    QMutex timestampMutex;
    struct FrameTiming { quint32 ssrc; quint32 rtp; qint64 receivedNs; };
    std::map<quint64, FrameTiming> timestamps;
};
