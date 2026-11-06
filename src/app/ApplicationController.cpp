#include "ApplicationController.h"

ApplicationController::ApplicationController(QObject *parent) : QObject(parent)
{
    connect(&video, &VideoReceiver::frameReady, &synchronizer,
        [this](const QImage &image, quint64, qint64 timestamp, quint64 deliveryId) {
            synchronizer.submitVideo(image, timestamp, deliveryId);
        });
    connect(&metadata, &MetadataReceiver::frameReady, &synchronizer, &FrameSynchronizer::submitMetadata);
    connect(&video, &VideoReceiver::streamRestarted, this, &ApplicationController::resetStream);
    connect(&synchronizer, &FrameSynchronizer::frameReady, this, &ApplicationController::frameReady);
    connect(&synchronizer, &FrameSynchronizer::trackCountsChanged, this, &ApplicationController::trackCountsChanged);
    connect(&video, &VideoReceiver::frameObserved, &performance, &PerformanceRecorder::observeFrame);
    connect(&video, &VideoReceiver::rtpBytesReceived, &performance, &PerformanceRecorder::addRtpBytes);
    connect(&performance, &PerformanceRecorder::sampleReady, this, [this](const PerformanceSample &sample) {
        emit videoFpsChanged(sample.displayFps());
        emit videoLatencyChanged(sample.pcLatency.average());
        emit performanceSampleReady(sample);
    });
    connect(&performance, &PerformanceRecorder::errorOccurred, this, &ApplicationController::performanceError);
    connect(&control, &ControlClient::pingResponseMeasured, &performance, &PerformanceRecorder::addPingResponse);
    connect(&control, &ControlClient::connectionLost, &performance, &PerformanceRecorder::disconnected);
    connect(&control, &ControlClient::reconnectAttempted, &performance, &PerformanceRecorder::reconnectAttempted);
    connect(&control, &ControlClient::ackTimedOut, &performance, &PerformanceRecorder::ackTimedOut);
    connect(&metadata, &MetadataReceiver::fpsChanged, this, &ApplicationController::metadataFpsChanged);
    connect(&video, &VideoReceiver::errorOccurred, this,
        [this](const QString &message) { fail("Video: " + message); });
    connect(&metadata, &MetadataReceiver::errorOccurred, this,
        [this](const QString &message) { fail("Metadata: " + message); });
    connect(&control, &ControlClient::connectionChanged, this, [this](Control::Connection connection) {
        if (connection != Control::Connection::Ready) {
            inputMode = Control::Mode::Manual;
            emit inputResetRequested();
        }
        emit manualInputEnabledChanged(control.canControl());
        emit controlConnectionChanged(connection);
    });
    connect(&control, &ControlClient::resetRequested, this, [this] {
        resetStream();
        emit inputResetRequested();
    });
    connect(&control, &ControlClient::boardStateChanged, this, [this](const Control::BoardState &state) {
        // AUTO/HOME has replaced manual intent; don't emit an obsolete key-release MOVE.
        if ((state.mode == Control::Mode::Auto && inputMode != Control::Mode::Auto) || state.gimbal == Control::GimbalState::Homing
            || state.gimbal == Control::GimbalState::Error) emit inputResetRequested();
        inputMode = state.mode;
        emit boardStateChanged(state);
        emit manualInputEnabledChanged(control.canControl());
    });
    connect(&control, &ControlClient::commandFinished, this, &ApplicationController::commandFinished);
    connect(&control, &ControlClient::errorOccurred, this, &ApplicationController::controlError);
    connect(&control, &ControlClient::modelStateChanged, this, [this](const Control::ModelState &state) {
        if (state.busy && !modelWasBusy) {
            emit inputResetRequested();
            resetStream();
        }
        modelWasBusy = state.busy;
        emit modelStateChanged(state);
        emit manualInputEnabledChanged(control.canControl());
    });
}

ApplicationController::~ApplicationController()
{
    stop();
}

bool ApplicationController::start(const ReceiverSettings &settings, const PerformanceSettings &performanceSettings,
                                  const ControlSettings &controlSettings)
{
    stopReception();
    if (!settings.videoPort || !settings.metadataPort || settings.videoPort == settings.metadataPort
        || settings.latencyMs > 10000) {
        emit errorOccurred("Invalid receiver ports or jitter latency");
        return false;
    }
    performance.start(performanceSettings, settings, controlSettings);
    if (!metadata.start(settings.metadataPort, settings.metadataBindAddress)) return false;
    if (!video.start(settings.videoPort, settings.tracePath, settings.latencyMs,
                     settings.packetTracePath, settings.codec, settings.videoBindAddress)) return false;
    return true;
}

void ApplicationController::stop()
{
    control.stop();
    emit inputResetRequested();
    stopReception();
    performance.stop();
}

void ApplicationController::stopReception()
{
    video.stop();
    metadata.stop();
    resetStream();
}

void ApplicationController::resetStream()
{
    synchronizer.clear();
    video.resetMeasurementTimes();
    performance.resetFrames();
    emit streamReset();
}

void ApplicationController::recordPaint(quint64 deliveryId, qint64 monotonicNs)
{
    video.recordPaint(deliveryId, monotonicNs);
    performance.observePaint(deliveryId, monotonicNs);
}

void ApplicationController::fail(const QString &message)
{
    stopReception(); // A video failure must not tear down an independent control connection.
    emit errorOccurred(message);
}

void ApplicationController::startControl(const ControlSettings &settings) { control.start(settings); }
void ApplicationController::requestAuto(quint32 id) { control.requestAuto(id); }
void ApplicationController::requestManual() { control.requestManual(); }
void ApplicationController::requestModel(quint32 id) { control.requestModel(id); }
void ApplicationController::requestHome()
{
    if (control.requestHome()) emit inputResetRequested();
}
void ApplicationController::sendMove(qint8 x, qint8 y)
{
    if (!control.sendMove(x, y)) emit inputResetRequested();
}
