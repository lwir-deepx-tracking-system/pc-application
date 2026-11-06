#include "PerformanceRecorder.h"
#include <QDateTime>
#include <QDir>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
QByteArray decimal(double value) { return QByteArray::number(value, 'f', 6); }
void appendStatistics(QList<QByteArray> &fields, const PerformanceStatistics &stats)
{
    fields << QByteArray::number(stats.samples);
    if (stats.samples) fields << decimal(stats.minimum) << decimal(stats.average()) << decimal(stats.maximum);
    else fields << QByteArray() << QByteArray() << QByteArray();
}
// Keep trial metadata on one line even for host strings supplied in YAML.
QString infoValue(QString value)
{
    return value.replace('%', "%25").replace('\r', "%0D").replace('\n', "%0A").replace(';', "%3B");
}
}

void PerformanceStatistics::add(double value)
{
    if (!std::isfinite(value) || value < 0) return;
    if (!samples) minimum = maximum = value;
    else { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
    sum += value;
    ++samples;
}

PerformanceRecorder::PerformanceRecorder(QObject *parent) : QObject(parent)
{
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, &PerformanceRecorder::collect);
}

PerformanceRecorder::~PerformanceRecorder() { stop(); }

void PerformanceRecorder::start(const PerformanceSettings &settings, const ReceiverSettings &receiver,
                                const ControlSettings &control)
{
    stop();
    file.setFileName({});
    frames.clear();
    interval = {};
    startedNs = previousNs = nowNs();
    active = true;
    timer.start();
    if (!settings.enabled) return;
    const QDateTime startedAt = QDateTime::currentDateTime();
    const QDir directory(settings.outputDir);
    if (!directory.mkpath(".")) {
        emit errorOccurred("Cannot create performance output directory: " + directory.absolutePath());
        return;
    }
    const QString stem = startedAt.toString("yyyyMMdd_HHmmss");
    for (int suffix = 0; ; ++suffix) {
        file.setFileName(directory.filePath(stem + (suffix ? "_" + QString::number(suffix) : QString()) + ".csv"));
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) break;
        if (QFile::exists(file.fileName())) continue;
        emit errorOccurred("Cannot open performance file: " + file.fileName() + ": " + file.errorString());
        return;
    }
    const QString info = QString("# codec=%1; started_at=%2; video_bind_address=%3; video_port=%4; "
        "metadata_bind_address=%5; metadata_port=%6; jitter_latency_ms=%7; control_enabled=%8; "
        "control_host=%9; control_port=%10; ping_interval_ms=%11; ack_timeout_ms=%12; "
        "connect_timeout_ms=%13; reconnect_interval_ms=%14\n")
        .arg(receiver.codec == VideoCodec::JPEG ? "jpeg" : "h264",
             startedAt.toOffsetFromUtc(startedAt.offsetFromUtc()).toString(Qt::ISODateWithMs),
             infoValue(receiver.videoBindAddress)).arg(receiver.videoPort)
        .arg(infoValue(receiver.metadataBindAddress)).arg(receiver.metadataPort).arg(receiver.latencyMs)
        .arg(control.enabled ? "true" : "false", infoValue(control.host)).arg(control.port)
        .arg(control.pingIntervalMs).arg(control.ackTimeoutMs).arg(control.connectTimeoutMs).arg(control.reconnectIntervalMs);
    write(info.toUtf8() + "elapsed_s,interval_s,displayed_frames,display_fps,rtp_mbps,pc_latency_samples,"
        "pc_latency_min_ms,pc_latency_avg_ms,pc_latency_max_ms,ping_samples,ping_rtt_min_ms,ping_rtt_avg_ms,"
        "ping_rtt_max_ms,control_disconnects,reconnect_attempts,ack_timeouts\n");
}

void PerformanceRecorder::stop()
{
    if (!active) return;
    timer.stop();
    collect();
    active = false;
    file.close();
    frames.clear();
}

void PerformanceRecorder::resetFrames() { frames.clear(); }

void PerformanceRecorder::observeFrame(quint64 deliveryId, qint64 firstRtpNs)
{
    if (!active || !deliveryId) return;
    frames.emplace(deliveryId, firstRtpNs);
    while (frames.size() > 256) frames.erase(frames.begin());
}

void PerformanceRecorder::observePaint(quint64 deliveryId, qint64 paintedNs)
{
    if (!active) return;
    const auto frame = frames.find(deliveryId);
    if (frame == frames.end()) return; // Includes duplicate paints and reset/expired frames.
    ++interval.displayedFrames;
    if (frame->second >= startedNs && paintedNs >= frame->second)
        interval.pcLatency.add(double(paintedNs - frame->second) / 1e6);
    frames.erase(frame);
}

void PerformanceRecorder::addRtpBytes(quint64 bytes) { if (active) interval.rtpBytes += bytes; }
void PerformanceRecorder::addPingResponse(double ms) { if (active) interval.pingRtt.add(ms); }
void PerformanceRecorder::disconnected() { if (active) ++interval.disconnects; }
void PerformanceRecorder::reconnectAttempted() { if (active) ++interval.reconnectAttempts; }
void PerformanceRecorder::ackTimedOut() { if (active) ++interval.ackTimeouts; }

void PerformanceRecorder::collect()
{
    const qint64 now = nowNs();
    if (!active || now <= previousNs) return;
    PerformanceSample sample = interval;
    sample.elapsedSeconds = double(now - startedNs) / 1e9;
    sample.intervalSeconds = double(now - previousNs) / 1e9;
    interval = {};
    previousNs = now;
    if (file.isOpen()) {
        QList<QByteArray> fields{decimal(sample.elapsedSeconds), decimal(sample.intervalSeconds),
            QByteArray::number(sample.displayedFrames), decimal(sample.displayFps()), decimal(sample.rtpMbps())};
        appendStatistics(fields, sample.pcLatency);
        appendStatistics(fields, sample.pingRtt);
        fields << QByteArray::number(sample.disconnects) << QByteArray::number(sample.reconnectAttempts)
               << QByteArray::number(sample.ackTimeouts);
        write(fields.join(',') + '\n');
    }
    emit sampleReady(sample);
}

bool PerformanceRecorder::write(const QByteArray &contents)
{
    if (file.write(contents) == contents.size() && file.flush()) return true;
    const auto message = "Performance recording incomplete: " + file.fileName() + ": " + file.errorString();
    file.close(); // Report once; continue live statistics without an unbounded retry queue.
    emit errorOccurred(message);
    return false;
}
