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
    if (count > quint32((packet.size() - TrackingWire::HeaderSize) / TrackingWire::TrackSize)
        || packet.size() != TrackingWire::HeaderSize + qsizetype(count) * TrackingWire::TrackSize) return false;

    TrackingMetadata decoded;
    decoded.frameId = qFromBigEndian<quint64>(data + 4);
    decoded.rtpTimestamp = u32(12);
    decoded.guiStartedUs = qFromBigEndian<quint64>(data + 16);
    decoded.trackCount = count;
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
