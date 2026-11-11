#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QtEndian>
#include <bit>
#include <limits>
#include "metadata/TrackingMetadata.h"
#include "metadata/MetadataReceiver.h"
#include "diagnostics/RuntimeTelemetryModel.h"
#include "app/ApplicationController.h"
#include "ui/MainWindow.h"

namespace {
QByteArray fixture()
{
    QFile file(QString(TEST_SOURCE_DIR) + "/tests/fixtures/runtime-telemetry.hex");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QByteArray::fromHex(file.readAll());
}
void put32(QByteArray &packet, int offset, quint32 value)
{ qToBigEndian(value, reinterpret_cast<uchar *>(packet.data() + offset)); }
void put64(QByteArray &packet, int offset, quint64 value)
{ qToBigEndian(value, reinterpret_cast<uchar *>(packet.data() + offset)); }
void putFloat(QByteArray &packet, int offset, float value)
{ put32(packet, offset, std::bit_cast<quint32>(value)); }
RuntimeTelemetry sample(quint64 id = 16)
{
    RuntimeTelemetry t;
    t.sessionId = 0x0102030405060708ULL; t.trackerEpoch = 1; t.frameId = id;
    t.modelId = t.trackerId = 1; t.validMask = RuntimeWire::ValidBits;
    for (int i = 0; i < RuntimeWire::MetricCount; ++i) t.values[i] = float(i);
    return t;
}
}

class RuntimeTelemetryTest : public QObject
{
    Q_OBJECT
private slots:
    void fixedBytesAndLegacy()
    {
        const auto bytes = fixture();
        QCOMPARE(bytes.size(), 28 + 124);
        TrackingMetadata data;
        QVERIFY(decodeTrackingMetadata(bytes, data));
        QCOMPARE(data.frameId, quint64(32));
        QVERIFY(data.tracks.isEmpty());
        QVERIFY(data.telemetry.has_value());
        QCOMPARE(data.telemetry->sessionId, quint64(0x0102030405060708ULL));
        QCOMPARE(data.telemetry->modelGeneration, quint64(0));
        QCOMPARE(data.telemetry->trackerEpoch, quint64(1));
        QCOMPARE(data.telemetry->frameId, quint64(16));
        QCOMPARE(data.telemetry->sampleAgeMs, 1.5f);
        QCOMPARE(data.telemetry->validMask, RuntimeWire::ValidBits);
        QCOMPARE(data.telemetry->modelId, quint32(1));
        QCOMPARE(data.telemetry->trackerId, quint32(1));
        for (int i = 0; i < 18; ++i) QCOMPARE(data.telemetry->values[i], float(i));
        QVERIFY(decodeTrackingMetadata(bytes.first(28), data));
        QVERIFY(!data.telemetry);

        // An ordinary BBox still uses its current frame's timestamp and original offsets.
        auto tracked = bytes.first(28);
        put32(tracked, 24, 1);
        QByteArray track(25, '\0');
        put32(track, 0, 7); putFloat(track, 4, 10); putFloat(track, 8, 20);
        putFloat(track, 12, 30); putFloat(track, 16, 40); putFloat(track, 21, .4f);
        tracked += track + bytes.mid(28);
        QVERIFY(decodeTrackingMetadata(tracked, data));
        QCOMPARE(data.rtpTimestamp, quint32(0x12345678));
        QCOMPARE(data.tracks.size(), 1);
        QCOMPARE(data.tracks.first().confidence, .4f);
        QCOMPARE(data.tracks.first().bbox, QRectF(10,20,30,40));
        for (int count : {41, 42}) {
            auto packet = bytes.first(28);
            put32(packet,24,quint32(count));
            for (int i = 0; i < count; ++i) packet += track;
            packet += bytes.mid(28);
            QCOMPARE(packet.size(), 28 + count * 25 + 124);
            QVERIFY(decodeTrackingMetadata(packet,data)); // Runtime, not PC, owns its configured 1200B cap.
            QCOMPARE(data.tracks.size(), count);
        }
    }

    void malformedPackets()
    {
        const auto good = fixture();
        TrackingMetadata output; output.frameId = 999;
        auto reject = [&](QByteArray bytes) {
            const bool rejected = !decodeTrackingMetadata(bytes, output);
            return rejected && output.frameId == 999;
        };
        for (int size = 29; size < good.size(); ++size) QVERIFY(reject(good.first(size)));
        QVERIFY(reject(good + '\0'));
        auto p = good; put32(p,24,0xffffffffU); QVERIFY(reject(p));
        p = good; put32(p,28,0); QVERIFY(reject(p));
        p = good; put32(p,28+36,1u<<18); QVERIFY(reject(p));
        p = good; putFloat(p,28+40,-1); QVERIFY(reject(p));
        p = good; putFloat(p,28+40,std::numeric_limits<float>::infinity()); QVERIFY(reject(p));
        p = good; put64(p,28+28,32); QVERIFY(reject(p)); // K must precede N.
        p = good; put64(p,28+4,0); QVERIFY(reject(p));
        p = good; put32(p,28+44,0); QVERIFY(reject(p));
        p = good; putFloat(p,28+52+4*5,std::numeric_limits<float>::quiet_NaN()); QVERIFY(reject(p));
        p = good; putFloat(p,28+52,-1); QVERIFY(reject(p));
        p = good; put32(p,28+36,0); // Unmeasured NaN slot is ignored, not converted to measured zero.
        putFloat(p,28+52,std::numeric_limits<float>::quiet_NaN());
        QVERIFY(decodeTrackingMetadata(p,output));
        QCOMPARE(output.telemetry->validMask, quint32(0));
    }

