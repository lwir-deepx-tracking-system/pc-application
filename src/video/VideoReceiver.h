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
    explicit VideoReceiver(QObject *parent = nullptr);
    ~VideoReceiver() override;
    bool start(quint16 port = 5000, const QString &tracePath = {}, uint latencyMs = 50);
    void stop();

signals:
    void frameReady(const QImage &image, quint64 ptsNs, qint64 rtpTimestamp);
    void fpsChanged(double fps);
    void errorOccurred(const QString &message);

private:
    static GstPadProbeReturn inspectRtp(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    void poll();

    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    GstBus *bus = nullptr;
    QTimer timer;
    QElapsedTimer fpsClock;
    quint64 fpsFrames = 0;
    VideoTrace trace;
    QMutex timestampMutex;
    std::map<quint64, quint32> timestamps;
};
