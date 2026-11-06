#include "GuiPreview.h"
#include "ui/MainWindow.h"
#include <QPainter>

namespace {
QImage makeDummyImage()
{
    QImage image(640, 480, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, 0, 480);
    gradient.setColorAt(0, QColor(28, 28, 28));
    gradient.setColorAt(1, QColor(87, 87, 87));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(99, 99, 99));
    for (int y = 300; y < 480; y += 30)
        painter.drawLine(0, y, 640, y);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (const QPoint &position : {QPoint(120, 160), QPoint(315, 205), QPoint(510, 135)}) {
        painter.setBrush(QColor(194, 194, 194));
        painter.drawEllipse(QRectF(position.x() - 13, position.y(), 26, 26));
        painter.drawRoundedRect(QRectF(position.x() - 20, position.y() + 30, 40, 70), 12, 12);
        painter.setBrush(QColor(155, 155, 155));
        painter.drawRoundedRect(QRectF(position.x() - 18, position.y() + 96, 14, 45), 5, 5);
        painter.drawRoundedRect(QRectF(position.x() + 4, position.y() + 96, 14, 45), 5, 5);
    }
    painter.setPen(QColor(190, 190, 190));
    painter.drawText(QRect(16, 14, 300, 24), "DUMMY FRAME  /  640 x 480");
    return image;
}
}

void showGuiPreview(MainWindow &window)
{
    window.setDisplayFrame(makeDummyImage(), {{3, QRectF(82, 150, 76, 162)},
                                            {15, QRectF(277, 195, 76, 162)},
                                            {22, QRectF(472, 125, 76, 162)}});
}
