#include <QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <limits>
#include <stdexcept>
#include "config/AppConfig.h"
#include "diagnostics/PerformanceRecorder.h"
#include "video/VideoTrace.h"

class PerformanceTest : public QObject
{
    Q_OBJECT
private slots:
    void yamlConfiguration()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("pc.yaml");
        const auto write = [&](const QByteArray &text) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        QVERIFY(write("video:\n  codec: jpeg\n"));
        auto settings = loadAppConfig(path).performance;
        QVERIFY(!settings.enabled);
        QCOMPARE(settings.outputDir, directory.filePath("logs"));
        QVERIFY(write("video:\n  codec: h264\nperformance:\n  enabled: true\n  output_dir: ../results\n"));
        settings = loadAppConfig(path).performance;
        QVERIFY(settings.enabled);
        QCOMPARE(settings.outputDir, QDir::cleanPath(directory.filePath("../results")));
        QVERIFY(write("video:\n  codec: jpeg\nperformance: []\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
        QVERIFY(write("video:\n  codec: jpeg\nperformance:\n  enabled: maybe\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
        QVERIFY(write("video:\n  codec: jpeg\nperformance:\n  output_dir: '  '\n"));
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
    }

    void liveStatisticsWithoutFile()
    {
        QTemporaryDir directory;
        PerformanceRecorder recorder;
        QSignalSpy samples(&recorder, &PerformanceRecorder::sampleReady);
        recorder.start({false, directory.filePath("not-created")}, {}, {});
        const qint64 first = VideoTrace::nowNs();
        recorder.observeFrame(1, first);
        recorder.observePaint(1, first + 12'000'000);
        recorder.observePaint(1, first + 30'000'000); // Overlay/repaint is not a second frame.
        recorder.observeFrame(2, -1); // A displayed frame can have no matching RTP time.
        recorder.observePaint(2, first + 40'000'000);
        recorder.observeFrame(3, first);
        recorder.resetFrames();
        recorder.observePaint(3, first + 50'000'000); // A previous stream cannot supply latency.
        recorder.addRtpBytes(125000);
        QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty(), 1800);
        const auto sample = qvariant_cast<PerformanceSample>(samples.first().first());
        QCOMPARE(sample.displayedFrames, quint64(2));
        QCOMPARE(sample.pcLatency.samples, quint64(1));
        QCOMPARE(sample.pcLatency.average(), 12.0);
        QCOMPARE(sample.rtpBytes, quint64(125000));
        QVERIFY(sample.intervalSeconds > 0.5);
        QVERIFY(sample.rtpMbps() > 0);
        QVERIFY(!QFileInfo::exists(directory.filePath("not-created")));
        recorder.stop();
        const auto empty = qvariant_cast<PerformanceSample>(samples.last().first());
        QCOMPARE(empty.pcLatency.samples, quint64(0));
        QCOMPARE(empty.pcLatency.average(), -1.0);
    }

    void singleFileAndFinalInterval()
    {
        QTemporaryDir directory;
        PerformanceRecorder recorder;
        ReceiverSettings receiver;
        receiver.codec = VideoCodec::H264;
        receiver.videoPort = 15100;
        receiver.latencyMs = 30;
        ControlSettings control;
        control.enabled = true;
        recorder.start({true, directory.path()}, receiver, control);
        const auto path = recorder.outputPath();
        const qint64 first = VideoTrace::nowNs();
        recorder.observeFrame(1, first);
        recorder.observePaint(1, first + 10'000'000);
        recorder.observeFrame(2, first);
        recorder.observePaint(2, first + 20'000'000);
        recorder.addPingResponse(1.5);
        recorder.addPingResponse(2.5);
        recorder.addPingResponse(std::numeric_limits<double>::quiet_NaN());
        recorder.disconnected();
        recorder.reconnectAttempted();
        recorder.ackTimedOut();
        QTest::qWait(30);
        recorder.stop();
        recorder.stop(); // Normal shutdown/destruction must not append another row.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines[0].startsWith("# codec=h264;"));
        QVERIFY(QRegularExpression("started_at=[^;]+(?:[+-][0-9]{2}:[0-9]{2}|Z);")
            .match(QString::fromUtf8(lines[0])).hasMatch());
        QVERIFY(lines[0].contains("video_port=15100;"));
        QVERIFY(lines[0].contains("jitter_latency_ms=30;"));
        QVERIFY(lines[0].contains("control_enabled=true;"));
        QVERIFY(lines[0].contains("control_host=127.0.0.1; control_port=5002;"));
        QVERIFY(lines[0].contains("ping_interval_ms=500; ack_timeout_ms=1500;"));
        QVERIFY(!lines[1].contains("codec"));
        const auto values = lines[2].split(',');
        QCOMPARE(values.size(), 16);
        QVERIFY(values[1].toDouble() > 0);
        QVERIFY(values[1].toDouble() < 1);
        QCOMPARE(values[2], QByteArray("2"));
        QCOMPARE(values[5], QByteArray("2"));
        QCOMPARE(values[6].toDouble(), 10.0);
        QCOMPARE(values[7].toDouble(), 15.0);
        QCOMPARE(values[8].toDouble(), 20.0);
        QCOMPARE(values[9], QByteArray("2"));
        QCOMPARE(values[11].toDouble(), 2.0);
        QCOMPARE(values[13], QByteArray("1"));
        QCOMPARE(values[14], QByteArray("1"));
        QCOMPARE(values[15], QByteArray("1"));
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);
        QVERIFY(QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!QFileInfo(path).fileName().contains("h264"));
    }

    void filenamesNeverOverwrite()
    {
        QTemporaryDir directory;
        PerformanceRecorder a, b;
        a.start({true, directory.path()}, {}, {});
        b.start({true, directory.path()}, {}, {});
        QVERIFY(a.outputPath() != b.outputPath());
        a.stop();
        b.stop();
        QCOMPARE(QDir(directory.path()).entryList({"*.csv"}, QDir::Files).size(), 2);
    }

    void fileErrorDoesNotStopLiveMeasurements()
    {
        QTemporaryDir directory;
        const auto blocked = directory.filePath("file");
        QFile blocker(blocked);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        PerformanceRecorder recorder;
        QSignalSpy errors(&recorder, &PerformanceRecorder::errorOccurred);
        QSignalSpy samples(&recorder, &PerformanceRecorder::sampleReady);
        recorder.start({true, blocked + "/logs"}, {}, {});
        QCOMPARE(errors.count(), 1);
        recorder.observeFrame(1, -1);
        recorder.observePaint(1, VideoTrace::nowNs());
        recorder.stop();
        QCOMPARE(qvariant_cast<PerformanceSample>(samples.last().first()).displayedFrames, quint64(1));
    }

    void analysisUsesSampleWeights()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath("trial.csv"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# codec=jpeg; started_at=2026-11-02T15:30:00+09:00\n"
            "elapsed_s,interval_s,displayed_frames,display_fps,rtp_mbps,pc_latency_samples,pc_latency_min_ms,pc_latency_avg_ms,pc_latency_max_ms,ping_samples,ping_rtt_min_ms,ping_rtt_avg_ms,ping_rtt_max_ms,control_disconnects,reconnect_attempts,ack_timeouts\n"
            "1,1,30,30,2,3,10,10,10,1,1,1,1,0,0,0\n"
            "1.5,0.5,10,20,4,1,30,30,30,1,3,3,3,1,2,1\n"
            "2,0.5,0,0,0,0,,,,0,,,,0,0,0\n");
        file.close();
        QProcess analysis;
        analysis.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            QFINDTESTDATA("../../tools/Measure-Performance.ps1"), "-Path", file.fileName()});
        QVERIFY(analysis.waitForFinished(10000));
        QCOMPARE(analysis.exitCode(), 0);
        const auto result = analysis.readAllStandardOutput();
        QVERIFY2(result.contains("15.000"), result.constData()); // (3*10 + 1*30) / 4, not 20.
        QVERIFY2(result.contains("20.000"), result.constData()); // 40 frames / 2 seconds.
        QVERIFY(result.contains("jpeg"));
    }
};

QTEST_GUILESS_MAIN(PerformanceTest)
#include "PerformanceTest.moc"
