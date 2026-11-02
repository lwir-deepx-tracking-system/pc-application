#include "ManualInputHandler.h"
#include <QApplication>
#include <QKeyEvent>

namespace {
enum Key : quint8 { Left = 1, Right = 2, Up = 4, Down = 8 };
quint8 directionKey(int key)
{
    switch (key) {
    case Qt::Key_Left: return Left;
    case Qt::Key_Right: return Right;
    case Qt::Key_Up: return Up;
    case Qt::Key_Down: return Down;
    default: return 0;
    }
}
}

ManualInputHandler::ManualInputHandler(QWidget *widget, QObject *parent) : QObject(parent), surface(widget)
{
    widget->setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);
}

void ManualInputHandler::reset() { pressed = 0; lastX = lastY = 0; spaceDown = false; }
void ManualInputHandler::setEnabled(bool value) { enabled = value; if (!value) reset(); }

void ManualInputHandler::releaseInput()
{
    const bool moving = lastX || lastY;
    reset();
    if (enabled && moving) emit moveRequested(0, 0);
}

bool ManualInputHandler::eventFilter(QObject *watched, QEvent *event)
{
    if (!surface) return false;
    if ((watched == surface && event->type() == QEvent::FocusOut)
        || (watched == surface->window() && event->type() == QEvent::WindowDeactivate)
        || event->type() == QEvent::ApplicationDeactivate) {
        releaseInput();
        return false;
    }
    if (watched != surface || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const quint8 bit = directionKey(key->key());
    if (!bit && key->key() != Qt::Key_Space) return false;
    // Consume these keys on the video surface even when control is unavailable.
    if (!enabled || key->isAutoRepeat()) return true;
    if (key->key() == Qt::Key_Space) {
        if (event->type() == QEvent::KeyRelease) spaceDown = false;
        else if (!spaceDown) { reset(); spaceDown = true; emit homeRequested(); }
        return true;
    }
    if (event->type() == QEvent::KeyPress) pressed |= bit;
    else {
        if (!(pressed & bit)) return true; // A release after HOME/reset is obsolete.
        pressed &= ~bit;
    }
    const qint8 x = bool(pressed & Right) - bool(pressed & Left);
    const qint8 y = bool(pressed & Up) - bool(pressed & Down);
    if (x != lastX || y != lastY) {
        lastX = x;
        lastY = y;
        emit moveRequested(x, y);
    }
    return true;
}
