#include "AppConfig.h"

#include <QFile>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

AppConfig loadAppConfig(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = file.readAll();
    try {
        const auto root = YAML::Load(std::string(bytes.constData(), size_t(bytes.size())));
        if (!root.IsMap() || !root["video"].IsMap() || !root["video"]["codec"].IsScalar())
            throw std::runtime_error("video.codec is required (jpeg or h264)");
        const auto codec = root["video"]["codec"].as<std::string>();
        AppConfig config;
        if (codec == "jpeg") config.videoCodec = VideoCodec::JPEG;
        else if (codec == "h264") config.videoCodec = VideoCodec::H264;
        else throw std::runtime_error("video.codec must be jpeg or h264");
        const auto control = root["control"];
        if (control) {
            if (!control.IsMap()) throw std::runtime_error("control must be a map");
            auto &settings = config.control;
            settings.enabled = control["enabled"].as<bool>(false);
            settings.host = QString::fromStdString(control["host"].as<std::string>("127.0.0.1")).trimmed();
            const int port = control["port"].as<int>(5002);
            if (settings.host.isEmpty() || port < 1 || port > 65535)
                throw std::runtime_error("control.host/port is invalid");
            settings.port = quint16(port);
            const auto interval = [&control](const char *key, int fallback) {
                const int value = control[key].as<int>(fallback);
                if (value < 1 || value > 60000) throw std::runtime_error(std::string("Invalid control timer: ") + key);
                return value;
            };
            settings.pingIntervalMs = interval("ping_interval_ms", 500);
            settings.ackTimeoutMs = interval("ack_timeout_ms", 1500);
            settings.connectTimeoutMs = interval("connect_timeout_ms", 2000);
            settings.reconnectIntervalMs = interval("reconnect_interval_ms", 1000);
        }
        return config;
    } catch (const YAML::Exception &error) {
        throw std::runtime_error(std::string("Invalid YAML: ") + error.what());
    }
}
