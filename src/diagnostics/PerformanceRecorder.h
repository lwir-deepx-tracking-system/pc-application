#pragma once

#include "config/PerformanceSettings.h"
#include "config/ReceiverSettings.h"
#include "control/ControlSettings.h"
#include <QFile>
#include <QObject>
#include <QTimer>
#include <map>

struct PerformanceStatistics
{
    quint64 samples = 0;
    double minimum = 0, sum = 0, maximum = 0;
    void add(double value);
    double average() const { return samples ? sum / samples : -1; }
};

struct PerformanceSample
{
    double elapsedSeconds = 0, intervalSeconds = 0;
    quint64 displayedFrames = 0, rtpBytes = 0;
    PerformanceStatistics pcLatency, pingRtt;
    quint64 disconnects = 0, reconnectAttempts = 0, ackTimeouts = 0;
    double displayFps() const { return displayedFrames / intervalSeconds; }
    double rtpMbps() const { return rtpBytes * 8.0 / intervalSeconds / 1e6; }
};
Q_DECLARE_METATYPE(PerformanceSample)

// Aggregates observations on the controller thread; owns the optional trial file.
class PerformanceRecorder : public QObject
{
    Q_OBJECT
public:
    explicit PerformanceRecorder(QObject *parent = nullptr);
    ~PerformanceRecorder() override;
    void start(const PerformanceSettings &settings, const ReceiverSettings &receiver, const ControlSettings &control);
    void stop();
    void resetFrames();
    void observeFrame(quint64 deliveryId, qint64 firstRtpNs);
    void observePaint(quint64 deliveryId, qint64 paintedNs);
    void addRtpBytes(quint64 bytes);
    void addPingResponse(double milliseconds);
    void disconnected();
    void reconnectAttempted();
    void ackTimedOut();
    QString outputPath() const { return file.fileName(); }

signals:
    void sampleReady(const PerformanceSample &sample);
    void errorOccurred(const QString &message);

private:
    void collect();
    bool write(const QByteArray &contents);
    QTimer timer;
    QFile file;
    bool active = false;
    qint64 startedNs = 0, previousNs = 0;
    PerformanceSample interval;
    std::map<quint64, qint64> frames;
};
