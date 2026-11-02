#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <stdexcept>
#include "video/VideoReceiver.h"
#include "ui/MainWindow.h"
#include "metadata/MetadataReceiver.h"
#include "metadata/FrameSynchronizer.h"
#include "config/AppConfig.h"
#include "app/ApplicationController.h"

class VideoReceiverTest : public QObject
{
    Q_OBJECT
private slots:
    void launchConfiguredApplication_data()
    {
        QTest::addColumn<QString>("codecName");
        QTest::addColumn<bool>("overridePorts");
        for (const auto &codec : {QString("jpeg"), QString("h264")}) {
            QTest::newRow(qPrintable(codec + "-yaml")) << codec << false;
            QTest::newRow(qPrintable(codec + "-override")) << codec << true;
        }
    }

    void launchConfiguredApplication()
    {
        QFETCH(QString, codecName);
        QFETCH(bool, overridePorts);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString configPath = directory.filePath("pc.yaml");
        QFile configFile(configPath);
        QVERIFY(configFile.open(QIODevice::WriteOnly));
        const auto videoPort = overridePorts ? "15104" : "15004";
        const auto metadataPort = overridePorts ? "15105" : "15005";
        configFile.write("video:\n  codec: " + codecName.toUtf8()
            + "\n  bind_address: 127.0.0.1\n  port: " + videoPort
            + "\n  latency_ms: 20\nmetadata:\n  bind_address: 127.0.0.1\n  port: " + metadataPort + "\n");
        configFile.close();
        const auto outputDir = QCoreApplication::applicationDirPath();
        const auto tracePath = directory.filePath("receiver.csv");
        QProcess application;
        application.setWorkingDirectory(directory.path());
        application.setProgram(outputDir + "/pc-application");
        QStringList arguments{"--config", configPath, "--video-trace", tracePath};
        if (overridePorts) arguments << "--video-port" << "15004" << "--metadata-port" << "15005"
                                     << "--video-latency" << "30";
        application.setArguments(arguments);
        application.start();
        QVERIFY(application.waitForStarted());
        QProcess sender;
        sender.setProgram(outputDir + "/mock-video-sender");
        sender.setArguments({"--port", "15004", "--metadata-port", "15005", "--codec", codecName, "--frames", "90"});
        sender.start();
        QVERIFY(sender.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 8000);
        QCOMPARE(sender.exitCode(), 0);
        QCOMPARE(application.state(), QProcess::Running);
        application.terminate(); // WM_CLOSE on Windows: flush the trace during normal shutdown.
        QVERIFY(application.waitForFinished(5000));
        QCOMPARE(application.exitCode(), 0);
        QFile traceFile(tracePath);
        QVERIFY(traceFile.open(QIODevice::ReadOnly));
        int displayed = 0;
        for (const auto &row : traceFile.readAll().split('\n')) {
            const auto fields = row.split(',');
            if (fields.size() == 17 && fields[14] == "1") ++displayed;
        }
        QVERIFY2(displayed > 20, "Configured GUI did not receive/display the selected codec");
    }

