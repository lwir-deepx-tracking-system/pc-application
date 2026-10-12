#include <QtTest>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include "video/VideoReceiver.h"
#include "ui/MainWindow.h"

class VideoReceiverTest : public QObject
{
    Q_OBJECT
private slots:
    void receiveAndRestart()
    {
        const QString outputDir = QCoreApplication::applicationDirPath();
        qputenv("GST_PLUGIN_PATH_1_0", (outputDir + "/gstreamer-1.0").toUtf8());
        VideoReceiver receiver;
        MainWindow window(nullptr, false);
        window.show();
        QObject::connect(&receiver, &VideoReceiver::frameReady, &window,
            [&window](const QImage &image, quint64, qint64) { window.setDisplayFrame(image, {}); });
        QObject::connect(&receiver, &VideoReceiver::fpsChanged, &window, &MainWindow::setVideoFps);
        QSignalSpy frames(&receiver, &VideoReceiver::frameReady);
        QSignalSpy fps(&receiver, &VideoReceiver::fpsChanged);
        QSignalSpy errors(&receiver, &VideoReceiver::errorOccurred);
        const bool started = receiver.start(15000, outputDir + "/receiver-trace.csv");
        QVERIFY2(started, errors.isEmpty() ? "Receiver startup failed" : qPrintable(errors.last().first().toString()));
        QProcess sender;
        sender.setProgram(outputDir + "/mock-video-sender");
        sender.setArguments({"--port", "15000", "--frames", "120", "--trace", outputDir + "/sender-trace.csv"});
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
        QCOMPARE(window.findChild<QComboBox *>("trackCombo")->count(), 0);
        QVERIFY(window.findChild<QLabel *>("videoFps")->text() != "--");
        window.grab().save(outputDir + "/video-receiver.png");
        QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 6000);
        QCOMPARE(sender.exitCode(), 0);
        QFile senderTrace(outputDir + "/sender-trace.csv");
        QVERIFY(senderTrace.open(QIODevice::ReadOnly));
        const QByteArray senderTraceData = senderTrace.readAll();
        QCOMPARE(senderTraceData.count("appsrc,"), 120);
        QCOMPARE(senderTraceData.count("sender_rtp,"), 120);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(heldImage.constBits()), heldImage.sizeInBytes()), heldBytes);
        int matched = 0;
        for (const auto &frame : frames) {
            QVERIFY(frame.at(1).toULongLong() != quint64(-1));
            if (frame.at(2).toLongLong() >= 0)
                ++matched;
        }
        QVERIFY2(matched > frames.count() * 0.8, "RTP marker PTS did not match decoded PTS; inspect trace");
        receiver.stop();
        const int beforeRestart = frames.count();
        QVERIFY(receiver.start(15000));
        sender.setArguments({"--port", "15000", "--frames", "30"});
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
};

QTEST_MAIN(VideoReceiverTest)
#include "VideoReceiverTest.moc"
