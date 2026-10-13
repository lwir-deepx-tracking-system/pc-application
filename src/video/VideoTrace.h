#pragma once

#include <QFile>
#include <QMutex>
#include <deque>
#include <map>
#include <tuple>
#include <atomic>

// Probes only update bounded memory. The Qt timer batches file writes.
class VideoTrace
{
public:
    ~VideoTrace() { close(); }
    static qint64 nowNs();
    bool active() const { return enabled.load(std::memory_order_relaxed); }
    bool open(const QString &summaryPath, const QString &packetPath = {});
    void record(const char *stage, quint64 frameId, quint64 pts,
                qint64 rtpTimestamp = -1, qint64 sequence = -1,
                qint64 ssrc = -1, bool marker = false, qint64 observedNs = -1);
    void painted(quint64 frameId, qint64 observedNs);
    void drain();
    void close();

private:
    using Key = std::tuple<qint64, qint64, quint64>;
    struct Frame {
        QByteArray role;
        quint64 frameId = 0, pts = quint64(-1), jitterPts = quint64(-1);
        qint64 ssrc = -1, rtp = -1, firstSequence = -1, lastSequence = -1;
        qint64 firstRtp = -1, lastRtp = -1, markerRtp = -1;
        qint64 acquired = -1, appsink = -1, paint = -1;
    };
    void queueFrame(const Frame &frame);
    void append(QByteArray &queue, const QByteArray &row);
    QFile summaryFile, packetFile;
    QMutex mutex;
    std::atomic<bool> enabled{false};
    qint64 nextDrain = 0;
    quint64 droppedRows = 0;
    QByteArray summaries, packets;
    std::map<Key, Frame> frames;
    std::deque<Key> order;
    std::map<quint64, std::pair<quint64, qint64>> sourceFrames;
};
