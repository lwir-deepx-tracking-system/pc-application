#pragma once

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include "metadata/TrackingMetadata.h"

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId = 0);
    QRectF videoRect() const;

signals:
    void targetSetRequested(quint32 trackId);
    void targetClearRequested();
    void framePainted(quint64 frameId, qint64 monotonicNs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QImage image;
    QVector<TrackBox> tracks;
    quint64 pendingFrameId = 0;
};
