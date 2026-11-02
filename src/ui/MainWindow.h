#pragma once

#include <QMainWindow>
#include <QSet>
#include "VideoWidget.h"
#include "video/VideoCodec.h"
#include "control/ControlProtocol.h"

class QComboBox;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void resetReception();
    QWidget *manualInputSurface() const;
    void setControlConnection(Control::Connection connection);
    void setBoardState(const Control::BoardState &state);
    void showCommandResult(const Control::Ack &ack);
    void showControlError(const QString &message);
    void setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId = 0);
    void setVideoFps(double fps);
    void setVideoProcessingTime(double milliseconds);
    void setVideoCodec(VideoCodec codec);
    void setMetadataFps(double fps);
    void setTrackCounts(quint32 wireCount, qsizetype displayedCount, bool metadataAvailable);
    void setClassLabels(const ClassLabels &labels);

signals:
    void autoRequested(quint32 trackId);
    void manualRequested();
    void framePainted(quint64 frameId, qint64 monotonicNs);

private:
    void requestAuto(quint32 trackId);
    void requestManual();
    quint32 enteredTrackId() const;
    bool blocked() const;
    void updateControlActions();

    VideoWidget *videoWidget;
    QComboBox *trackCombo;
    QPushButton *autoButton;
    QPushButton *manualButton;
    QLabel *connectionLabel, *modeLabel, *targetLabel, *gimbalLabel, *errorLabel;
    Control::BoardState board;
    bool controlReady = false, stateAvailable = false;
    QLabel *videoFpsLabel;
    QLabel *videoProcessingLabel;
    QLabel *videoCodecLabel;
    QLabel *metadataFpsLabel;
    QLabel *trackCountLabel;
    QSet<quint32> availableTrackIds;
};
