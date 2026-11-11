#include "TrackingMetadata.h"

#include <QtEndian>
#include <bit>
#include <cmath>

bool decodeTrackingMetadata(const QByteArray &packet, TrackingMetadata &result)
{
    if (packet.size() < TrackingWire::HeaderSize || packet.size() > 65507) return false;
    const auto *data = reinterpret_cast<const uchar *>(packet.constData());
    const auto u32 = [data](int offset) { return qFromBigEndian<quint32>(data + offset); };
    const auto real = [&u32](int offset) { return std::bit_cast<float>(u32(offset)); };
    if (u32(0) != TrackingWire::Magic) return false;
    const quint32 count = u32(24);
    if (count > quint32((packet.size() - TrackingWire::HeaderSize) / TrackingWire::TrackSize)) return false;
    const qsizetype base = TrackingWire::HeaderSize + qsizetype(count) * TrackingWire::TrackSize;
    if (packet.size() != base && packet.size() != base + RuntimeWire::Size) return false;

    TrackingMetadata decoded;
    decoded.frameId = qFromBigEndian<quint64>(data + 4);
    if (!decoded.frameId) return false;
    decoded.rtpTimestamp = u32(12);
    decoded.guiStartedUs = qFromBigEndian<quint64>(data + 16);
    decoded.trackCount = count;
    if (packet.size() != base) {
        if (u32(int(base)) != RuntimeWire::Magic) return false;
        RuntimeTelemetry t;
        const auto u64 = [data, base](int offset) { return qFromBigEndian<quint64>(data + base + offset); };
        t.sessionId = u64(4); t.modelGeneration = u64(12); t.trackerEpoch = u64(20); t.frameId = u64(28);
        t.validMask = u32(int(base) + 36);
        t.sampleAgeMs = real(int(base) + 40);
        t.modelId = u32(int(base) + 44); t.trackerId = u32(int(base) + 48);
        if (!t.sessionId || !t.frameId || t.frameId >= decoded.frameId || !t.modelId || !t.trackerId
            || (t.validMask & ~RuntimeWire::ValidBits) || !std::isfinite(t.sampleAgeMs) || t.sampleAgeMs < 0) return false;
        for (int i = 0; i < RuntimeWire::MetricCount; ++i) {
            if (!(t.validMask & (1u << i))) continue; // Invalid slots never become measured zeroes.
            t.values[i] = real(int(base) + 52 + 4 * i);
            if (!std::isfinite(t.values[i]) || t.values[i] < 0) return false;
        }
        decoded.telemetry = t;
    }
    decoded.tracks.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        const int offset = TrackingWire::HeaderSize + int(i) * TrackingWire::TrackSize;
        const auto id = u32(offset);
        const float x = real(offset + 4), y = real(offset + 8);
        const float w = real(offset + 12), h = real(offset + 16);
        const quint8 classId = data[offset + 20];
        const float confidence = real(offset + 21);
        if (!id || !std::isfinite(confidence) || confidence < 0 || confidence > 1
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
            || w <= 0 || h <= 0) continue;
        const QRectF clipped = QRectF(x, y, w, h).intersected(QRectF(QPointF(0, 0), decoded.imageSize));
        if (!clipped.isEmpty()) decoded.tracks.push_back({id, clipped, classId, confidence});
    }
    result = std::move(decoded); // Reject malformed packets without partially changing output.
    return true;
}
