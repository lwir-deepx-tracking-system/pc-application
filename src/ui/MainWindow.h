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
    void setVideoLatency(double milliseconds);
    void showRecordingError(const QString &message);
    void setVideoCodec(VideoCodec codec);
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
    QLabel *videoLatencyLabel;
    QLabel *videoCodecLabel;
    QSet<quint32> availableTrackIds;
};
