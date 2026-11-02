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
    connect(&video, &VideoReceiver::fpsChanged, this, &ApplicationController::videoFpsChanged);
    connect(&metadata, &MetadataReceiver::fpsChanged, this, &ApplicationController::metadataFpsChanged);
    connect(&video, &VideoReceiver::processingTimeChanged, this, &ApplicationController::processingTimeChanged);
    connect(&video, &VideoReceiver::errorOccurred, this,
        [this](const QString &message) { fail("Video: " + message); });
    connect(&metadata, &MetadataReceiver::errorOccurred, this,
        [this](const QString &message) { fail("Metadata: " + message); });
}

ApplicationController::~ApplicationController()
{
    stop();
}

bool ApplicationController::start(const ReceiverSettings &settings)
{
    stop();
    if (!settings.videoPort || !settings.metadataPort || settings.videoPort == settings.metadataPort
        || settings.latencyMs > 10000) {
        emit errorOccurred("Invalid receiver ports or jitter latency");
        return false;
    }
    if (!metadata.start(settings.metadataPort)) return false;
    if (!video.start(settings.videoPort, settings.tracePath, settings.latencyMs,
                     settings.packetTracePath, settings.codec)) return false;
    return true;
}

void ApplicationController::stop()
{
    video.stop();
    metadata.stop();
    resetStream();
}

void ApplicationController::resetStream()
{
    synchronizer.clear();
    emit streamReset();
}

void ApplicationController::recordPaint(quint64 deliveryId, qint64 monotonicNs)
{
    video.recordPaint(deliveryId, monotonicNs);
}

void ApplicationController::fail(const QString &message)
{
    stop();
    emit errorOccurred(message);
}
