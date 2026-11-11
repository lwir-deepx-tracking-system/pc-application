#pragma once

#include "metadata/RuntimeTelemetry.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QElapsedTimer>
#include <QTimer>
#include <deque>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>

struct RuntimeTelemetryView
{
    enum class State { Unavailable, Fresh, Stale };
    State state = State::Unavailable;
    std::array<PerformanceStatistics, RuntimeWire::MetricCount> metrics{};
};
Q_DECLARE_METATYPE(RuntimeTelemetryView)

// GUI-thread aggregation; a single bounded writer owns all runtime CSV I/O.
class RuntimeTelemetryModel : public QObject
{
    Q_OBJECT
public:
    explicit RuntimeTelemetryModel(QObject *parent = nullptr);
    ~RuntimeTelemetryModel() override;
    void start(const QString &pcCsvPath); // Empty path: live statistics without recording.
    void stop();
    void clear(bool awaitTransition = false); // Preserve retired identities across resets.
    void cancelTransition(); // A rejected switch may legitimately keep its original generation.
    bool observe(const RuntimeTelemetry &sample); // False: retired session/generation.
    RuntimeTelemetryView view() const;
    QString outputPath() const { return csvPath; }

signals:
    void viewChanged(const RuntimeTelemetryView &view);
    void sessionChanged();
    void errorOccurred(const QString &message);

private:
    struct Entry { RuntimeTelemetry sample; qint64 receivedMs; };
    void refresh();
    void enqueue(const RuntimeTelemetry &sample, qint64 receivedMs);
    void writeLoop();
    QTimer timer;
    QElapsedTimer clock;
    bool active = false, haveContext = false, blocked = false;
    quint64 session = 0, generation = 0, epoch = 0, newestFrame = 0, retiredFrame = 0;
    quint32 modelId = 0, trackerId = 0;
    std::deque<quint64> retiredSessions;
    std::set<quint64> seenFrames;
    std::array<PerformanceStatistics, RuntimeWire::MetricCount> accumulated{};
    std::optional<Entry> latest;
    QString csvPath;
    std::thread writer;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<QByteArray> rows;
    bool closing = false, writerFailed = false, overflowReported = false;
};
