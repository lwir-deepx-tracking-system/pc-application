#include "VideoPipeline.h"

QString makeReceiverPipeline(VideoCodec codec, quint16 port, uint latencyMs)
{
    struct CodecPipeline {
        const char *encoding;
        int payloadType;
        const char *decode;
    };
    const CodecPipeline profile = codec == VideoCodec::JPEG
        ? CodecPipeline{"JPEG", 26, "rtpjpegdepay name=depay ! jpegdec"}
        : CodecPipeline{"H264", 96, "rtph264depay name=depay ! h264parse ! avdec_h264 max-threads=1"};
    // Jitter latency is provisional; queues retain the latest decoded frame.
    return QString(
        "udpsrc name=input port=%1 caps=\"application/x-rtp,media=video,encoding-name=%3,clock-rate=90000,payload=%4\" "
        "! rtpjitterbuffer latency=%2 drop-on-latency=true "
        "! %5 ! videoconvert "
        "! video/x-raw,format=RGB,width=640,height=480 "
        "! appsink name=frames sync=false max-buffers=1 drop=true wait-on-eos=false")
        .arg(port).arg(latencyMs).arg(profile.encoding).arg(profile.payloadType).arg(profile.decode);
}
