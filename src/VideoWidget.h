#pragma once

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QWidget>

struct TrackBox
{
    quint32 trackId;
    QRectF bbox;
};

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks);
    QRectF videoRect() const;

signals:
    void targetSetRequested(quint32 trackId);
    void targetClearRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QImage image;
    QVector<TrackBox> tracks;
};
