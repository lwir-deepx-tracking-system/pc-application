#pragma once

#include <QString>
#include "video/VideoCodec.h"

struct AppConfig
{
    VideoCodec videoCodec;
};

AppConfig loadAppConfig(const QString &path);
