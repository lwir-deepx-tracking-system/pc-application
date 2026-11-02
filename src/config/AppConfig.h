#pragma once

#include <QString>
#include "video/VideoCodec.h"
#include "control/ControlSettings.h"

struct AppConfig
{
    VideoCodec videoCodec = VideoCodec::JPEG;
    ControlSettings control;
};

AppConfig loadAppConfig(const QString &path);
