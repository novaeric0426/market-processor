#include "config/config_loader.h"

#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace mde::config {

AppConfig load_config(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    AppConfig cfg;

    // engine
    if (auto engine = root["engine"]) {
        cfg.name = engine["name"].as<std::string>(cfg.name);
    }

    // feed
    if (auto feed = root["feed"]) {
        cfg.feed.url = feed["url"].as<std::string>(cfg.feed.url);
        cfg.feed.port = feed["port"].as<std::string>(cfg.feed.port);

        if (feed["symbols"]) {
            cfg.feed.symbols.clear();
            for (const auto& s : feed["symbols"]) {
                cfg.feed.symbols.push_back(s.as<std::string>());
            }
        }
        if (feed["streams"]) {
            cfg.feed.streams.clear();
            for (const auto& s : feed["streams"]) {
                cfg.feed.streams.push_back(s.as<std::string>());
            }
        }

        if (auto rc = feed["reconnect"]) {
            cfg.feed.reconnect.initial_delay_ms =
                rc["initial_delay_ms"].as<uint32_t>(cfg.feed.reconnect.initial_delay_ms);
            cfg.feed.reconnect.max_delay_ms =
                rc["max_delay_ms"].as<uint32_t>(cfg.feed.reconnect.max_delay_ms);
            cfg.feed.reconnect.multiplier =
                rc["multiplier"].as<double>(cfg.feed.reconnect.multiplier);
        }
    }

    // logging
    if (auto log = root["logging"]) {
        cfg.logging.level = log["level"].as<std::string>(cfg.logging.level);
        cfg.logging.console = log["console"].as<bool>(cfg.logging.console);

        if (auto file = log["file"]) {
            cfg.logging.file.enabled = file["enabled"].as<bool>(cfg.logging.file.enabled);
            cfg.logging.file.path = file["path"].as<std::string>(cfg.logging.file.path);
            cfg.logging.file.max_size_mb =
                file["max_size_mb"].as<uint32_t>(cfg.logging.file.max_size_mb);
            cfg.logging.file.max_files =
                file["max_files"].as<uint32_t>(cfg.logging.file.max_files);
        }
    }

    // server
    if (auto srv = root["server"]) {
        cfg.server.metrics_port = srv["metrics_port"].as<uint16_t>(cfg.server.metrics_port);
        cfg.server.ws_port = srv["ws_port"].as<uint16_t>(cfg.server.ws_port);
    }

    // recording
    if (auto rec = root["recording"]) {
        cfg.recording.enabled = rec["enabled"].as<bool>(cfg.recording.enabled);
        cfg.recording.output_dir = rec["output_dir"].as<std::string>(cfg.recording.output_dir);
    }

    return cfg;
}

} // namespace mde::config
