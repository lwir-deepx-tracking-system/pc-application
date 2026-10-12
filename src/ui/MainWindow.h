#pragma once

#include <QMainWindow>
#include "VideoWidget.h"

class QComboBox;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks);

signals:
    void targetSetRequested(quint32 trackId);
    void targetClearRequested();

private:
    void requestTargetSet(quint32 trackId);
    void requestTargetClear();
    quint32 enteredTrackId() const;

    VideoWidget *videoWidget;
    QComboBox *trackCombo;
    QPushButton *trackButton;
};
