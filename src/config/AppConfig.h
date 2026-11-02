#pragma once

#include <QString>
#include "ReceiverSettings.h"
#include "control/ControlSettings.h"
#include "PerformanceSettings.h"

struct AppConfig
{
    ReceiverSettings receiver;
    ControlSettings control;
    PerformanceSettings performance;
};

AppConfig loadAppConfig(const QString &path);
