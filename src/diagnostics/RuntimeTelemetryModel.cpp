#include "RuntimeTelemetryModel.h"
#include <QDateTime>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

RuntimeTelemetryModel::RuntimeTelemetryModel(QObject *parent) : QObject(parent)
{
    clock.start();
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, &RuntimeTelemetryModel::refresh);
}

RuntimeTelemetryModel::~RuntimeTelemetryModel() { stop(); }

void RuntimeTelemetryModel::start(const QString &pcCsvPath)
{
    stop();
    clock.restart();
    haveContext = blocked = false;
    retiredSessions.clear(); seenFrames.clear();
    newestFrame = retiredFrame = 0;
    accumulated = {};
    active = true;
    timer.start();
    csvPath.clear();
    if (pcCsvPath.isEmpty()) return;
    const QFileInfo pcFile(pcCsvPath);
    csvPath = pcFile.absolutePath() + '/' + pcFile.completeBaseName() + "_runtime.csv";
    { std::lock_guard lock(mutex); closing = writerFailed = overflowReported = false; rows.clear(); }
}

void RuntimeTelemetryModel::stop()
{
    active = false;
    timer.stop();
    { std::lock_guard lock(mutex); closing = true; }
    wake.notify_one();
    if (writer.joinable()) writer.join(); // Drain accepted rows on normal shutdown.
    latest.reset();
}

void RuntimeTelemetryModel::clear(bool awaitTransition)
{
    blocked = blocked || (awaitTransition && haveContext);
    latest.reset();
    emit viewChanged(view());
}

void RuntimeTelemetryModel::cancelTransition() { blocked = false; }

bool RuntimeTelemetryModel::observe(const RuntimeTelemetry &t)
{
    if (!active || !t.sessionId || !t.frameId || !t.modelId || !t.trackerId || !std::isfinite(t.sampleAgeMs) || t.sampleAgeMs < 0
        || (t.validMask & ~RuntimeWire::ValidBits)) return false;
    for (int i = 0; i < RuntimeWire::MetricCount; ++i)
        if ((t.validMask & (1u << i)) && (!std::isfinite(t.values[i]) || t.values[i] < 0)) return false;
    const bool differentSession = !haveContext || t.sessionId != session;
    if (differentSession) {
        if (std::find(retiredSessions.begin(), retiredSessions.end(), t.sessionId) != retiredSessions.end()) return false;
        if (haveContext) {
            retiredSessions.push_back(session);
            if (retiredSessions.size() > 16) retiredSessions.pop_front();
        }
    } else {
        if (t.modelGeneration < generation || (t.modelGeneration == generation && t.trackerEpoch < epoch)) return false;
        if (blocked && t.modelGeneration == generation && t.trackerEpoch == epoch) return false;
    }
    const bool newContext = differentSession || t.modelGeneration != generation || t.trackerEpoch != epoch;
    if (!newContext && (t.modelId != modelId || t.trackerId != trackerId)) return false;
    if (newContext) {
        session = t.sessionId; generation = t.modelGeneration; epoch = t.trackerEpoch;
        modelId = t.modelId; trackerId = t.trackerId;
        haveContext = true; blocked = false;
        seenFrames.clear(); newestFrame = retiredFrame = 0;
        accumulated = {}; latest.reset();
        if (differentSession) emit sessionChanged();
        emit viewChanged(view());
    }
    if (t.frameId <= retiredFrame || seenFrames.contains(t.frameId)) return true;
    seenFrames.insert(t.frameId);
    if (seenFrames.size() > 4096) {
        retiredFrame = *seenFrames.begin();
        seenFrames.erase(seenFrames.begin());
    }
    const auto now = clock.elapsed();
    enqueue(t, now); // Keep distinct out-of-order rows within the bounded deduplication window.
    // Match raw CSV statistics: each accepted unique sample contributes once,
    // including late samples. Reception age controls status, not the recorded data.
    for (int i = 0; i < RuntimeWire::MetricCount; ++i)
        if (t.validMask & (1u << i)) accumulated[i].add(t.values[i]);
    if (t.frameId > newestFrame) {
        newestFrame = t.frameId;
        if (t.validMask) {
            latest = Entry{t, now};
        }
    }
    return true;
}

RuntimeTelemetryView RuntimeTelemetryModel::view() const
{
    RuntimeTelemetryView result;
    if (!latest) return result;
    result.metrics = accumulated;
    const auto now = clock.elapsed();
    if (latest->sample.sampleAgeMs + double(now - latest->receivedMs) >= 2000) {
        result.state = RuntimeTelemetryView::State::Stale;
        return result;
    }
    result.state = RuntimeTelemetryView::State::Fresh;
    return result;
}

void RuntimeTelemetryModel::refresh()
{
    emit viewChanged(view());
}

void RuntimeTelemetryModel::enqueue(const RuntimeTelemetry &t, qint64 receivedMs)
{
    if (csvPath.isEmpty()) return;
    QList<QByteArray> fields{QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8(),
        QByteArray::number(receivedMs / 1000.0, 'f', 6), QByteArray::number(t.sessionId),
        QByteArray::number(t.modelGeneration), QByteArray::number(t.trackerEpoch), QByteArray::number(t.frameId),
        QByteArray::number(t.modelId), QByteArray::number(t.trackerId), QByteArray::number(t.validMask),
        QByteArray::number(t.sampleAgeMs, 'g', 9)};
    for (int i = 0; i < RuntimeWire::MetricCount; ++i)
        fields << ((t.validMask & (1u << i)) ? QByteArray::number(t.values[i], 'g', 9) : QByteArray());
    bool overflow = false;
    {
        std::lock_guard lock(mutex);
        if (writerFailed || closing) return;
        if (rows.size() >= 1024) {
            overflow = !overflowReported;
            overflowReported = true;
        } else rows.push_back(fields.join(',') + '\n');
    }
    if (overflow) emit errorOccurred("Runtime recording incomplete: writer queue full; samples dropped");
    if (!writer.joinable()) writer = std::thread([this] { writeLoop(); });
    wake.notify_one();
}

void RuntimeTelemetryModel::writeLoop()
{
    QFile file(csvPath);
    auto fail = [this, &file] {
        { std::lock_guard lock(mutex); writerFailed = true; rows.clear(); }
        emit errorOccurred("Runtime recording incomplete: " + csvPath + ": " + file.errorString());
    };
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { fail(); return; }
    QByteArray header = "received_at,elapsed_s,session_id,model_generation,tracker_epoch,metric_frame_id,model_id,tracker_id,valid_mask,sample_age_ms";
    for (auto name : RuntimeWire::MetricNames) header += ',' + QByteArray(name);
    header += '\n';
    if (file.write(header) != header.size() || !file.flush()) { fail(); return; }
    for (;;) {
        std::deque<QByteArray> batch;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [this] { return closing || !rows.empty(); });
            if (rows.empty() && closing) break;
            batch.swap(rows);
        }
        for (const auto &row : batch)
            if (file.write(row) != row.size()) { fail(); return; }
        if (!file.flush()) { fail(); return; }
    }
}
