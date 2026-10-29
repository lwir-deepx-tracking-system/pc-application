#include "VideoWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <chrono>

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName("videoWidget");
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoWidget::setDisplayFrame(const QImage &newImage, const QVector<TrackBox> &newTracks,
                                  quint64 frameId)
{
    // Replace image and overlays together so old boxes cannot survive a new frame.
    image = newImage.copy();
    tracks = newImage.isNull() ? QVector<TrackBox>{} : newTracks;
    pendingFrameId = newImage.isNull() ? 0 : frameId;
    update();
}

QRectF VideoWidget::videoRect() const
{
    const QSize source = image.isNull() ? QSize(640, 480) : image.size();
    const qreal scale = qMin(qreal(width()) / source.width(), qreal(height()) / source.height());
    const QSizeF size(source.width() * scale, source.height() * scale);
    return QRectF(QPointF((width() - size.width()) / 2.0,
                         (height() - size.height()) / 2.0), size);
}

void VideoWidget::setClassLabels(const ClassLabels &labels)
{
    classLabels = labels;
    update();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#080b10"));
    const QRectF fitted = videoRect();
    if (image.isNull()) {
        painter.setPen(QColor("#8e99a9"));
        painter.drawText(fitted, Qt::AlignCenter, "No video");
        return;
    }
    painter.drawImage(fitted, image);
    painter.save();
    painter.setClipRect(fitted);
    painter.translate(fitted.topLeft());
    const qreal scale = fitted.width() / image.width();
    painter.scale(scale, scale);
    QPen pen(QColor("#64e5c0"), 2.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    QFont labelFont = font();
    labelFont.setPixelSize(14);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    for (const auto &track : tracks) {
        painter.drawRect(track.bbox);
        const QString idLabel = QString("ID %1").arg(track.trackId);
        const QString classLabel = classLabels.label(track.classId);
        const auto metrics = painter.fontMetrics();
        const qreal rowHeight = metrics.height() + 4;
        const qreal labelWidth = qMin(qreal(image.width()),
            qreal(qMax(metrics.horizontalAdvance(idLabel), metrics.horizontalAdvance(classLabel)) + 12));
        const qreal labelHeight = rowHeight * 2;
        const qreal x = qBound(0.0, track.bbox.x(), qMax(0.0, image.width() - labelWidth));
        const qreal preferredY = track.bbox.y() >= labelHeight ? track.bbox.y() - labelHeight : track.bbox.y();
        const qreal y = qBound(0.0, preferredY, qMax(0.0, image.height() - labelHeight));
        const QRectF labelRect(x, y, labelWidth, labelHeight);
        painter.fillRect(labelRect, QColor("#102c29"));
        painter.drawText(QRectF(x + 6, y, labelWidth - 12, rowHeight), Qt::AlignVCenter, idLabel);
        painter.drawText(QRectF(x + 6, y + rowHeight, labelWidth - 12, rowHeight), Qt::AlignVCenter,
            metrics.elidedText(classLabel, Qt::ElideRight, int(labelWidth - 12)));
    }
    painter.restore();
    painter.end();
    if (pendingFrameId) {
        const quint64 paintedId = pendingFrameId;
        pendingFrameId = 0; // Repainting the same image is not another delivered frame.
        const qint64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        emit framePainted(paintedId, now);
    }
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || image.isNull()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRectF fitted = videoRect();
    if (!fitted.contains(event->position()))
        return;
    const QPointF original = (event->position() - fitted.topLeft()) / (fitted.width() / image.width());
    quint32 hitId = 0;
    int hitCount = 0;
    for (const auto &track : tracks) {
        if (track.bbox.contains(original)) {
            hitId = track.trackId;
            ++hitCount;
        }
    }
    if (hitCount == 1)
        emit targetSetRequested(hitId);
    else if (hitCount == 0)
        emit targetClearRequested();
    // Multiple hits stay unresolved until the overlap selection policy is approved.
}
