#pragma once

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include "metadata/TrackingMetadata.h"
#include "ClassLabels.h"

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId = 0);
    QRectF videoRect() const;
    void setClassLabels(const ClassLabels &labels);

signals:
    void autoRequested(quint32 trackId);
    void manualRequested();
    void framePainted(quint64 frameId, qint64 monotonicNs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QImage image;
    QVector<TrackBox> tracks;
    ClassLabels classLabels;
    quint64 pendingFrameId = 0;
    quint64 lastFrameId = 0;
};
