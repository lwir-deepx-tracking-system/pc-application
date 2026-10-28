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
};
Q_DECLARE_METATYPE(TrackBox)
Q_DECLARE_METATYPE(QVector<TrackBox>)

struct TrackingMetadata
{
    quint64 frameId = 0;
    quint32 rtpTimestamp = 0;
    quint64 guiStartedUs = 0;
    QSize imageSize;
    QVector<TrackBox> tracks;
};
Q_DECLARE_METATYPE(TrackingMetadata)

// Runtime LWIM v2, network byte order: 40-byte header, 28 bytes per track.
bool decodeTrackingMetadata(const QByteArray &packet, TrackingMetadata &result);
