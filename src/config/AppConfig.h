#pragma once

#include <QString>
#include "ReceiverSettings.h"
#include "control/ControlSettings.h"

struct AppConfig
{
    ReceiverSettings receiver;
    ControlSettings control;
};

AppConfig loadAppConfig(const QString &path);
