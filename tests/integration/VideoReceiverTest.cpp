#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "video/VideoReceiver.h"
#include "ui/MainWindow.h"

class VideoReceiverTest : public QObject
{
    Q_OBJECT
private slots:
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
        MainWindow window(nullptr, false);
        window.setVideoCodec(codecName);
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
        MainWindow window(nullptr, false);
        window.setVideoCodec(codecName);
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
