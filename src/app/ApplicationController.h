#pragma once

#include "metadata/FrameSynchronizer.h"
#include "metadata/MetadataReceiver.h"
#include "video/VideoReceiver.h"
#include "control/ControlClient.h"
#include "config/ReceiverSettings.h"
#include "diagnostics/PerformanceRecorder.h"

// Coordinates reception and user requests; the board owns operating state.
class ApplicationController : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;
    bool start(const ReceiverSettings &settings, const PerformanceSettings &performance = {},
               const ControlSettings &controlSettings = {});
    void stop();
    void resetStream();
    void recordPaint(quint64 deliveryId, qint64 monotonicNs);
    void startControl(const ControlSettings &settings);
    void requestAuto(quint32 trackId);
    void requestManual();
    void requestHome();
    void requestModel(quint32 id);
    void sendMove(qint8 x, qint8 y);

signals:
    void frameReady(const QImage &image, const QVector<TrackBox> &tracks, quint64 deliveryId);
    void trackCountsChanged(quint32 wireCount, qsizetype displayedCount, bool metadataAvailable);
    void videoFpsChanged(double fps);
    void metadataFpsChanged(double fps);
    void videoLatencyChanged(double milliseconds);
    void performanceSampleReady(const PerformanceSample &sample);
    void performanceError(const QString &message);
    void streamReset();
    void errorOccurred(const QString &message);
    void controlConnectionChanged(Control::Connection connection);
    void boardStateChanged(const Control::BoardState &state);
    void commandFinished(const Control::Ack &ack);
    void controlError(const QString &message);
    void inputResetRequested();
    void manualInputEnabledChanged(bool enabled);
    void modelStateChanged(const Control::ModelState &state);

private:
    void fail(const QString &message);
    void stopReception();
    ControlClient control;
    Control::Mode inputMode = Control::Mode::Manual;
    bool modelWasBusy = false;
    VideoReceiver video;
    MetadataReceiver metadata;
    FrameSynchronizer synchronizer;
    PerformanceRecorder performance;
};
