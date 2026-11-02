#pragma once

#include "VideoCodec.h"
#include <QString>

QString makeReceiverPipeline(VideoCodec codec, quint16 port, uint latencyMs);