    void aggregationIdentityAndStale()
    {
        RuntimeTelemetryModel model;
        QSignalSpy sessions(&model, &RuntimeTelemetryModel::sessionChanged);
        model.start({});
        auto t = sample();
        QVERIFY(model.observe(t));
        QVERIFY(model.observe(t));
        auto v = model.view();
        QCOMPARE(v.state, RuntimeTelemetryView::State::Fresh);
        QCOMPARE(v.metrics[4].samples, quint64(1));
        t.frameId++; t.values[4] = 12;
        QVERIFY(model.observe(t));
        QCOMPARE(model.view().metrics[4].average(), 8.0);
        QCOMPARE(model.view().metrics[4].maximum, 12.0);
        t.frameId = 15; t.values[4] = 100;
        QVERIFY(model.observe(t));
        QCOMPARE(model.view().metrics[4].samples, quint64(2)); // Late raw row cannot rewind live statistics.
        t.frameId = 18; t.modelGeneration = 1; t.modelId = 2;
        QVERIFY(model.observe(t));
        QCOMPARE(model.view().metrics[4].samples, quint64(1));
        t.modelGeneration = 0;
        QVERIFY(!model.observe(t));
        model.clear(true);
        t.modelGeneration = 1; t.frameId++;
        QVERIFY(!model.observe(t));
        model.cancelTransition();
        QVERIFY(model.observe(t));
        model.clear(true);
        t.trackerEpoch++;
        QVERIFY(model.observe(t));
        t.sessionId++;
        QVERIFY(model.observe(t));
        t.sessionId--;
        QVERIFY(!model.observe(t));
        QCOMPARE(sessions.count(), 2);
        t.sessionId++; t.frameId++; t.sampleAgeMs = 2000;
        QVERIFY(model.observe(t));
        QCOMPARE(model.view().state, RuntimeTelemetryView::State::Stale);
        t.frameId++; t.sampleAgeMs = 1900;
        QVERIFY(model.observe(t));
        QCOMPARE(model.view().state, RuntimeTelemetryView::State::Fresh);
        QTest::qWait(130);
        QVERIFY(model.observe(t)); // Repetition does not refresh age.
        QCOMPARE(model.view().state, RuntimeTelemetryView::State::Stale);
    }

