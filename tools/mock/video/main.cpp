#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <gst/app/gstappsrc.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "video/VideoTrace.h"
#include "video/GStreamerRuntime.h"

namespace {
void recordRtpBuffer(GstBuffer *buffer, VideoTrace *trace)
{
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (buffer && gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
        if (gst_rtp_buffer_get_marker(&rtp))
            trace->record("sender_rtp",
                gst_util_uint64_scale_round(GST_BUFFER_PTS(buffer), 30, GST_SECOND) + 1,
                GST_BUFFER_PTS(buffer), gst_rtp_buffer_get_timestamp(&rtp), gst_rtp_buffer_get_seq(&rtp));
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
    parser.addOption({"frames", "Number of frames; 0 sends until interrupted.", "count", "0"});
    parser.addOption({"trace", "Timestamp CSV output.", "path"});
    parser.process(app);
    bool portOk = false;
    bool countOk = false;
    const uint port = parser.value("port").toUInt(&portOk);
    const uint frameLimit = parser.value("frames").toUInt(&countOk);
    if (!portOk || !port || port > 65535 || !countOk)
        parser.showHelp(1);
    configureGStreamerRuntime();
    GError *error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        qCritical() << error->message;
        g_clear_error(&error);
        return 1;
    }
    VideoTrace trace;
    if (!trace.open(parser.value("trace"))) {
        qCritical() << "Cannot open trace file";
        return 1;
    }
    GstElement *pipeline = gst_parse_launch(
        "appsrc name=source is-live=true format=time block=false "
        "caps=\"video/x-raw,format=RGB,width=640,height=480,framerate=30/1\" "
        "! videoconvert ! video/x-raw,format=I420 ! jpegenc quality=80 ! jpegparse "
        "! rtpjpegpay name=pay pt=26 mtu=1200 timestamp-offset=0 seqnum-offset=0 "
        "! udpsink name=output sync=true async=false", &error);
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
    timer.setInterval(2);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
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
        const quint64 pts = gst_util_uint64_scale(frameIndex, GST_SECOND, 30);
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
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, image.sizeInBytes(), nullptr);
        gst_buffer_fill(buffer, 0, image.constBits(), image.sizeInBytes());
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(frameIndex + 1, GST_SECOND, 30) - pts;
        trace.record("appsrc", frameIndex + 1, pts);
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
        qInfo() << "Sending RTP/JPEG to" << host << port;
        app.exec();
    }
    timer.stop();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(source);
    gst_object_unref(pipeline);
    return exitCode;
}
