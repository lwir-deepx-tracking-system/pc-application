#include "VideoReceiver.h"
#include "GStreamerRuntime.h"
#include "VideoPipeline.h"

#include <gst/app/gstappsink.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

VideoReceiver::VideoReceiver(QObject *parent) : QObject(parent)
{
    timer.setInterval(5);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &VideoReceiver::poll);
}

VideoReceiver::~VideoReceiver()
{
    stop();
}

bool VideoReceiver::start(quint16 port, const QString &tracePath, uint latencyMs,
                          const QString &packetTracePath, Codec codec)
{
    stop();
    configureGStreamerRuntime();
    GError *error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        emit errorOccurred(error ? QString::fromUtf8(error->message) : "Cannot initialize GStreamer");
        g_clear_error(&error);
        return false;
    }
    if (!trace.open(tracePath, packetTracePath)) {
        emit errorOccurred("Cannot open video trace paths (use separate summary and packet files): "
                           + tracePath + "; " + packetTracePath);
        return false;
    }
    const QString description = makeReceiverPipeline(codec, port, latencyMs);
    pipeline = gst_parse_launch(description.toUtf8().constData(), &error);
    if (error || !pipeline) {
        const QString message = error ? QString::fromUtf8(error->message) : "Cannot create video pipeline";
        g_clear_error(&error);
        stop();
        emit errorOccurred(message);
        return false;
    }
    sink = gst_bin_get_by_name(GST_BIN(pipeline), "frames");
    const auto addProbe = [this](const char *elementName, const char *padName,
                                GstPadProbeType type, GstPadProbeCallback callback) {
        GstElement *element = gst_bin_get_by_name(GST_BIN(pipeline), elementName);
        if (!element) return false;
        GstPad *pad = gst_element_get_static_pad(element, padName);
        gst_object_unref(element);
        if (!pad) return false;
        const auto probe = gst_pad_add_probe(pad, type, callback, this, nullptr);
        gst_object_unref(pad);
        return probe != 0;
    };
    if (!sink || !addProbe("depay", "sink", GST_PAD_PROBE_TYPE_BUFFER, &VideoReceiver::inspectRtp)
        || !addProbe("input", "src", GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                     &VideoReceiver::inspectIncomingRtp)) {
        stop();
        emit errorOccurred("Cannot attach video pipeline observers");
        return false;
    }
    bus = gst_element_get_bus(pipeline);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        emit errorOccurred("Cannot start video pipeline");
        return false;
    }
    fpsFrames = 0;
    processingNs = 0;
    processingFrames = 0;
    streamSsrc = -1;
    fpsClock.start();
    timer.start();
    return true;
}

void VideoReceiver::stop()
{
    timer.stop();
    // NULL stops streaming threads before releasing probe data and the trace.
    if (pipeline)
        gst_element_set_state(pipeline, GST_STATE_NULL);
    if (bus)
        gst_object_unref(bus);
    if (sink)
        gst_object_unref(sink);
    if (pipeline)
        gst_object_unref(pipeline);
    bus = nullptr;
    sink = nullptr;
    pipeline = nullptr;
    {
        QMutexLocker lock(&timestampMutex);
        timestamps.clear();
    }
    trace.close();
}

GstPadProbeReturn VideoReceiver::inspectRtp(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *self = static_cast<VideoReceiver *>(data);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (buffer && gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
        if (gst_rtp_buffer_get_marker(&rtp)) {
            const quint32 timestamp = gst_rtp_buffer_get_timestamp(&rtp);
            const quint64 pts = GST_BUFFER_PTS(buffer);
            const quint32 ssrc = gst_rtp_buffer_get_ssrc(&rtp);
            self->trace.record("receiver_rtp", 0, pts, timestamp, gst_rtp_buffer_get_seq(&rtp), ssrc);
            if (GST_CLOCK_TIME_IS_VALID(pts)) {
                QMutexLocker lock(&self->timestampMutex);
                self->timestamps[pts] = {ssrc, timestamp, VideoTrace::nowNs()};
                while (self->timestamps.size() > 256)
                    self->timestamps.erase(self->timestamps.begin());
            }
        }
        gst_rtp_buffer_unmap(&rtp);
    }
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn VideoReceiver::inspectIncomingRtp(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *self = static_cast<VideoReceiver *>(data);
    if (!self->trace.active()) return GST_PAD_PROBE_OK;
    const auto observe = [self](GstBuffer *buffer) {
        const qint64 observedNs = VideoTrace::nowNs();
        GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
        if (buffer && gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
            self->trace.record("receiver_packet", 0, GST_BUFFER_PTS(buffer),
                gst_rtp_buffer_get_timestamp(&rtp), gst_rtp_buffer_get_seq(&rtp),
                gst_rtp_buffer_get_ssrc(&rtp), gst_rtp_buffer_get_marker(&rtp), observedNs);
            gst_rtp_buffer_unmap(&rtp);
        }
    };
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
        observe(GST_PAD_PROBE_INFO_BUFFER(info));
    else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList *list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        for (guint i = 0; i < gst_buffer_list_length(list); ++i)
            observe(gst_buffer_list_get(list, i));
    }
    return GST_PAD_PROBE_OK;
}

void VideoReceiver::recordPaint(quint64 frameId, qint64 monotonicNs)
{
    trace.painted(frameId, monotonicNs);
}

void VideoReceiver::poll()
{
    trace.drain();
    while (GstMessage *message = gst_bus_pop_filtered(bus,
            GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        QString text = "Video stream ended";
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            text = QString::fromUtf8(error->message);
            g_clear_error(&error);
            g_free(debug);
        }
        gst_message_unref(message);
        stop();
        emit errorOccurred(text);
        return;
    }
    // Non-blocking pull: the GStreamer streaming thread performs decode.
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
    const qint64 acquiredNs = VideoTrace::nowNs();
    if (sample) {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstVideoInfo info;
        GstVideoFrame frame;
        gst_video_info_init(&info);
        if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
                && gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
            const QImage image(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info),
                GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888);
            const QImage ownedImage = image.copy();
            const quint64 pts = GST_BUFFER_PTS(buffer);
            qint64 rtpTimestamp = -1;
            qint64 ssrc = -1;
            {
                QMutexLocker lock(&timestampMutex);
                const auto found = timestamps.find(pts);
                if (found != timestamps.end()) {
                    ssrc = found->second.ssrc;
                    rtpTimestamp = found->second.rtp;
                    processingNs += VideoTrace::nowNs() - found->second.receivedNs;
                    ++processingFrames;
                    timestamps.erase(timestamps.begin(), std::next(found));
                }
            }
            const quint64 frameId = ++deliveryId;
            if (ssrc >= 0) {
                if (streamSsrc >= 0 && streamSsrc != ssrc) emit streamRestarted();
                streamSsrc = ssrc;
            }
            trace.record("appsink", frameId, pts, rtpTimestamp, -1, ssrc, false, acquiredNs);
            gst_video_frame_unmap(&frame);
            ++fpsFrames;
            emit frameReady(ownedImage, pts, rtpTimestamp, frameId);
        }
        gst_sample_unref(sample);
    }
    if (fpsClock.elapsed() >= 1000) {
        emit fpsChanged(fpsFrames * 1000.0 / fpsClock.elapsed());
        if (processingFrames)
            emit processingTimeChanged(double(processingNs) / processingFrames / 1e6);
        fpsFrames = 0;
        processingNs = 0;
        processingFrames = 0;
        fpsClock.restart();
    }
}
