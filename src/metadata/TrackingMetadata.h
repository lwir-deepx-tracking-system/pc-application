#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QRectF>
#include <QSize>
#include <QVector>

struct TrackBox
{
    quint32 trackId;
    QRectF bbox; // Original-image pixels: top-left x, y, width, height.
    quint8 classId = 255; // Unknown.
    float confidence = 0;
};
Q_DECLARE_METATYPE(TrackBox)
Q_DECLARE_METATYPE(QVector<TrackBox>)

struct TrackingMetadata
{
    quint64 frameId = 0;
    quint32 rtpTimestamp = 0;
    quint64 guiStartedUs = 0;
    QSize imageSize = QSize(640, 480);
    quint32 trackCount = 0; // Serialized records, before validation/display filtering.
    QVector<TrackBox> tracks;
};
Q_DECLARE_METATYPE(TrackingMetadata)

// LWIR, no version/type: 28-byte header, 25-byte tracks, big endian.
namespace TrackingWire {
inline constexpr quint32 Magic = 0x4c574952;
inline constexpr int HeaderSize = 28;
inline constexpr int TrackSize = 25;
}
bool decodeTrackingMetadata(const QByteArray &packet, TrackingMetadata &result);
