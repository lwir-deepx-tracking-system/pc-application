#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QPainter>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QDataStream>
#include <QUdpSocket>
#include <gst/app/gstappsrc.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "video/VideoTrace.h"
#include "video/GStreamerRuntime.h"

namespace {
void recordRtpBuffer(GstBuffer *buffer, VideoTrace *trace)
{
    if (!trace->active()) return;
    const qint64 observedNs = VideoTrace::nowNs();
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (buffer && gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
        trace->record("sender_rtp", 0, GST_BUFFER_PTS(buffer),
            gst_rtp_buffer_get_timestamp(&rtp), gst_rtp_buffer_get_seq(&rtp),
            gst_rtp_buffer_get_ssrc(&rtp), gst_rtp_buffer_get_marker(&rtp), observedNs);
        gst_rtp_buffer_unmap(&rtp);
    }
}

GstPadProbeReturn recordRtp(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *trace = static_cast<VideoTrace *>(data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        recordRtpBuffer(GST_PAD_PROBE_INFO_BUFFER(info), trace);
    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList *buffers = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        for (guint i = 0; i < gst_buffer_list_length(buffers); ++i)
            recordRtpBuffer(gst_buffer_list_get(buffers, i), trace);
    }
    return GST_PAD_PROBE_OK;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("640x480 / 30 FPS RTP/JPEG appsrc Mock Sender");
    parser.addHelpOption();
    parser.addOption({"host", "Destination IP.", "address", "127.0.0.1"});
    parser.addOption({"port", "Destination UDP port.", "port", "5000"});
    parser.addOption({"codec", "Codec: jpeg or h264 (software test sender).", "codec", "jpeg"});
    parser.addOption({"metadata-port", "Send synthetic LWIR tracking metadata to this UDP port; 0 disables.", "port", "0"});
    parser.addOption({"frames", "Number of frames; 0 sends until interrupted.", "count", "0"});
    parser.addOption({"trace", "Frame summary CSV output.", "path"});
    parser.addOption({"packet-trace", "Optional RTP packet CSV output.", "path"});
    parser.process(app);
    bool portOk = false;
    bool countOk = false;
    const uint port = parser.value("port").toUInt(&portOk);
    const uint frameLimit = parser.value("frames").toUInt(&countOk);
    const QString codec = parser.value("codec");
    bool metadataOk = false;
    const uint metadataPort = parser.value("metadata-port").toUInt(&metadataOk);
    if (!portOk || !port || port > 65535 || !countOk || !metadataOk || metadataPort > 65535
        || metadataPort == port || (codec != "jpeg" && codec != "h264"))
        parser.showHelp(1);
    configureGStreamerRuntime();
    GError *error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        qCritical() << error->message;
        g_clear_error(&error);
        return 1;
    }
    VideoTrace trace;
    if (!trace.open(parser.value("trace"), parser.value("packet-trace"))) {
        qCritical() << "Cannot open trace file";
        return 1;
    }
    const QString encoderPath = codec == "jpeg"
        ? "jpegenc quality=80 ! jpegparse ! rtpjpegpay name=pay pt=26"
        : "x264enc tune=zerolatency speed-preset=ultrafast bitrate=3000 key-int-max=30 bframes=0 byte-stream=true "
          "! h264parse ! rtph264pay name=pay pt=96 config-interval=-1";
    const QString description = QString(
        "appsrc name=source is-live=true format=time block=false "
        "caps=\"video/x-raw,format=RGB,width=640,height=480,framerate=30/1\" "
        "! videoconvert ! video/x-raw,format=I420 ! %1 mtu=1200 timestamp-offset=0 seqnum-offset=0 "
        "! udpsink name=output sync=true async=false").arg(encoderPath);
    GstElement *pipeline = gst_parse_launch(description.toUtf8().constData(), &error);
    if (error || !pipeline) {
        qCritical() << (error ? error->message : "Cannot create sender");
        g_clear_error(&error);
        if (pipeline) gst_object_unref(pipeline);
        return 1;
    }
    GstElement *source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    GstElement *output = gst_bin_get_by_name(GST_BIN(pipeline), "output");
    const QByteArray host = parser.value("host").toUtf8();
    g_object_set(output, "host", host.constData(), "port", port, nullptr);
    gst_object_unref(output);
    GstElement *pay = gst_bin_get_by_name(GST_BIN(pipeline), "pay");
    GstPad *pad = gst_element_get_static_pad(pay, "src");
    gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
        recordRtp, &trace, nullptr);
    gst_object_unref(pad);
    gst_object_unref(pay);
    GstBus *bus = gst_element_get_bus(pipeline);
    int exitCode = 0;
    quint64 frameIndex = 0;
    bool ending = false;
    QElapsedTimer clock;
    QTimer timer;
    QUdpSocket metadataSocket;
    timer.setInterval(2);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        trace.drain();
        if (GstMessage *message = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *messageError = nullptr;
                gchar *debug = nullptr;
                gst_message_parse_error(message, &messageError, &debug);
                qCritical() << messageError->message;
                g_clear_error(&messageError);
                g_free(debug);
                exitCode = 1;
            }
            gst_message_unref(message);
            app.quit();
            return;
        }
        const quint64 ticks = gst_util_uint64_scale(frameIndex, 90000, 30);
        const quint64 pts = gst_util_uint64_scale_ceil(ticks, GST_SECOND, 90000);
        if (ending || quint64(clock.nsecsElapsed()) < pts)
            return;
        QImage image(640, 480, QImage::Format_RGB888);
        image.fill(QColor(35, 35, 35));
        {
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            for (int x = 0; x < 640; x += 80) {
                const int gray = 25 + x / 4;
                painter.fillRect(x, 120, 80, 240, QColor(gray, gray, gray));
            }
            painter.setBrush(QColor(240, 240, 240));
            painter.drawEllipse(QRectF((frameIndex * 4) % 560, 200, 80, 80));
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setPixelSize(24);
            painter.setFont(font);
            painter.drawText(QRect(20, 20, 600, 60), QString("MOCK VIDEO  |  Frame %1").arg(frameIndex + 1));
        }
        const qint64 acquiredNs = VideoTrace::nowNs();
        if (metadataPort) {
            // Explicit mock annotations only; these are not inference results.
            QByteArray packet;
            QDataStream stream(&packet, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::BigEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            stream << quint32(0x4c574952) << quint64(frameIndex + 1)
                   << quint32(ticks) << quint64(0) << quint32(1)
                   << quint32(7) << float((frameIndex * 4) % 560)
                   << float(200) << float(80) << float(80) << quint8(0) << float(0.95);
            if (metadataSocket.writeDatagram(packet, QHostAddress(parser.value("host")), quint16(metadataPort)) != packet.size()) {
                exitCode = 1;
                app.quit();
                return;
            }
        }
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, image.sizeInBytes(), nullptr);
        gst_buffer_fill(buffer, 0, image.constBits(), image.sizeInBytes());
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_ceil((frameIndex + 1) * 3000, GST_SECOND, 90000) - pts;
        trace.record("appsrc", frameIndex + 1, pts, -1, -1, -1, false, acquiredNs);
        if (gst_app_src_push_buffer(GST_APP_SRC(source), buffer) != GST_FLOW_OK) {
            exitCode = 1;
            app.quit();
            return;
        }
        ++frameIndex;
        if (frameLimit && frameIndex >= frameLimit) {
            ending = true;
            gst_app_src_end_of_stream(GST_APP_SRC(source));
        }
    });
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        exitCode = 1;
    } else {
        clock.start();
        timer.start();
        qInfo() << "Sending RTP" << codec << "to" << host << port;
        app.exec();
    }
    timer.stop();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(source);
    gst_object_unref(pipeline);
    return exitCode;
}
