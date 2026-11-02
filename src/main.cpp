#include <QApplication>
#include "ui/MainWindow.h"
#include "app/ApplicationController.h"
#include "app/GuiPreview.h"
#include "control/ManualInputHandler.h"
#include "config/AppConfig.h"
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <exception>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("LWIR RTP/JPEG or H.264 video receiver");
    parser.addHelpOption();
    parser.addOption({"gui-preview", "Show a synthetic preview without receiving video."});
    parser.addOption({"video-trace", "Write frame summary CSV.", "path"});
    parser.addOption({"video-packet-trace", "Optional pre-jitter RTP packet CSV.", "path"});
    parser.addOption({"video-port", "UDP video port.", "port", "5000"});
    parser.addOption({"metadata-port", "UDP tracking metadata port (LWIR, 28-byte header / 25-byte tracks).", "port", "5001"});
    parser.addOption({"config", "Application YAML file (default: config/pc-application.yaml).", "path"});
    parser.addOption({"class-names", "UTF-8 class names file, one name per line starting at class 0.", "path"});
    parser.addOption({"video-latency", "PoC jitter buffer latency in milliseconds.", "ms", "50"});
    parser.process(app);
    bool portOk = false;
    bool latencyOk = false;
    bool metadataPortOk = false;
    const uint port = parser.value("video-port").toUInt(&portOk);
    const uint latency = parser.value("video-latency").toUInt(&latencyOk);
    const uint metadataPort = parser.value("metadata-port").toUInt(&metadataPortOk);
    if (!portOk || port == 0 || port > 65535 || !latencyOk || latency > 10000
        || !metadataPortOk || metadataPort == 0 || metadataPort > 65535 || metadataPort == port)
        parser.showHelp(1);

    QString configPath = parser.isSet("config") ? parser.value("config") : "config/pc-application.yaml";
    if (!parser.isSet("config") && !QFileInfo::exists(configPath))
        configPath = QCoreApplication::applicationDirPath() + "/config/pc-application.yaml";
    AppConfig config;
    try {
        config = loadAppConfig(configPath);
    } catch (const std::exception &error) {
        qCritical().noquote() << "Config:" << configPath << error.what();
        return 1;
    }

    MainWindow window;
    ClassLabels classLabels;
    QString classError;
    const QString classPath = parser.isSet("class-names") ? parser.value("class-names")
        : QCoreApplication::applicationDirPath() + "/class_names.txt";
    if (!classLabels.load(classPath, &classError)) {
        qWarning().noquote() << "Class names:" << classPath << classError;
        if (parser.isSet("class-names")) return 1;
    }
    window.setClassLabels(classLabels);
    window.setVideoCodec(config.videoCodec);
    ApplicationController controller;
    QObject::connect(&controller, &ApplicationController::frameReady, &window, &MainWindow::setDisplayFrame);
    QObject::connect(&controller, &ApplicationController::trackCountsChanged, &window, &MainWindow::setTrackCounts);
    QObject::connect(&controller, &ApplicationController::videoFpsChanged, &window, &MainWindow::setVideoFps);
    QObject::connect(&controller, &ApplicationController::metadataFpsChanged, &window, &MainWindow::setMetadataFps);
    QObject::connect(&controller, &ApplicationController::processingTimeChanged, &window, &MainWindow::setVideoProcessingTime);
    QObject::connect(&controller, &ApplicationController::streamReset, &window, &MainWindow::resetReception);
    QObject::connect(&controller, &ApplicationController::errorOccurred, &window,
        [](const QString &message) { qCritical().noquote() << message; });
    QObject::connect(&window, &MainWindow::framePainted, &controller, &ApplicationController::recordPaint);
    ManualInputHandler input(window.manualInputSurface());
    QObject::connect(&input, &ManualInputHandler::moveRequested, &controller, &ApplicationController::sendMove);
    QObject::connect(&input, &ManualInputHandler::homeRequested, &controller, &ApplicationController::requestHome);
    QObject::connect(&controller, &ApplicationController::inputResetRequested, &input, &ManualInputHandler::reset);
    QObject::connect(&controller, &ApplicationController::manualInputEnabledChanged, &input, &ManualInputHandler::setEnabled);
    QObject::connect(&window, &MainWindow::autoRequested, &controller, &ApplicationController::requestAuto);
    QObject::connect(&window, &MainWindow::manualRequested, &controller, &ApplicationController::requestManual);
    QObject::connect(&controller, &ApplicationController::controlConnectionChanged, &window, &MainWindow::setControlConnection);
    QObject::connect(&controller, &ApplicationController::boardStateChanged, &window, &MainWindow::setBoardState);
    QObject::connect(&controller, &ApplicationController::commandFinished, &window, &MainWindow::showCommandResult);
    QObject::connect(&controller, &ApplicationController::controlError, &window, &MainWindow::showControlError);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, &ApplicationController::stop);
    if (parser.isSet("gui-preview")) {
        showGuiPreview(window);
    } else {
        ReceiverSettings settings;
        settings.videoPort = static_cast<quint16>(port);
        settings.metadataPort = static_cast<quint16>(metadataPort);
        settings.codec = config.videoCodec;
        settings.latencyMs = latency;
        settings.tracePath = parser.value("video-trace");
        settings.packetTracePath = parser.value("video-packet-trace");
        if (!controller.start(settings)) return 1;
        controller.startControl(config.control);
    }
    window.show();

    return app.exec();
}
