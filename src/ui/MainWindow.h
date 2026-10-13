#pragma once

#include <QMainWindow>
#include "VideoWidget.h"

class QComboBox;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool dummyPreview = true);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId = 0);
    void setVideoFps(double fps);

signals:
    void targetSetRequested(quint32 trackId);
    void targetClearRequested();
    void framePainted(quint64 frameId, qint64 monotonicNs);

private:
    void requestTargetSet(quint32 trackId);
    void requestTargetClear();
    quint32 enteredTrackId() const;

    VideoWidget *videoWidget;
    QComboBox *trackCombo;
    QPushButton *trackButton;
    QLabel *videoFpsLabel;
};
