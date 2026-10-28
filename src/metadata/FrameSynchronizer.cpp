#include "FrameSynchronizer.h"
#include <algorithm>

FrameSynchronizer::FrameSynchronizer(QObject *parent) : QObject(parent)
{
    clock.start();
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, &FrameSynchronizer::expire);
    timer.start();
}

void FrameSynchronizer::clear()
{
    cache.clear();
    image = {};
    timestamp = -1;
    frameId = 0;
    hasOverlay = false;
    emit trackCountChanged(0);
}

void FrameSynchronizer::submitVideo(const QImage &next, qint64 rtpTimestamp, quint64 deliveryId)
{
    expire();
    image = next;
    timestamp = rtpTimestamp;
    frameId = deliveryId; // PC trace ID; never substitute the runtime's capture frame ID.
    display(); // Display immediately; do not delay video waiting for UDP metadata.
}

void FrameSynchronizer::submitMetadata(const TrackingMetadata &metadata)
{
    expire();
    cache.insert(metadata.rtpTimestamp, {metadata, clock.elapsed()});
    if (cache.size() > Capacity) {
        auto oldest = std::min_element(cache.begin(), cache.end(),
            [](const Entry &a, const Entry &b) { return a.receivedMs < b.receivedMs; });
        cache.erase(oldest);
    }
    // Late metadata may update only the image currently showing that exact timestamp.
    if (!image.isNull() && timestamp >= 0 && quint32(timestamp) == metadata.rtpTimestamp) display();
}

void FrameSynchronizer::expire()
{
    const auto now = clock.elapsed();
    for (auto it = cache.begin(); it != cache.end();) {
        if (now - it->receivedMs >= LifetimeMs) it = cache.erase(it);
        else ++it;
    }
    if (hasOverlay && (timestamp < 0 || !cache.contains(quint32(timestamp)))) display();
}

void FrameSynchronizer::display()
{
    QVector<TrackBox> tracks;
    if (!image.isNull() && timestamp >= 0) {
        const auto found = cache.constFind(quint32(timestamp));
        if (found != cache.cend() && found->metadata.imageSize == image.size()) tracks = found->metadata.tracks;
    }
    hasOverlay = !tracks.isEmpty();
    emit trackCountChanged(tracks.size());
    if (!image.isNull()) emit frameReady(image, tracks, frameId);
}
