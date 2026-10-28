#include "TrackingMetadata.h"

#include <QtEndian>
#include <bit>
#include <cmath>

bool decodeTrackingMetadata(const QByteArray &packet, TrackingMetadata &result)
{
    if (packet.size() < 40 || packet.size() > 65507) return false;
    const auto *data = reinterpret_cast<const uchar *>(packet.constData());
    const auto u32 = [data](int offset) { return qFromBigEndian<quint32>(data + offset); };
    const auto real = [&u32](int offset) { return std::bit_cast<float>(u32(offset)); };
    if (u32(0) != 0x4c57494d || qFromBigEndian<quint16>(data + 4) != 2
        || qFromBigEndian<quint16>(data + 6) != 1) return false;
    const quint32 width = u32(28), height = u32(32), count = u32(36);
    if (!width || !height || width > 16384 || height > 16384
        || count > quint32((packet.size() - 40) / 28)
        || packet.size() != 40 + qsizetype(count) * 28) return false;

    TrackingMetadata decoded;
    decoded.frameId = qFromBigEndian<quint64>(data + 8);
    decoded.rtpTimestamp = u32(16);
    decoded.guiStartedUs = qFromBigEndian<quint64>(data + 20);
    decoded.imageSize = QSize(int(width), int(height));
    decoded.tracks.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        const int offset = 40 + int(i) * 28;
        const auto id = std::bit_cast<qint32>(u32(offset));
        const float confidence = real(offset + 8);
        const float x = real(offset + 12), y = real(offset + 16);
        const float w = real(offset + 20), h = real(offset + 24);
        if (id < 0 || !std::isfinite(confidence) || confidence < 0 || confidence > 1
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
            || w < 0 || h < 0) return false;
        const QRectF clipped = QRectF(x, y, w, h).intersected(QRectF(0, 0, width, height));
        if (!clipped.isEmpty()) decoded.tracks.push_back({quint32(id), clipped});
    }
    result = std::move(decoded); // Reject malformed packets without partially changing output.
    return true;
}
