#pragma once

#include <QMetaType>
#include <array>

// Fixed LWTM contract. No version, flags, RTP timestamp, or variable-length fields.
namespace RuntimeWire {
inline constexpr quint32 Magic = 0x4c57544d;
inline constexpr int Size = 124;
inline constexpr int MetricCount = 18;
inline constexpr quint32 ValidBits = (1u << MetricCount) - 1;
inline constexpr std::array<const char *, MetricCount> MetricNames = {
    "camera_capture_ms", "detection_queue_ms", "detection_processing_ms", "preprocess_ms",
    "inference_latency_ms", "npu_core_ms", "postprocess_ms", "tracking_queue_ms",
    "tracking_processing_ms", "capture_to_tracking_ms", "control_queue_ms", "control_submit_ms",
    "capture_to_control_submit_ms", "gui_queue_ms", "gui_convert_ms", "gui_submit_ms",
    "encode_rtp_ms", "capture_to_rtp_ready_ms"
};
inline constexpr std::array<int, 4> DisplayMetrics = {4, 8, 16, 17};
}

struct RuntimeTelemetry
{
    quint64 sessionId = 0, modelGeneration = 0, trackerEpoch = 0, frameId = 0;
    quint32 validMask = 0, modelId = 0, trackerId = 0;
    float sampleAgeMs = 0;
    std::array<float, RuntimeWire::MetricCount> values{};
};
Q_DECLARE_METATYPE(RuntimeTelemetry)
