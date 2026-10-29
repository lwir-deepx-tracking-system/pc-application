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
        if (codec == "jpeg") return {VideoCodec::JPEG};
        if (codec == "h264") return {VideoCodec::H264};
        throw std::runtime_error("video.codec must be jpeg or h264");
    } catch (const YAML::Exception &error) {
        throw std::runtime_error(std::string("Invalid YAML: ") + error.what());
    }
}
