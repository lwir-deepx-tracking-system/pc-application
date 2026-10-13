#include "VideoTrace.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <chrono>

namespace {
constexpr size_t maxFrames = 256;
constexpr qsizetype maxQueueBytes = 1024 * 1024;
QByteArray number(qint64 value) { return value < 0 ? QByteArray() : QByteArray::number(value); }
QByteArray ptsNumber(quint64 value) { return value == quint64(-1) ? QByteArray() : QByteArray::number(value); }
}

qint64 VideoTrace::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool VideoTrace::open(const QString &summaryPath, const QString &packetPath)
{
    close();
    if (!summaryPath.isEmpty() && !packetPath.isEmpty()
        && QFileInfo(summaryPath).absoluteFilePath().compare(
            QFileInfo(packetPath).absoluteFilePath(), Qt::CaseInsensitive) == 0)
        return false;
    if (!summaryPath.isEmpty()) {
        summaryFile.setFileName(summaryPath);
        if (!summaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        if (summaryFile.write("role,frameId,ptsNs,jitterPtsNs,ssrc,rtpTimestamp,firstSequence,lastSequence,firstRtpNs,lastRtpNs,markerRtpNs,acquiredNs,appsinkNs,paintNs,displayed,clockDomain,timeUnit\n") < 0) {
            close();
            return false;
        }
    }
    if (!packetPath.isEmpty()) {
        packetFile.setFileName(packetPath);
        if (!packetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            close();
            return false;
        }
        if (packetFile.write("role,ssrc,rtpTimestamp,sequence,marker,observedMonotonicNs,clockDomain,timeUnit\n") < 0) {
            close();
            return false;
        }
    }
    enabled = summaryFile.isOpen() || packetFile.isOpen();
    nextDrain = nowNs();
    return true;
}

void VideoTrace::append(QByteArray &queue, const QByteArray &row)
{
    if (queue.size() + row.size() > maxQueueBytes) ++droppedRows;
    else queue += row;
}

void VideoTrace::queueFrame(const Frame &f)
{
    if (!summaryFile.isOpen()) return;
    append(summaries, f.role + ',' + QByteArray::number(f.frameId) + ',' + ptsNumber(f.pts)
        + ',' + ptsNumber(f.jitterPts) + ',' + number(f.ssrc) + ',' + number(f.rtp) + ',' + number(f.firstSequence)
        + ',' + number(f.lastSequence) + ',' + number(f.firstRtp) + ',' + number(f.lastRtp)
        + ',' + number(f.markerRtp)
        + ',' + number(f.acquired) + ',' + number(f.appsink) + ',' + number(f.paint)
        + ',' + (f.role == "sender" ? QByteArray() : QByteArray(f.paint >= 0 ? "1" : "0"))
        + ",local_steady_clock,ns\n");
}

void VideoTrace::record(const char *stage, quint64 frameId, quint64 pts,
                        qint64 rtp, qint64 sequence, qint64 ssrc, bool marker, qint64 observedNs)
{
    if (!active()) return;
    const qint64 now = observedNs < 0 ? nowNs() : observedNs;
    QMutexLocker lock(&mutex);
    if (!enabled) return;
    const QByteArray type(stage);
    if (type == "appsrc") {
        sourceFrames[pts] = {frameId, now};
        while (sourceFrames.size() > maxFrames) sourceFrames.erase(sourceFrames.begin());
        return;
    }
    const bool sender = type == "sender_rtp";
    const bool packet = sender || type == "receiver_packet";
    if (packet && packetFile.isOpen()) {
        append(packets, QByteArray(sender ? "sender" : "receiver") + ',' + number(ssrc)
            + ',' + number(rtp) + ',' + number(sequence) + ',' + (marker ? "1" : "0")
            + ',' + number(now) + ",local_steady_clock,ns\n");
    }
    if (!summaryFile.isOpen()) return;
    const Key key{ssrc, rtp, rtp < 0 ? frameId : 0};
    if (!frames.contains(key)) {
        if (order.size() >= maxFrames) {
            queueFrame(frames.at(order.front()));
            frames.erase(order.front());
            order.pop_front();
        }
        frames.emplace(key, Frame{});
        order.push_back(key);
    }
    Frame &f = frames.at(key);
    f.role = sender ? "sender" : "receiver";
    f.ssrc = ssrc;
    f.rtp = rtp;
    if (sender) {
        const auto source = sourceFrames.find(pts);
        if (source != sourceFrames.end()) {
            f.frameId = source->second.first;
            f.acquired = source->second.second;
        }
        f.pts = pts;
    }
    if (packet) {
        if (f.firstRtp < 0) { f.firstRtp = now; f.firstSequence = sequence; }
        f.lastSequence = sequence; // Arrival order, not a packet-loss estimate.
        f.lastRtp = now;
        if (marker) f.markerRtp = now;
    } else if (type == "receiver_rtp") {
        f.jitterPts = pts; // Keep both PTS values; never substitute the RTP timestamp.
    } else if (type == "appsink") {
        f.pts = pts;
        f.frameId = frameId; // Local delivery ID, not the camera's Frame ID.
        f.appsink = now;
    }
}

void VideoTrace::painted(quint64 frameId, qint64 observedNs)
{
    QMutexLocker lock(&mutex);
    if (!enabled || !frameId) return;
    for (auto &[key, f] : frames)
        if (f.role == "receiver" && f.frameId == frameId && f.paint < 0) {
            f.paint = observedNs;
            break;
        }
}

void VideoTrace::drain()
{
    const qint64 now = nowNs();
    if (now < nextDrain) return;
    nextDrain = now + 250'000'000;
    QByteArray summaryBatch, packetBatch;
    quint64 dropped;
    {
        QMutexLocker lock(&mutex);
        summaries.swap(summaryBatch);
        packets.swap(packetBatch);
        dropped = droppedRows;
        droppedRows = 0;
    }
    if (dropped) qWarning() << "Video trace queue overflow: dropped log rows" << dropped;
    const auto write = [](QFile &file, const QByteArray &batch) {
        if (!file.isOpen() || batch.isEmpty()) return;
        if (file.write(batch) != batch.size() || !file.flush())
            qWarning() << "Video trace write failed:" << file.fileName() << file.errorString();
    };
    write(summaryFile, summaryBatch);
    write(packetFile, packetBatch);
}

void VideoTrace::close()
{
    {
        QMutexLocker lock(&mutex);
        enabled = false;
        for (const auto &key : order) queueFrame(frames.at(key));
        frames.clear();
        order.clear();
        sourceFrames.clear();
    }
    nextDrain = 0;
    drain();
    summaryFile.close();
    packetFile.close();
}
