#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

inline void configureGStreamerRuntime()
{
    const QString directory = QCoreApplication::applicationDirPath();
    const QString plugins = directory + "/gstreamer-1.0";
    if (QDir(plugins).exists())
        qputenv("GST_PLUGIN_PATH_1_0", plugins.toUtf8());
#ifdef Q_OS_WIN
    const QString scanner = directory + "/gst-plugin-scanner.exe";
    if (QFileInfo::exists(scanner))
        qputenv("GST_PLUGIN_SCANNER_1_0", scanner.toUtf8());
#endif
}