    void rawCsvAndAnalysis()
    {
        QTemporaryDir directory;
        RuntimeTelemetryModel model;
        QSignalSpy errors(&model, &RuntimeTelemetryModel::errorOccurred);
        model.start(directory.filePath("trial.csv"));
        auto t = sample();
        t.validMask = (1u << 4) | (1u << 5);
        t.values[4] = 0; t.values[5] = 0;
        model.observe(t); model.observe(t);
        t.frameId++; t.values[4] = 10; t.validMask = 1u << 4; model.observe(t);
        t.frameId++; t.values[4] = 20; model.observe(t);
        t.frameId++; t.modelGeneration++; t.modelId = 2; t.values[4] = 99; model.observe(t);
        model.stop();
        QVERIFY(errors.isEmpty());
        QFile file(model.outputPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().trimmed().split('\n');
        file.close();
        QCOMPARE(lines.size(), 5); // Header + four distinct raw samples.
        const auto fields = lines[1].trimmed().split(',');
        QCOMPARE(fields.size(), 28);
        QCOMPARE(fields[2], QByteArray("72623859790382856")); // uint64 preserved exactly.
        QCOMPARE(fields[14], QByteArray("0")); // Actual measured zero.
        QVERIFY(fields[10].isEmpty()); // Unmeasured camera value.
        QFile duplicate(model.outputPath());
        QVERIFY(duplicate.open(QIODevice::Append));
        duplicate.write(lines[1] + '\n'); // Analyzer independently deduplicates copied/combined data.
        duplicate.close();
        QProcess analysis;
        analysis.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            QString(TEST_SOURCE_DIR) + "/tools/Measure-RuntimeTelemetry.ps1", "-Path", model.outputPath(), "-AsJson"});
        QVERIFY(analysis.waitForFinished(15000));
        const auto output = analysis.readAllStandardOutput();
        QCOMPARE(analysis.exitCode(), 0);
        const auto result = QJsonDocument::fromJson(output).array();
        QCOMPARE(result.size(), 36); // Two models/generations, 18 metrics each.
        bool inferenceFound = false, npuFound = false;
        for (const auto &value : result) {
            const auto row = value.toObject();
            if (row["Model"].toString() != "1") continue;
            if (row["Metric"].toString() == "inference_latency_ms") {
                QCOMPARE(row["Samples"].toInt(), 3);
                QCOMPARE(row["MinMs"].toDouble(), 0.0);
                QCOMPARE(row["AvgMs"].toDouble(), 10.0);
                QCOMPARE(row["MaxMs"].toDouble(), 20.0);
                inferenceFound = true;
            }
            if (row["Metric"].toString() == "npu_core_ms") {
                QCOMPARE(row["Samples"].toInt(), 1);
                QCOMPARE(row["AvgMs"].toDouble(), 0.0);
                npuFound = true;
            }
            if (row["Metric"].toString() == "camera_capture_ms") QVERIFY(row["AvgMs"].isNull());
        }
        QVERIFY(inferenceFound && npuFound);
        analysis.start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            QString(TEST_SOURCE_DIR) + "/tools/Measure-RuntimeTelemetry.ps1", "-Path", model.outputPath(),
            "-FromElapsed", "0", "-ToElapsed", "60", "-ModelId", "2", "-AsJson"});
        QVERIFY(analysis.waitForFinished(15000));
        QCOMPARE(analysis.exitCode(), 0);
        const auto filtered = QJsonDocument::fromJson(analysis.readAllStandardOutput()).array();
        QCOMPARE(filtered.size(),18);
        for (const auto &row : filtered) QCOMPARE(row.toObject()["Model"].toString(), QString("2"));
    }

    void recordingFailureAndDisabled()
    {
        QTemporaryDir directory;
        RuntimeTelemetryModel model;
        QSignalSpy errors(&model, &RuntimeTelemetryModel::errorOccurred);
        model.start(directory.filePath("missing/trial.csv"));
        model.observe(sample());
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(model.view().state, RuntimeTelemetryView::State::Fresh);
        model.stop();
        model.start({});
        model.observe(sample()); model.stop();
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 0);
    }

    void udpToControllerAndUiWithoutVideo()
    {
        QUdpSocket reservation;
        QVERIFY(reservation.bind(QHostAddress::LocalHost, 0));
        const auto port = reservation.localPort(); reservation.close();
        QUdpSocket videoReservation;
        QVERIFY(videoReservation.bind(QHostAddress::LocalHost, 0));
        const auto videoPort = videoReservation.localPort(); videoReservation.close();
        ApplicationController controller;
        MainWindow window;
        connect(&controller, &ApplicationController::runtimeTelemetryChanged, &window, &MainWindow::setRuntimeTelemetry);
        QSignalSpy views(&controller, &ApplicationController::runtimeTelemetryChanged);
        ReceiverSettings settings;
        settings.videoPort = videoPort; settings.metadataPort = port;
        settings.videoBindAddress = settings.metadataBindAddress = "127.0.0.1";
        QVERIFY(controller.start(settings));
        QUdpSocket sender;
        auto packet = fixture();
        QCOMPARE(sender.writeDatagram(packet, QHostAddress::LocalHost, port), qint64(packet.size()));
        QTimer packets;
        quint64 metricFrame = 16;
        connect(&packets, &QTimer::timeout, &sender, [&] {
            ++metricFrame;
            put64(packet,4,metricFrame + 16); put64(packet,28+28,metricFrame);
            sender.writeDatagram(packet,QHostAddress::LocalHost,port);
        });
        packets.start(100);
        auto *status = window.findChild<QLabel *>("runtimeStatus");
        auto *inference = window.findChild<QLabel *>("runtimeMetric4");
        QVERIFY(status && inference);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QString("Fresh"), 1800);
        QCOMPARE(inference->text(), QString("4.00 / 4.00"));
        packets.stop();
        // A legacy packet immediately clears runtime display but remains valid metadata.
        sender.writeDatagram(packet.first(28), QHostAddress::LocalHost, port);
        QTRY_COMPARE(status->text(), QString("Unavailable"));
        QCOMPARE(inference->text(), QString("--"));
        controller.stop();
    }

    void panelLayout()
    {
        MainWindow window;
        RuntimeTelemetryView view;
        view.state = RuntimeTelemetryView::State::Fresh;
        for (int metric : RuntimeWire::DisplayMetrics) view.metrics[metric].add(double(metric));
        window.setRuntimeTelemetry(view);
        QImage image(640,480,QImage::Format_RGB32);
        image.fill(QColor("#18232a"));
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(),Qt::AlignCenter,"PROTOCOL TEST FIXTURE\nNot board measurements");
        painter.end();
        window.setDisplayFrame(image, {});
        window.show();
        QTest::qWait(50);
        QVERIFY(window.findChild<QLabel *>("runtimeMetric17"));
        const auto screenshot = qEnvironmentVariable("LWIR_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot));
        window.resize(800,560);
        QTest::qWait(30);
        QVERIFY(window.width() <= 800);
        QVERIFY(window.height() <= 560);
    }
};

QTEST_MAIN(RuntimeTelemetryTest)
#include "RuntimeTelemetryTest.moc"
