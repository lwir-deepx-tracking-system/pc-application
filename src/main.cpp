#include <QApplication>
#include "ui/MainWindow.h"
#include "video/VideoReceiver.h"
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("LWIR RTP/JPEG video receiver (Phase 2 PoC)");
    parser.addHelpOption();
    parser.addOption({"gui-preview", "Show the Phase 1 dummy GUI without receiving video."});
    parser.addOption({"video-trace", "Write frame summary CSV.", "path"});
    parser.addOption({"video-packet-trace", "Optional pre-jitter RTP packet CSV.", "path"});
    parser.addOption({"video-port", "UDP video port.", "port", "5000"});
    parser.addOption({"video-latency", "PoC jitter buffer latency in milliseconds.", "ms", "50"});
    parser.process(app);
    bool portOk = false;
    bool latencyOk = false;
    const uint port = parser.value("video-port").toUInt(&portOk);
    const uint latency = parser.value("video-latency").toUInt(&latencyOk);
    if (!portOk || port == 0 || port > 65535 || !latencyOk || latency > 10000)
        parser.showHelp(1);

    MainWindow window(nullptr, parser.isSet("gui-preview"));
    VideoReceiver receiver;
    QObject::connect(&receiver, &VideoReceiver::frameReady, &window,
        [&window](const QImage &image, quint64, qint64, quint64 frameId) {
            window.setDisplayFrame(image, {}, frameId);
        });
    QObject::connect(&window, &MainWindow::framePainted, &receiver, &VideoReceiver::recordPaint);
    QObject::connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
    QObject::connect(&receiver, &VideoReceiver::errorOccurred, &window,
        [&window](const QString &message) {
            qCritical().noquote() << "Video:" << message;
            window.setDisplayFrame({}, {});
        });
    if (!parser.isSet("gui-preview")
        && !receiver.start(static_cast<quint16>(port), parser.value("video-trace"), latency,
                           parser.value("video-packet-trace")))
        return 1;
    window.show();

    return app.exec();
}
