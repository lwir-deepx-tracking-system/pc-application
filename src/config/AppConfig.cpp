#include "AppConfig.h"

#include <QFile>
#include <QHostAddress>
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
        if (codec == "jpeg") config.receiver.codec = VideoCodec::JPEG;
        else if (codec == "h264") config.receiver.codec = VideoCodec::H264;
        else throw std::runtime_error("video.codec must be jpeg or h264");
        const auto number = [](const YAML::Node &section, const char *key, int fallback, int min, int max) {
            const int value = section[key].as<int>(fallback);
            if (value < min || value > max) throw std::runtime_error(std::string("Invalid receiver setting: ") + key);
            return value;
        };
        const auto bindAddress = [](const YAML::Node &section) {
            const auto text = QString::fromStdString(section["bind_address"].as<std::string>("0.0.0.0")).trimmed();
            const QHostAddress address(text);
            if (address.protocol() != QAbstractSocket::IPv4Protocol)
                throw std::runtime_error("bind_address must be a local IPv4 address or 0.0.0.0");
            return address.toString();
        };
        auto &receiver = config.receiver;
        const auto video = root["video"];
        receiver.videoBindAddress = bindAddress(video);
        receiver.videoPort = quint16(number(video, "port", 5000, 1, 65535));
        receiver.latencyMs = uint(number(video, "latency_ms", 50, 0, 10000));
        const auto metadata = root["metadata"];
        if (metadata) {
            if (!metadata.IsMap()) throw std::runtime_error("metadata must be a map");
            receiver.metadataBindAddress = bindAddress(metadata);
            receiver.metadataPort = quint16(number(metadata, "port", 5001, 1, 65535));
        }
        if (receiver.videoPort == receiver.metadataPort)
            throw std::runtime_error("video.port and metadata.port must differ");
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
