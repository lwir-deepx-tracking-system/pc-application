#pragma once

#include "VideoCodec.h"
#include <QString>

QString makeReceiverPipeline(VideoCodec codec, quint16 port, uint latencyMs,
                             const QString &bindAddress = "0.0.0.0");
