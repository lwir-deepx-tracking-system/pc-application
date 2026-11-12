#pragma once
#include <QString>

struct ControlSettings {
    bool enabled = false;
    QString host = "127.0.0.1";
    quint16 port = 5002;
    // Initial test values; hardware integration may require adjustment.
    int pingIntervalMs = 500;
    int ackTimeoutMs = 1500;
    int connectTimeoutMs = 2000;
    int reconnectIntervalMs = 1000;
    bool modelSwitchEnabled = false; // Enabled by a nonempty PC model catalog.
    bool trackerSwitchEnabled = false; // Opt in for boards with Pipeline tracker commands.
    int modelSwitchTimeoutMs = 30000;
    int trackerSwitchTimeoutMs = 30000;
};