    void yamlCodecConfig()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("pc.yaml");
        const auto write = [&](const QByteArray &contents) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
        };
        QVERIFY(write("video:\n  codec: h264\n"));
        QCOMPARE(loadAppConfig(path).receiver.codec, VideoCodec::H264);
        QVERIFY(write("# Receiver settings\nvideo:\n  codec: 'jpeg' # matches board\n"));
        QCOMPARE(loadAppConfig(path).receiver.codec, VideoCodec::JPEG);
        QVERIFY(write("video:\n  codec: vp9\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
        QVERIFY(write("video: {}\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
        QVERIFY(write("video: [\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(directory.filePath("missing.yaml")), std::runtime_error);
        MainWindow window;
        QVERIFY(!window.findChild<QComboBox *>("videoCodec"));
        window.setVideoCodec(VideoCodec::H264);
        QCOMPARE(window.findChild<QLabel *>("videoCodec")->text(), QString("H.264"));
        window.setVideoCodec(VideoCodec::JPEG);
        QCOMPARE(window.findChild<QLabel *>("videoCodec")->text(), QString("JPEG"));
    }

    void yamlNetworkSettings()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("pc.yaml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("video:\n  codec: jpeg\n  bind_address: 127.0.0.1\n  port: 15100\n  latency_ms: 0\n"
                   "metadata:\n  bind_address: 0.0.0.0\n  port: 15101\n"
                   "control:\n  enabled: true\n  host: board.example\n  port: 15102\n");
        file.close();
        const auto config = loadAppConfig(path);
        QCOMPARE(config.receiver.videoPort, quint16(15100));
        QCOMPARE(config.receiver.metadataPort, quint16(15101));
        QCOMPARE(config.receiver.videoBindAddress, QString("127.0.0.1"));
        QCOMPARE(config.receiver.metadataBindAddress, QString("0.0.0.0"));
        QCOMPARE(config.receiver.latencyMs, uint(0));
        QCOMPARE(config.control.host, QString("board.example")); // Peer hostnames are allowed.
        QCOMPARE(config.control.port, quint16(15102));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("video:\n  codec: h264\n");
        file.close();
        const auto defaults = loadAppConfig(path).receiver;
        QCOMPARE(defaults.videoPort, quint16(5000));
        QCOMPARE(defaults.metadataPort, quint16(5001));
        QCOMPARE(defaults.latencyMs, uint(50));
        QCOMPARE(defaults.videoBindAddress, QString("0.0.0.0"));
    }

    void invalidYamlNetworkSettings_data()
    {
        QTest::addColumn<QByteArray>("yaml");
        QTest::newRow("zero-port") << QByteArray("video:\n  codec: jpeg\n  port: 0\n");
        QTest::newRow("port-overflow") << QByteArray("video:\n  codec: jpeg\n  port: 65536\n");
        QTest::newRow("negative-latency") << QByteArray("video:\n  codec: jpeg\n  latency_ms: -1\n");
        QTest::newRow("large-latency") << QByteArray("video:\n  codec: jpeg\n  latency_ms: 10001\n");
        QTest::newRow("bad-video-address") << QByteArray("video:\n  codec: jpeg\n  bind_address: board.example\n");
        QTest::newRow("ipv6-bind") << QByteArray("video:\n  codec: jpeg\n  bind_address: '::1'\n");
        QTest::newRow("pipeline-injection") << QByteArray("video:\n  codec: jpeg\n  bind_address: '127.0.0.1 ! fakesink'\n");
        QTest::newRow("metadata-type") << QByteArray("video:\n  codec: jpeg\nmetadata: []\n");
        QTest::newRow("bad-metadata-address") << QByteArray("video:\n  codec: jpeg\nmetadata:\n  bind_address: ''\n");
        QTest::newRow("bad-metadata-port") << QByteArray("video:\n  codec: jpeg\nmetadata:\n  port: -1\n");
        QTest::newRow("duplicate-udp-port") << QByteArray("video:\n  codec: jpeg\n  port: 5001\n");
    }

    void invalidYamlNetworkSettings()
    {
        QFETCH(QByteArray, yaml);
        QTemporaryDir directory;
        QFile file(directory.filePath("pc.yaml"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(yaml);
        file.close();
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(file.fileName()), std::runtime_error);
    }

    void invalidReceiverBindAddresses()
    {
        MetadataReceiver metadata;
        QSignalSpy metadataErrors(&metadata, &MetadataReceiver::errorOccurred);
        QVERIFY(!metadata.start(15103, "board.example"));
        QCOMPARE(metadataErrors.count(), 1);
        QVERIFY(metadata.start(15103, "127.0.0.1"));
        metadata.stop();
        VideoReceiver video;
        QSignalSpy videoErrors(&video, &VideoReceiver::errorOccurred);
        QVERIFY(!video.start(15104, {}, 50, {}, VideoCodec::JPEG, "127.0.0.1 ! fakesink"));
        QCOMPARE(videoErrors.count(), 1);
    }

    void classLabels()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath("classes.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("person\nbicycle\n\ncar\n");
        file.close();
        ClassLabels labels;
        QVERIFY(labels.load(file.fileName()));
        QCOMPARE(labels.label(0), QString("person"));
        QCOMPARE(labels.label(1), QString("bicycle"));
        QCOMPARE(labels.label(2), QString("Class 2")); // Blank lines must not shift later IDs.
        QCOMPARE(labels.label(3), QString("car"));
        QCOMPARE(labels.label(80), QString("Class 80"));
        QCOMPARE(labels.label(-1), QString("Unknown"));
        QCOMPARE(labels.label(255), QString("Unknown"));
        QVERIFY(!labels.load(directory.filePath("missing.txt")));
        QCOMPARE(labels.label(0), QString("person"));
        QVERIFY(labels.load(QFINDTESTDATA("../../config/class_names.txt")));
        QCOMPARE(labels.label(0), QString("person"));
        QCOMPARE(labels.label(2), QString("car"));
    }

    void decodeRuntimeMetadata()
    {
        // Independent fixture: frame 42, RTP 9000, ID 7, xywh 100/50/200/100.
        const auto packet = QByteArray::fromHex(
            "4c574952000000000000002a00002328000000000001e24000000001"
            "0000000742c80000424800004348000042c80000013f400000");
        TrackingMetadata decoded;
        QVERIFY(decodeTrackingMetadata(packet, decoded));
        QCOMPARE(decoded.frameId, quint64(42));
        QCOMPARE(decoded.rtpTimestamp, quint32(9000));
        QCOMPARE(decoded.guiStartedUs, quint64(123456));
        QCOMPARE(decoded.imageSize, QSize(640, 480));
        QCOMPARE(decoded.trackCount, quint32(1));
        QCOMPARE(decoded.tracks.size(), 1);
        QCOMPARE(decoded.tracks[0].trackId, quint32(7));
        QCOMPARE(decoded.tracks[0].classId, quint8(1));
        QCOMPARE(decoded.tracks[0].confidence, 0.75f);
        QCOMPARE(decoded.tracks[0].bbox, QRectF(100, 50, 200, 100));
        QVERIFY(!decodeTrackingMetadata(packet.left(27), decoded));
        QVERIFY(!decodeTrackingMetadata(packet.left(52), decoded));
        QVERIFY(!decodeTrackingMetadata(packet + 'x', decoded));
        auto bad = packet;
        bad[3] = 'M'; // Old LWIM magic is incompatible.
        QVERIFY(!decodeTrackingMetadata(bad, decoded));
        bad = packet;
        bad.replace(24, 4, QByteArray::fromHex("ffffffff")); // Count cannot exceed payload.
        QVERIFY(!decodeTrackingMetadata(bad, decoded));
        bad = packet;
        bad.replace(4, 8, QByteArray(8, '\0')); // Frame ID 0 is invalid, even with valid tracks.
        QVERIFY(!decodeTrackingMetadata(bad, decoded));
        QCOMPARE(decoded.frameId, quint64(42));
        bad = bad.left(28);
        bad.replace(24, 4, QByteArray(4, '\0'));
        QVERIFY(!decodeTrackingMetadata(bad, decoded)); // Header-only frames also require a valid ID.
        const auto oldPacket = QByteArray::fromHex(
            "4c57494d00020001000000000000002a00002328000000000001e24000000280000001e0"
            "0000000100000007000000013f40000042c80000424800004348000042c80000");
        QVERIFY(!decodeTrackingMetadata(oldPacket, decoded));
        QCOMPARE(decoded.tracks[0].bbox, QRectF(100, 50, 200, 100)); // Rejection is atomic.
        auto highIds = packet;
        highIds.replace(4, 8, QByteArray::fromHex("000000010000002a"));
        highIds.replace(28, 4, QByteArray::fromHex("ffffffff"));
        highIds.replace(32, 4, QByteArray::fromHex("42c88000")); // Fractional x = 100.25.
        highIds[48] = char(255);
        QVERIFY(decodeTrackingMetadata(highIds, decoded));
        QCOMPARE(decoded.frameId, quint64(0x10000002aULL));
        QCOMPARE(decoded.tracks[0].trackId, quint32(0xffffffff));
        QCOMPARE(decoded.tracks[0].classId, quint8(255));
        QCOMPARE(decoded.tracks[0].bbox.x(), 100.25);
        auto empty = packet.left(28);
        empty.replace(24, 4, QByteArray(4, '\0'));
        QVERIFY(decodeTrackingMetadata(empty, decoded));
        QCOMPARE(decoded.trackCount, quint32(0));
        QVERIFY(decoded.tracks.isEmpty());
    }

    void invalidTrackDoesNotDiscardOtherTracks_data()
    {
        QTest::addColumn<int>("offset");
        QTest::addColumn<QByteArray>("value");
        QTest::newRow("invalid-id") << 0 << QByteArray::fromHex("00000000");
        QTest::newRow("negative-width") << 12 << QByteArray::fromHex("bf800000");
        QTest::newRow("negative-height") << 16 << QByteArray::fromHex("bf800000");
        QTest::newRow("zero-width") << 12 << QByteArray(4, '\0');
        QTest::newRow("zero-height") << 16 << QByteArray(4, '\0');
        QTest::newRow("confidence-negative") << 21 << QByteArray::fromHex("bf800000");
        QTest::newRow("confidence-above-one") << 21 << QByteArray::fromHex("40000000");
        for (int offset : {4, 8, 12, 16, 21}) {
            QTest::newRow(qPrintable(QString("nan-%1").arg(offset))) << offset << QByteArray::fromHex("7fc00000");
            QTest::newRow(qPrintable(QString("inf-%1").arg(offset))) << offset << QByteArray::fromHex("7f800000");
        }
    }

    void invalidTrackDoesNotDiscardOtherTracks()
    {
        QFETCH(int, offset);
        QFETCH(QByteArray, value);
        auto header = QByteArray::fromHex("4c574952000000000000002a00002328000000000001e24000000003");
        const auto valid = QByteArray::fromHex("0000000742c80000424800004348000042c80000013f400000");
        auto bad = valid;
        bad.replace(offset, value.size(), value);
        TrackingMetadata decoded;
        QVERIFY(decodeTrackingMetadata(header + valid + bad + valid, decoded));
        QCOMPARE(decoded.trackCount, quint32(3));
        QCOMPARE(decoded.tracks.size(), 2);
        QCOMPARE(decoded.tracks[0].bbox, QRectF(100, 50, 200, 100));
        QCOMPARE(decoded.tracks[1].confidence, 0.75f);
        header.replace(24, 4, QByteArray::fromHex("00000001"));
        QVERIFY(decodeTrackingMetadata(header + bad, decoded));
        QCOMPARE(decoded.trackCount, quint32(1));
        QVERIFY(decoded.tracks.isEmpty());
    }

    void clipMetadataBoxes()
    {
        auto header = QByteArray::fromHex("4c574952000000000000002a00002328000000000001e24000000001");
        auto track = QByteArray::fromHex("00000007c1200000c1a0000041f0000042200000ff3f800000");
        TrackingMetadata decoded;
        QVERIFY(decodeTrackingMetadata(header + track, decoded));
        QCOMPARE(decoded.tracks[0].bbox, QRectF(0, 0, 20, 20)); // (-10,-20,30,40).
        track.replace(4, 16, QByteArray::fromHex("441d800043ed800042a0000042200000"));
        QVERIFY(decodeTrackingMetadata(header + track, decoded));
        QCOMPARE(decoded.tracks[0].bbox, QRectF(630, 475, 10, 5));
        track.replace(4, 4, QByteArray::fromHex("44200000")); // x = 640: fully outside.
        QVERIFY(decodeTrackingMetadata(header + track, decoded));
        QVERIFY(decoded.tracks.isEmpty());
    }

    void receiveMetadataDatagram()
    {
        MetadataReceiver receiver;
        QSignalSpy frames(&receiver, &MetadataReceiver::frameReady);
        const quint16 port = 15001;
        QVERIFY(receiver.start(port));
        QUdpSocket sender;
        const auto packet = QByteArray::fromHex(
            "4c574952000000000000002a00002328000000000001e24000000001"
            "0000000742c80000424800004348000042c80000013f400000");
        QCOMPARE(sender.writeDatagram(packet, QHostAddress::LocalHost, port), qint64(packet.size()));
        QTRY_COMPARE(frames.count(), 1);
        const auto decoded = qvariant_cast<TrackingMetadata>(frames[0][0]);
        QCOMPARE(decoded.tracks[0].bbox, QRectF(100, 50, 200, 100));
        sender.writeDatagram("invalid", QHostAddress::LocalHost, port);
        QTest::qWait(30);
        QCOMPARE(frames.count(), 1);
        receiver.stop();
    }

    void synchronizeMetadata()
    {
        FrameSynchronizer sync;
        QVector<TrackBox> shown;
        quint64 delivered = 0;
        connect(&sync, &FrameSynchronizer::frameReady, this,
            [&](const QImage &, const QVector<TrackBox> &tracks, quint64 id) { shown = tracks; delivered = id; });
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(Qt::gray);
        TrackingMetadata data;
        data.frameId = 42;
        data.rtpTimestamp = 9000;
        data.imageSize = image.size();
        data.trackCount = 1;
        data.tracks = {{7, QRectF(100, 50, 200, 100), 2}};
        sync.submitVideo(image, 9000, 17);
        QVERIFY(shown.isEmpty());
        sync.submitMetadata(data); // Late packet for the current frame.
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].classId, qint32(2));
        QCOMPARE(delivered, quint64(17)); // Preserve PC trace ID.
        sync.submitVideo(image, 12000, 18);
        QVERIFY(shown.isEmpty()); // Never carry boxes to an unrelated image.
        sync.submitMetadata(data);
        QVERIFY(shown.isEmpty());
        data.rtpTimestamp = 0xffffff00;
        sync.submitMetadata(data); // Early packet, including RTP wrap.
        sync.submitVideo(image, 0xffffff00, 19);
        QCOMPARE(shown.size(), 1);
        sync.submitVideo(image, 0, 20);
        QVERIFY(shown.isEmpty());
        data.rtpTimestamp = 0;
        data.imageSize = QSize(320, 240);
        sync.submitMetadata(data);
        QVERIFY(shown.isEmpty()); // Wrong source coordinate system.
        data.imageSize = image.size();
        sync.submitMetadata(data);
        QCOMPARE(shown.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(shown.isEmpty(), 1000); // Expire stalled overlays.
        sync.submitMetadata(data);
        QCOMPARE(shown.size(), 1);
        data.tracks.clear();
        data.trackCount = 0;
        sync.submitMetadata(data);
        QVERIFY(shown.isEmpty());
        sync.clear();
    }

    void synchronizeTrackCounts()
    {
        // Three wire records, but only one remains after record validation/clipping.
        const auto header = QByteArray::fromHex("4c574952000000000000002a00002328000000000001e24000000003");
        const auto valid = QByteArray::fromHex("0000000742c80000424800004348000042c80000013f400000");
        auto invalid = valid;
        invalid.replace(0, 4, QByteArray(4, '\0'));
        auto outside = valid;
        outside.replace(4, 4, QByteArray::fromHex("44200000")); // x = 640, no visible intersection.
        TrackingMetadata metadata;
        QVERIFY(decodeTrackingMetadata(header + valid + invalid + outside, metadata));
        QCOMPARE(metadata.trackCount, quint32(3));
        QCOMPARE(metadata.tracks.size(), 1);

        FrameSynchronizer sync;
        MainWindow window;
        connect(&sync, &FrameSynchronizer::frameReady, &window, &MainWindow::setDisplayFrame);
        connect(&sync, &FrameSynchronizer::trackCountsChanged, &window, &MainWindow::setTrackCounts);
        auto *count = window.findChild<QLabel *>("trackCount");
        auto *tracks = window.findChild<QComboBox *>("trackCombo");
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(Qt::gray);
        sync.submitVideo(image, 9000, 1);
        QCOMPARE(count->text(), QString("--"));
        sync.submitMetadata(metadata);
        QCOMPARE(count->text(), QString("3"));
        QCOMPARE(count->toolTip(), QString("Wire records: 3\nDisplayed boxes: 1"));
        QCOMPARE(tracks->count(), 1);
        for (auto *label : window.findChildren<QLabel *>())
            QVERIFY(label->text() != "Detection");

        sync.submitVideo(image, 12000, 2);
        QCOMPARE(count->text(), QString("--"));
        QCOMPARE(tracks->count(), 0);
        const auto empty = QByteArray::fromHex("4c574952000000000000002b00002ee0000000000001e24000000000");
        QVERIFY(decodeTrackingMetadata(empty, metadata));
        sync.submitMetadata(metadata);
        QCOMPARE(count->text(), QString("0"));
        QCOMPARE(count->toolTip(), QString("Wire records: 0\nDisplayed boxes: 0"));
        // A zero-track packet must expire to unknown, rather than remain a permanent zero.
        QTRY_COMPARE_WITH_TIMEOUT(count->text(), QString("--"), 1000);
        sync.submitMetadata(metadata);
        QCOMPARE(count->text(), QString("0"));
        sync.clear();
        QCOMPARE(count->text(), QString("--"));
    }

    void resizeBoundingBoxes_data()
    {
        QTest::addColumn<QSize>("widgetSize");
        QTest::addColumn<QSize>("sourceSize");
        QTest::newRow("small") << QSize(320, 240) << QSize(640, 480);
        QTest::newRow("wide-letterbox") << QSize(1280, 720) << QSize(640, 480);
        QTest::newRow("tall-letterbox") << QSize(640, 800) << QSize(640, 480);
        QTest::newRow("different-source-size") << QSize(960, 640) << QSize(800, 600);
    }

    void resizeBoundingBoxes()
    {
        QFETCH(QSize, widgetSize);
        QFETCH(QSize, sourceSize);
        VideoWidget widget;
        widget.resize(widgetSize);
        QImage source(sourceSize, QImage::Format_RGB888);
        source.fill(Qt::gray);
        widget.setDisplayFrame(source, {{7, QRectF(100, 50, 200, 100)}}, 1);
        widget.show();
        QTest::qWait(30);
        const auto rect = widget.videoRect();
        const double scale = rect.width() / sourceSize.width();
        QSignalSpy selected(&widget, &VideoWidget::autoRequested);
        QSignalSpy cleared(&widget, &VideoWidget::manualRequested);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
            (rect.topLeft() + QPointF(200, 100) * scale).toPoint());
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected[0][0].toUInt(), quint32(7));
        if (rect.x() > 10 || rect.y() > 10) {
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
            QCOMPARE(cleared.count(), 0); // Padding is not part of the source image.
        }
        const auto rendered = widget.grab().toImage();
        // QWidget positions are logical pixels; the screenshot stores physical pixels.
        const auto border = ((rect.topLeft() + QPointF(250, 50) * scale) * rendered.devicePixelRatio()).toPoint();
        bool foundBorder = false;
        for (int y = border.y() - 2; y <= border.y() + 2; ++y)
            for (int x = border.x() - 2; x <= border.x() + 2; ++x) {
                const auto color = rendered.pixelColor(x, y);
                if (color.green() > color.red() + 50 && color.blue() > color.red()) foundBorder = true;
            }
        QVERIFY2(foundBorder, "Rendered xywh border does not follow image scale/padding");
    }

    void paintTracksOnlyLatestFrame()
    {
        VideoWidget widget;
        widget.show();
        QTest::qWait(50);
        QSignalSpy painted(&widget, &VideoWidget::framePainted);
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(Qt::gray);
        widget.setDisplayFrame(image, {}, 1);
        widget.setDisplayFrame(image, {}, 2);
        QTRY_COMPARE(painted.count(), 1);
        QCOMPARE(painted.first().first().toULongLong(), quint64(2));
        widget.repaint();
        QCOMPARE(painted.count(), 1);
        widget.setDisplayFrame(image, {{7, QRectF(100, 50, 200, 100)}}, 2);
        widget.repaint();
        QCOMPARE(painted.count(), 1); // A metadata-only overlay update is not a new frame.
        widget.setDisplayFrame({}, {}, 3);
        widget.repaint();
        QCOMPARE(painted.count(), 1);
    }

    void traceAggregation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        VideoTrace trace;
        const auto path = directory.filePath("frames.csv");
        QVERIFY(!trace.open(path, path));
        QVERIFY(trace.open(path));
        // A marker can arrive before another fragment. Arrival order and SSRC matter.
        trace.record("receiver_packet", 0, quint64(-1), 9000, 10, 123, true, 100);
        trace.record("receiver_packet", 0, quint64(-1), 9000, 9, 123, false, 110);
        trace.record("receiver_rtp", 0, 500, 9000, 10, 123, true, 115);
        trace.record("appsink", 1, 500, 9000, -1, 123, false, 120);
        trace.painted(1, 130);
        trace.painted(1, 999); // Resize/expose must retain the first draw time.
        trace.record("receiver_packet", 0, quint64(-1), 9000, 10, 456, true, 140);
        trace.close();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = file.readAll().trimmed().split('\n');
        QCOMPARE(rows.size(), 3);
        const auto drawn = rows.at(1).split(',');
        QCOMPARE(drawn.at(4), QByteArray("123"));
        QCOMPARE(drawn.at(6), QByteArray("10"));
        QCOMPARE(drawn.at(7), QByteArray("9"));
        QCOMPARE(drawn.at(8), QByteArray("100"));
        QCOMPARE(drawn.at(9), QByteArray("110"));
        QCOMPARE(drawn.at(10), QByteArray("100"));
        QCOMPARE(drawn.at(13), QByteArray("130"));
        QCOMPARE(drawn.at(14), QByteArray("1"));
        QCOMPARE(rows.at(2).split(',').at(14), QByteArray("0"));
        QVERIFY(!QFile::exists(directory.filePath("packets.csv")));
        file.close();
        QVERIFY(trace.open({}, directory.filePath("packets.csv")));
        trace.record("receiver_packet", 0, quint64(-1), 9000, 10, 123, true, 100);
        trace.close();
        QFile packets(directory.filePath("packets.csv"));
        QVERIFY(packets.open(QIODevice::ReadOnly));
        QCOMPARE(packets.readAll().count("receiver,"), 1);
    }

    void boundedTraceStorage()
    {
        QTemporaryDir directory;
        VideoTrace trace;
        const auto path = directory.filePath("frames.csv");
        QVERIFY(trace.open(path));
        for (int i = 0; i < 600; ++i) {
            // Timestamp wrap must not change insertion-order eviction.
            const quint32 rtp = quint32(0xffffff00u + quint32(i));
            trace.record("receiver_packet", 0, quint64(-1), rtp, i, 123, true, 100 + i);
            trace.drain();
        }
        trace.close();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().count("receiver,"), 600);
    }

    void stableTrackList()
    {
        MainWindow window;
        window.setBoardState({});
        window.setControlConnection(Control::Connection::Ready);
        auto *combo = window.findChild<QComboBox *>("trackCombo");
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(Qt::gray);
        window.setDisplayFrame(image, {{7, QRectF(100, 50, 200, 100), 0}}, 1);
        combo->setEditText("4294967295");
        QSignalSpy removed(combo->model(), &QAbstractItemModel::rowsRemoved);
        window.setDisplayFrame(image, {{7, QRectF(105, 50, 200, 100), 0}}, 2);
        QCOMPARE(removed.count(), 0); // A moving box must not rebuild an open selection list.
        QCOMPARE(combo->currentText(), QString("4294967295"));
        QSignalSpy selected(&window, &MainWindow::autoRequested);
        QTest::mouseClick(window.findChild<QPushButton *>("autoButton"), Qt::LeftButton);
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected[0][0].toUInt(), quint32(0xffffffff));
        window.resetReception();
        QCOMPARE(combo->count(), 0);
        QCOMPARE(window.findChild<QLabel *>("trackCount")->text(), QString("--"));
    }

    void controllerStartupRecovery()
    {
        ApplicationController controller;
        QSignalSpy errors(&controller, &ApplicationController::errorOccurred);
        ReceiverSettings settings;
        settings.videoBindAddress = "127.0.0.1";
        settings.metadataBindAddress = "127.0.0.1";
        settings.videoPort = 15010;
        settings.metadataPort = 15011;
        QTemporaryDir directory;
        settings.tracePath = directory.filePath("missing/trace.csv");
        QVERIFY(!controller.start(settings));
        QCOMPARE(errors.count(), 1);
        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress::AnyIPv4, settings.metadataPort, QAbstractSocket::DontShareAddress));
        probe.close(); // Failed video startup must release the already-bound metadata socket.
        settings.tracePath.clear();
        QVERIFY(controller.start(settings));
        controller.stop();
        QVERIFY(probe.bind(QHostAddress::AnyIPv4, settings.metadataPort, QAbstractSocket::DontShareAddress));
        probe.close();
        settings.metadataPort = settings.videoPort;
        QVERIFY(!controller.start(settings));
        QCOMPARE(errors.count(), 2);
    }

    void receiveUsingController_data() { receiveAndRestart_data(); }

    void receiveUsingController()
    {
        QFETCH(QString, codecName);
        ReceiverSettings settings;
        settings.videoBindAddress = "127.0.0.1";
        settings.metadataBindAddress = "127.0.0.1";
        settings.videoPort = 15010;
        settings.metadataPort = 15011;
        settings.codec = codecName == "jpeg" ? VideoCodec::JPEG : VideoCodec::H264;
        ApplicationController controller;
        MainWindow window;
        connect(&controller, &ApplicationController::frameReady, &window, &MainWindow::setDisplayFrame);
        connect(&controller, &ApplicationController::trackCountsChanged, &window, &MainWindow::setTrackCounts);
        connect(&controller, &ApplicationController::streamReset, &window, &MainWindow::resetReception);
        connect(&window, &MainWindow::framePainted, &controller, &ApplicationController::recordPaint);
        window.show();
        QSignalSpy errors(&controller, &ApplicationController::errorOccurred);
        QSignalSpy counts(&controller, &ApplicationController::trackCountsChanged);
        for (int run = 0; run < 2; ++run) {
            QTemporaryDir directory;
            settings.tracePath = directory.filePath("frames.csv");
            QVERIFY(controller.start(settings));
            auto *count = window.findChild<QLabel *>("trackCount");
            QCOMPARE(count->text(), QString("--"));
            QProcess sender;
            sender.setProgram(QCoreApplication::applicationDirPath() + "/mock-video-sender");
            sender.setArguments({"--port", "15010", "--metadata-port", "15011", "--frames", "45", "--codec", codecName});
            sender.start();
            QVERIFY(sender.waitForStarted());
            QTRY_COMPARE_WITH_TIMEOUT(count->text(), QString("1"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 5000);
            QCOMPARE(sender.exitCode(), 0);
            controller.stop();
            QCOMPARE(count->text(), QString("--"));
            QCOMPARE(window.findChild<QComboBox *>("trackCombo")->count(), 0);
            QVERIFY(!counts.isEmpty());
            QCOMPARE(counts.last()[2].toBool(), false);
            QFile trace(settings.tracePath);
            QVERIFY(trace.open(QIODevice::ReadOnly));
            int displayed = 0;
            for (const auto &row : trace.readAll().split('\n')) {
                const auto columns = row.split(',');
                if (columns.size() == 17 && columns[14] == "1") ++displayed;
            }
            QVERIFY2(displayed > 10, "Controller did not record displayed frames");
        }
        QCOMPARE(errors.count(), 0);
    }

    void receiveBoxes_data() { receiveAndRestart_data(); }

    void receiveBoxes()
    {
        QFETCH(QString, codecName);
        const auto codec = codecName == "jpeg" ? VideoReceiver::Codec::JPEG : VideoReceiver::Codec::H264;
        VideoReceiver video;
        MetadataReceiver metadata;
        FrameSynchronizer sync;
        MainWindow window;
        ClassLabels labels;
        QVERIFY(labels.load(QFINDTESTDATA("../../config/class_names.txt")));
        window.setClassLabels(labels);
        window.setVideoCodec(codecName == "jpeg" ? VideoCodec::JPEG : VideoCodec::H264);
        window.show();
        qint64 currentTimestamp = -1;
        int matched = 0;
        bool wrongBox = false;
        connect(&video, &VideoReceiver::frameReady, &sync,
            [&](const QImage &image, quint64, qint64 timestamp, quint64 id) {
                currentTimestamp = timestamp;
                sync.submitVideo(image, timestamp, id);
            });
        connect(&metadata, &MetadataReceiver::frameReady, &sync, &FrameSynchronizer::submitMetadata);
        connect(&sync, &FrameSynchronizer::frameReady, &window,
            [&](const QImage &image, const QVector<TrackBox> &tracks, quint64 id) {
                window.setDisplayFrame(image, tracks, id);
                if (!tracks.isEmpty()) {
                    ++matched;
                    const QRectF expected((quint32(currentTimestamp) / 3000 * 4) % 560, 200, 80, 80);
                    if (tracks.size() != 1 || tracks[0].trackId != 7 || tracks[0].classId != 0 || tracks[0].bbox != expected) wrongBox = true;
                }
            });
        connect(&metadata, &MetadataReceiver::fpsChanged, &window, &MainWindow::setMetadataFps);
        connect(&sync, &FrameSynchronizer::trackCountsChanged, &window, &MainWindow::setTrackCounts);
        QSignalSpy videoErrors(&video, &VideoReceiver::errorOccurred);
        QSignalSpy metadataErrors(&metadata, &MetadataReceiver::errorOccurred);
        QVERIFY(metadata.start(15003));
        QVERIFY(video.start(15002, {}, 50, {}, codec));
        QProcess sender;
        const auto outputDir = QCoreApplication::applicationDirPath();
        sender.setProgram(outputDir + "/mock-video-sender");
        sender.setArguments({"--port", "15002", "--metadata-port", "15003", "--frames", "90", "--codec", codecName});
        sender.start();
        QVERIFY(sender.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(matched > 30, 8000);
        QVERIFY2(!wrongBox, "Box xywh was matched to the wrong RTP frame");
        QCOMPARE(window.findChild<QComboBox *>("trackCombo")->count(), 1);
        QCOMPARE(window.findChild<QLabel *>("trackCount")->text(), QString("1"));
        QTRY_VERIFY(window.findChild<QLabel *>("metadataFps")->text() != "--");
        window.grab().save(outputDir + "/metadata-" + codecName + ".png");
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 6000);
        QCOMPARE(sender.exitCode(), 0);
        QVERIFY(!wrongBox);
        QCOMPARE(videoErrors.count(), 0);
        QCOMPARE(metadataErrors.count(), 0);
        video.stop();
        metadata.stop();
    }

    void receiveAndRestart_data()
    {
        QTest::addColumn<QString>("codecName");
        QTest::newRow("jpeg") << QString("jpeg");
        QTest::newRow("h264") << QString("h264");
    }

    void receiveAndRestart()
    {
        QFETCH(QString, codecName);
        const auto codec = codecName == "jpeg" ? VideoReceiver::Codec::JPEG : VideoReceiver::Codec::H264;
        const QString outputDir = QCoreApplication::applicationDirPath();
        qputenv("GST_PLUGIN_PATH_1_0", (outputDir + "/gstreamer-1.0").toUtf8());
        VideoReceiver receiver;
        MainWindow window;
        window.setVideoCodec(codecName == "jpeg" ? VideoCodec::JPEG : VideoCodec::H264);
        window.show();
        QObject::connect(&receiver, &VideoReceiver::frameReady, &window,
            [&window](const QImage &image, quint64, qint64, quint64 frameId) {
                window.setDisplayFrame(image, {}, frameId);
            });
        QObject::connect(&window, &MainWindow::framePainted, &receiver, &VideoReceiver::recordPaint);
        QObject::connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
        QSignalSpy frames(&receiver, &VideoReceiver::frameReady);
        QSignalSpy fps(&receiver, &VideoReceiver::fpsChanged);
        QSignalSpy errors(&receiver, &VideoReceiver::errorOccurred);
        QSignalSpy processing(&receiver, &VideoReceiver::processingTimeChanged);
        const bool started = receiver.start(15000, outputDir + "/receiver-trace.csv", 50,
                                             outputDir + "/receiver-packets.csv", codec);
        QVERIFY2(started, errors.isEmpty() ? "Receiver startup failed" : qPrintable(errors.last().first().toString()));
        QProcess sender;
        sender.setProgram(outputDir + "/mock-video-sender");
        sender.setArguments({"--port", "15000", "--frames", "120", "--trace", outputDir + "/sender-trace.csv",
                             "--packet-trace", outputDir + "/sender-packets.csv", "--codec", codecName});
        sender.start();
        QVERIFY(sender.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 60, 8000);
        QCOMPARE(errors.count(), 0);
        const QImage heldImage = qvariant_cast<QImage>(frames.first().at(0));
        QCOMPARE(heldImage.size(), QSize(640, 480));
        const QByteArray heldBytes(reinterpret_cast<const char *>(heldImage.constBits()), heldImage.sizeInBytes());
        QTRY_VERIFY_WITH_TIMEOUT(!fps.isEmpty(), 2000);
        QVERIFY(fps.last().first().toDouble() > 20.0);
        QVERIFY(fps.last().first().toDouble() < 40.0);
        QVERIFY(!processing.isEmpty());
        QVERIFY(processing.last().first().toDouble() > 0.0);
        QVERIFY(processing.last().first().toDouble() < 1000.0);
        QCOMPARE(window.findChild<QComboBox *>("trackCombo")->count(), 0);
        QVERIFY(window.findChild<QLabel *>("videoFps")->text() != "--");
        window.grab().save(outputDir + "/video-receiver-" + codecName + ".png");
        window.hide(); // Continued reception without paint must not mean network loss.
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 6000);
        QCOMPARE(sender.exitCode(), 0);
        QFile senderTrace(outputDir + "/sender-trace.csv");
        QVERIFY(senderTrace.open(QIODevice::ReadOnly));
        const QByteArray senderTraceData = senderTrace.readAll();
        QCOMPARE(senderTraceData.count("sender,"), 120);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(heldImage.constBits()), heldImage.sizeInBytes()), heldBytes);
        int matched = 0;
        for (const auto &frame : frames) {
            QVERIFY(frame.at(1).toULongLong() != quint64(-1));
            if (frame.at(2).toLongLong() >= 0)
                ++matched;
        }
        QVERIFY2(matched > frames.count() * 0.8, "RTP marker PTS did not match decoded PTS; inspect trace");
        receiver.stop();
        const auto loadCsv = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return QList<QByteArray>{};
            auto rows = file.readAll().trimmed().split('\n');
            rows.removeFirst();
            return rows;
        };
        const auto senderPackets = loadCsv(outputDir + "/sender-packets.csv");
        const auto receiverPackets = loadCsv(outputDir + "/receiver-packets.csv");
        QVERIFY(senderPackets.size() > 120);
        QVERIFY(receiverPackets.size() > 120);
        int markerCount = 0;
        for (const auto &row : senderPackets) {
            const auto fields = row.split(',');
            QCOMPARE(fields.size(), 8);
            if (fields.at(4) == "1") ++markerCount;
        }
        QCOMPARE(markerCount, 120);
        int displayed = 0, skipped = 0;
        for (const auto &row : loadCsv(outputDir + "/receiver-trace.csv")) {
            const auto fields = row.split(',');
            QCOMPARE(fields.size(), 17);
            QCOMPARE(fields.at(15), QByteArray("local_steady_clock"));
            QCOMPARE(fields.at(16), QByteArray("ns"));
            if (fields.at(14) == "1") {
                ++displayed;
                QVERIFY(!fields.at(8).isEmpty());
                QVERIFY(!fields.at(10).isEmpty());
                QVERIFY(!fields.at(12).isEmpty());
                QCOMPARE(fields.at(2), fields.at(3));
                QVERIFY(fields.at(12).toLongLong() >= fields.at(10).toLongLong());
                QVERIFY(fields.at(13).toLongLong() >= fields.at(12).toLongLong());
            } else if (!fields.at(12).isEmpty()) {
                ++skipped;
                QVERIFY(fields.at(13).isEmpty());
            }
        }
        QVERIFY(displayed > 20);
        QVERIFY(skipped > 10);
        const int beforeRestart = frames.count();
        QVERIFY(receiver.start(15000, {}, 50, {}, codec));
        // Allow another IDR after startup loss; H.264 late join is bounded by GOP.
        sender.setArguments({"--port", "15000", "--frames", "90", "--codec", codecName});
        sender.start();
        QVERIFY(sender.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > beforeRestart + 10, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 5000);
        receiver.stop();
        QCOMPARE(errors.count(), 0);
    }

    void invalidTracePath()
    {
        VideoReceiver receiver;
        QSignalSpy errors(&receiver, &VideoReceiver::errorOccurred);
        QVERIFY(!receiver.start(15000, "directory-that-does-not-exist/trace.csv"));
        QCOMPARE(errors.count(), 1);
        receiver.stop();
    }

    void receiveBoard_data() { receiveAndRestart_data(); }

    void receiveBoard()
    {
        if (!qEnvironmentVariableIsSet("LWIR_BOARD_VIDEO_HOST"))
            QSKIP("Set LWIR_BOARD_VIDEO_HOST to this PC's board-reachable IP for MPP integration");
        QFETCH(QString, codecName);
        VideoReceiver receiver;
        MainWindow window;
        window.setVideoCodec(codecName == "jpeg" ? VideoCodec::JPEG : VideoCodec::H264);
        window.show();
        connect(&receiver, &VideoReceiver::frameReady, &window,
            [&window](const QImage &image, quint64, qint64, quint64 id) { window.setDisplayFrame(image, {}, id); });
        connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
        connect(&receiver, &VideoReceiver::processingTimeChanged, &window, &MainWindow::setVideoProcessingTime);
        QSignalSpy frames(&receiver, &VideoReceiver::frameReady);
        QSignalSpy processing(&receiver, &VideoReceiver::processingTimeChanged);
        QSignalSpy errors(&receiver, &VideoReceiver::errorOccurred);
        QVERIFY(receiver.start(5000, {}, 50, {}, codecName == "jpeg"
            ? VideoReceiver::Codec::JPEG : VideoReceiver::Codec::H264));
        const QString host = qEnvironmentVariable("LWIR_BOARD_VIDEO_HOST");
        // Only numeric IPv4 addresses are passed to the remote shell.
        QVERIFY(QRegularExpression("^[0-9.]+$").match(host).hasMatch());
        QProcess sender;
        const bool relay = qEnvironmentVariableIsSet("LWIR_BOARD_VIDEO_RELAY");
        if (relay) {
            sender.setProgram("python");
            sender.setArguments({QFINDTESTDATA("../../tools/mock/video/board_relay.py"), codecName});
        } else {
            sender.setProgram("ssh");
            sender.setArguments({"-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "pi",
                "/home/orangepi/.cache/lwir-runtime/build/lwir_video_send --synthetic --seconds 5 --codec "
                    + codecName + " --host " + host});
        }
        sender.start();
        QVERIFY(sender.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 15000);
        QCOMPARE(sender.exitCode(), 0);
        qInfo().noquote() << sender.readAllStandardOutput();
        qInfo() << "transport:" << (relay ? "SSH relay (functional test only)" : "direct UDP");
        QCOMPARE(errors.count(), 0);
        QVERIFY2(frames.count() > 60, "Not enough board frames: check routing and UDP 5000 firewall");
        QVERIFY(!processing.isEmpty());
        qInfo() << codecName << "received" << frames.count() << "frames; last average decode+delivery ms:"
                << processing.last().first().toDouble();
        window.grab().save(QCoreApplication::applicationDirPath() + "/board-" + codecName + ".png");
        receiver.stop();
    }
};

QTEST_MAIN(VideoReceiverTest)
#include "VideoReceiverTest.moc"
