#pragma once
#include <QObject>
#include <QPointer>
#include <QWidget>

class ManualInputHandler : public QObject
{
    Q_OBJECT
public:
    explicit ManualInputHandler(QWidget *surface, QObject *parent = nullptr);
    void setEnabled(bool enabled);
    void reset(); // Clears input without sending a command (INIT/disconnect/HOME).

signals:
    void moveRequested(qint8 x, qint8 y);
    void homeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void releaseInput();
    QPointer<QWidget> surface;
    quint8 pressed = 0;
    qint8 lastX = 0, lastY = 0;
    bool enabled = false;
    bool spaceDown = false;
};
