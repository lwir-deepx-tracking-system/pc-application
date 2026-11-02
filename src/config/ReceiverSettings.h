#pragma once
#include "video/VideoCodec.h"
#include <QString>

struct ReceiverSettings
{
    QString videoBindAddress = "0.0.0.0";
    quint16 videoPort = 5000;
    QString metadataBindAddress = "0.0.0.0";
    quint16 metadataPort = 5001;
    VideoCodec codec = VideoCodec::JPEG;
    uint latencyMs = 50;
    QString tracePath;
    QString packetTracePath;
};
