#pragma once

#include <QString>
#include "ReceiverSettings.h"
#include "control/ControlSettings.h"
#include "PerformanceSettings.h"
#include "ModelSettings.h"

struct AppConfig
{
    ReceiverSettings receiver;
    ControlSettings control;
    PerformanceSettings performance;
    ModelOptions models;
};

AppConfig loadAppConfig(const QString &path);
