#pragma once

#include <QMainWindow>
#include <QSet>
#include "VideoWidget.h"
#include "video/VideoCodec.h"
#include "control/ControlProtocol.h"
#include "config/ModelSettings.h"
#include "diagnostics/RuntimeTelemetryModel.h"

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
    void setModels(const ModelOptions &models);
    void setModelState(const Control::ModelState &state);
    void setTrackerState(const Control::TrackerState &state);
    void setRuntimeTelemetry(const RuntimeTelemetryView &view);

signals:
    void autoRequested(quint32 trackId);
    void manualRequested();
    void homeRequested();
    void modelRequested(quint32 id);
    void trackerRequested(quint32 id);
    void framePainted(quint64 frameId, qint64 monotonicNs);

private:
    void requestAuto(quint32 trackId);
    void requestManual();
    quint32 enteredTrackId() const;
    bool blocked() const;
    void updateControlActions();
    void updateModels();
    void updateTrackers();

    VideoWidget *videoWidget;
    QComboBox *trackCombo;
    QPushButton *autoButton;
    QPushButton *manualButton;
    QPushButton *homeButton;
    QLabel *connectionLabel, *modeLabel, *targetLabel, *gimbalLabel, *errorLabel;
    Control::BoardState board;
    bool controlReady = false, stateAvailable = false;
    QLabel *videoFpsLabel;
    QLabel *videoLatencyLabel;
    QLabel *videoCodecLabel;
    QLabel *runtimeStatusLabel;
    std::array<QLabel *, 4> runtimeValueLabels;
    QSet<quint32> availableTrackIds;
    QComboBox *modelCombo;
    QLabel *currentModelLabel, *modelStatusLabel;
    ModelOptions models;
    Control::ModelState modelState;
    QComboBox *trackerCombo;
    QLabel *currentTrackerLabel, *trackerStatusLabel;
    Control::TrackerState trackerState;
};
