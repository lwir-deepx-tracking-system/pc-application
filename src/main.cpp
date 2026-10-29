#include <QApplication>
#include "ui/MainWindow.h"
#include "video/VideoReceiver.h"
#include "metadata/MetadataReceiver.h"
#include "metadata/FrameSynchronizer.h"
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("LWIR RTP/JPEG or H.264 video receiver");
    parser.addHelpOption();
    parser.addOption({"gui-preview", "Show the Phase 1 dummy GUI without receiving video."});
    parser.addOption({"video-trace", "Write frame summary CSV.", "path"});
    parser.addOption({"video-packet-trace", "Optional pre-jitter RTP packet CSV.", "path"});
    parser.addOption({"video-port", "UDP video port.", "port", "5000"});
    parser.addOption({"metadata-port", "UDP tracking metadata port (runtime LWIM v2).", "port", "5001"});
    parser.addOption({"video-codec", "Video codec: jpeg or h264.", "codec", "jpeg"});
    parser.addOption({"class-names", "UTF-8 class names file, one name per line starting at class 0.", "path"});
    parser.addOption({"video-latency", "PoC jitter buffer latency in milliseconds.", "ms", "50"});
    parser.process(app);
    bool portOk = false;
    bool latencyOk = false;
    bool metadataPortOk = false;
    const uint port = parser.value("video-port").toUInt(&portOk);
    const uint latency = parser.value("video-latency").toUInt(&latencyOk);
    const uint metadataPort = parser.value("metadata-port").toUInt(&metadataPortOk);
    const QString codecName = parser.value("video-codec");
    if (!portOk || port == 0 || port > 65535 || !latencyOk || latency > 10000
        || !metadataPortOk || metadataPort == 0 || metadataPort > 65535 || metadataPort == port
        || (codecName != "jpeg" && codecName != "h264"))
        parser.showHelp(1);

    MainWindow window(nullptr, parser.isSet("gui-preview"));
    ClassLabels classLabels;
    QString classError;
    const QString classPath = parser.isSet("class-names") ? parser.value("class-names")
        : QCoreApplication::applicationDirPath() + "/class_names.txt";
    if (!classLabels.load(classPath, &classError)) {
        qWarning().noquote() << "Class names:" << classPath << classError;
        if (parser.isSet("class-names")) return 1;
    }
    window.setClassLabels(classLabels);
    window.setVideoCodec(codecName);
    VideoReceiver receiver;
    MetadataReceiver metadata;
    FrameSynchronizer synchronizer;
    QObject::connect(&receiver, &VideoReceiver::frameReady, &synchronizer,
        [&synchronizer](const QImage &image, quint64, qint64 timestamp, quint64 frameId) {
            synchronizer.submitVideo(image, timestamp, frameId);
        });
    QObject::connect(&metadata, &MetadataReceiver::frameReady, &synchronizer, &FrameSynchronizer::submitMetadata);
    QObject::connect(&receiver, &VideoReceiver::streamRestarted, &synchronizer, &FrameSynchronizer::clear);
    QObject::connect(&synchronizer, &FrameSynchronizer::frameReady, &window, &MainWindow::setDisplayFrame);
    QObject::connect(&synchronizer, &FrameSynchronizer::trackCountChanged, &window, &MainWindow::setTrackCount);
    QObject::connect(&metadata, &MetadataReceiver::fpsChanged, &window, &MainWindow::setMetadataFps);
    QObject::connect(&metadata, &MetadataReceiver::errorOccurred, &window,
        [](const QString &message) { qCritical().noquote() << "Metadata:" << message; });
    QObject::connect(&window, &MainWindow::framePainted, &receiver, &VideoReceiver::recordPaint);
    QObject::connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
    QObject::connect(&receiver, &VideoReceiver::processingTimeChanged, &window, &MainWindow::setVideoProcessingTime);
    QObject::connect(&receiver, &VideoReceiver::errorOccurred, &window,
        [&window, &synchronizer](const QString &message) {
            qCritical().noquote() << "Video:" << message;
            synchronizer.clear();
            window.setDisplayFrame({}, {});
        });
    const auto startReceiver = [&](const QString &name) {
        synchronizer.clear();
        window.setDisplayFrame({}, {});
        return receiver.start(static_cast<quint16>(port), parser.value("video-trace"), latency,
                              parser.value("video-packet-trace"), name == "jpeg"
                                  ? VideoReceiver::Codec::JPEG : VideoReceiver::Codec::H264);
    };
    QObject::connect(&window, &MainWindow::videoCodecChanged, &receiver, [&](const QString &name) {
        if (!parser.isSet("gui-preview")) startReceiver(name);
    });
    if (!parser.isSet("gui-preview")
        && (!metadata.start(static_cast<quint16>(metadataPort)) || !startReceiver(codecName)))
        return 1;
    window.show();

    return app.exec();
}
