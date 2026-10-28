#include <QApplication>
#include "ui/MainWindow.h"
#include "video/VideoReceiver.h"
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
    parser.addOption({"video-codec", "Video codec: jpeg or h264.", "codec", "jpeg"});
    parser.addOption({"video-latency", "PoC jitter buffer latency in milliseconds.", "ms", "50"});
    parser.process(app);
    bool portOk = false;
    bool latencyOk = false;
    const uint port = parser.value("video-port").toUInt(&portOk);
    const uint latency = parser.value("video-latency").toUInt(&latencyOk);
    const QString codecName = parser.value("video-codec");
    if (!portOk || port == 0 || port > 65535 || !latencyOk || latency > 10000
        || (codecName != "jpeg" && codecName != "h264"))
        parser.showHelp(1);

    MainWindow window(nullptr, parser.isSet("gui-preview"));
    window.setVideoCodec(codecName);
    VideoReceiver receiver;
    QObject::connect(&receiver, &VideoReceiver::frameReady, &window,
        [&window](const QImage &image, quint64, qint64, quint64 frameId) {
            window.setDisplayFrame(image, {}, frameId);
        });
    QObject::connect(&window, &MainWindow::framePainted, &receiver, &VideoReceiver::recordPaint);
    QObject::connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
    QObject::connect(&receiver, &VideoReceiver::processingTimeChanged, &window, &MainWindow::setVideoProcessingTime);
    QObject::connect(&receiver, &VideoReceiver::errorOccurred, &window,
        [&window](const QString &message) {
            qCritical().noquote() << "Video:" << message;
            window.setDisplayFrame({}, {});
        });
    const auto startReceiver = [&](const QString &name) {
        window.setDisplayFrame({}, {});
        return receiver.start(static_cast<quint16>(port), parser.value("video-trace"), latency,
                              parser.value("video-packet-trace"), name == "jpeg"
                                  ? VideoReceiver::Codec::JPEG : VideoReceiver::Codec::H264);
    };
    QObject::connect(&window, &MainWindow::videoCodecChanged, &receiver, [&](const QString &name) {
        if (!parser.isSet("gui-preview")) startReceiver(name);
    });
    if (!parser.isSet("gui-preview") && !startReceiver(codecName))
        return 1;
    window.show();

    return app.exec();
}
