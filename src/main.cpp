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
#include <stdexcept>
#include <string>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("LWIR RTP/JPEG or H.264 video receiver");
    parser.addHelpOption();
    parser.addOption({"gui-preview", "Show a synthetic preview without receiving video."});
    parser.addOption({"video-trace", "Write frame summary CSV.", "path"});
    parser.addOption({"video-packet-trace", "Optional pre-jitter RTP packet CSV.", "path"});
    parser.addOption({"video-port", "Override YAML video.port.", "port"});
    parser.addOption({"metadata-port", "Override YAML metadata.port.", "port"});
    parser.addOption({"config", "Application YAML file (default: config/pc-application.yaml).", "path"});
    parser.addOption({"class-names", "UTF-8 class names file, one name per line starting at class 0.", "path"});
    parser.addOption({"video-latency", "Override YAML video.latency_ms.", "ms"});
    parser.process(app);
    QString configPath = parser.isSet("config") ? parser.value("config") : "config/pc-application.yaml";
    if (!parser.isSet("config") && !QFileInfo::exists(configPath))
        configPath = QCoreApplication::applicationDirPath() + "/config/pc-application.yaml";
    AppConfig config;
    try {
        config = loadAppConfig(configPath);
        const auto overrideValue = [&parser](const char *name, uint fallback, uint min, uint max) {
            if (!parser.isSet(name)) return fallback;
            bool valid = false;
            const auto value = parser.value(name).toUInt(&valid);
            if (!valid || value < min || value > max)
                throw std::runtime_error(std::string("Invalid --") + name);
            return value;
        };
        auto &settings = config.receiver;
        settings.videoPort = quint16(overrideValue("video-port", settings.videoPort, 1, 65535));
        settings.metadataPort = quint16(overrideValue("metadata-port", settings.metadataPort, 1, 65535));
        settings.latencyMs = overrideValue("video-latency", settings.latencyMs, 0, 10000);
        if (settings.videoPort == settings.metadataPort)
            throw std::runtime_error("Video and metadata UDP ports must differ");
    } catch (const std::exception &error) {
        qCritical().noquote() << "Settings:" << configPath << error.what();
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
    window.setVideoCodec(config.receiver.codec);
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
        ReceiverSettings settings = config.receiver;
        settings.tracePath = parser.value("video-trace");
        settings.packetTracePath = parser.value("video-packet-trace");
        if (!controller.start(settings)) return 1;
        controller.startControl(config.control);
    }
    window.show();

    return app.exec();
}
