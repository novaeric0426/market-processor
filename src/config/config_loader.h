#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace mde::config {

struct ReconnectConfig {
    uint32_t initial_delay_ms = 100;
    uint32_t max_delay_ms = 30000;
    double multiplier = 2.0;
};

struct FeedConfig {
    std::string url = "stream.binance.com";
    std::string port = "9443";
    std::vector<std::string> symbols;
    std::vector<std::string> streams;
    ReconnectConfig reconnect;
};

struct FileLogConfig {
    bool enabled = true;
    std::string path = "logs/engine.log";
    uint32_t max_size_mb = 50;
    uint32_t max_files = 3;
};

struct LoggingConfig {
    std::string level = "info";
    bool console = true;
    FileLogConfig file;
};

struct ServerConfig {
    uint16_t metrics_port = 8080;
    uint16_t ws_port = 8081;
};

struct RecordingConfig {
    bool enabled = false;
    std::string output_dir = "recordings/";
};

struct AppConfig {
    std::string name = "market-data-engine";
    FeedConfig feed;
    LoggingConfig logging;
    ServerConfig server;
    RecordingConfig recording;
};

// Load configuration from a YAML file.
// Throws std::runtime_error if the file cannot be read or parsed.
AppConfig load_config(const std::string& path);

} // namespace mde::config
