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
        const QString label = QString("ID %1").arg(track.trackId);
        const qreal labelWidth = painter.fontMetrics().horizontalAdvance(label) + 12;
        const QRectF labelRect(track.bbox.x(), qMax(0.0, track.bbox.y() - 23.0), labelWidth, 23);
        painter.fillRect(labelRect, QColor("#102c29"));
        painter.drawText(labelRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter, label);
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
