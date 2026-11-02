#pragma once

#include <QMainWindow>
#include <QSet>
#include "VideoWidget.h"
#include "video/VideoCodec.h"

class QComboBox;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void resetReception();
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId = 0);
    void setVideoFps(double fps);
    void setVideoProcessingTime(double milliseconds);
    void setVideoCodec(VideoCodec codec);
    void setMetadataFps(double fps);
    void setTrackCounts(quint32 wireCount, qsizetype displayedCount, bool metadataAvailable);
    void setClassLabels(const ClassLabels &labels);

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
    QLabel *videoProcessingLabel;
    QLabel *videoCodecLabel;
    QLabel *metadataFpsLabel;
    QLabel *trackCountLabel;
    QSet<quint32> availableTrackIds;
};
