#include "feed/ws_client.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace mde::feed {

WsClient::WsClient(const config::FeedConfig& feed_cfg, MessageCallback on_message)
    : feed_cfg_(feed_cfg)
    , on_message_(std::move(on_message))
    , reconnect_policy_(
          feed_cfg.reconnect.initial_delay_ms,
          feed_cfg.reconnect.max_delay_ms,
          feed_cfg.reconnect.multiplier) {}

WsClient::~WsClient() {
    stop();
}

void WsClient::run(std::atomic<bool>& running) {
    while (running.load(std::memory_order_relaxed) && !stopped_.load(std::memory_order_relaxed)) {
        try {
            connect_and_run(running);
        } catch (const beast::system_error& se) {
            if (se.code() == websocket::error::closed) {
                spdlog::info("WebSocket closed normally");
            } else {
                spdlog::error("WebSocket error: {}", se.code().message());
            }
        } catch (const std::exception& e) {
            spdlog::error("Connection error: {}", e.what());
        }

        if (!running.load(std::memory_order_relaxed) || stopped_.load(std::memory_order_relaxed)) {
            break;
        }

        auto delay = reconnect_policy_.next_delay_ms();
        spdlog::info("Reconnecting in {} ms...", delay);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

void WsClient::stop() {
    stopped_.store(true, std::memory_order_relaxed);
}

void WsClient::connect_and_run(std::atomic<bool>& running) {
    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};

    tcp::resolver resolver{ioc};
    websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

    // Resolve
    auto const results = resolver.resolve(feed_cfg_.url, feed_cfg_.port);
    spdlog::debug("Resolved {} endpoints for {}:{}", results.size(), feed_cfg_.url, feed_cfg_.port);

    // Connect TCP
    auto ep = net::connect(beast::get_lowest_layer(ws), results);
    spdlog::debug("TCP connected to {}:{}", feed_cfg_.url, ep.port());

    // Set SNI hostname for TLS
    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), feed_cfg_.url.c_str())) {
        throw beast::system_error(
            beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
            "Failed to set SNI hostname");
    }

    // TLS handshake
    ws.next_layer().handshake(ssl::stream_base::client);
    spdlog::debug("TLS handshake complete");

    // WebSocket handshake
    std::string host = feed_cfg_.url + ":" + feed_cfg_.port;
    std::string target = build_stream_path();
    ws.handshake(host, target);
    spdlog::info("WebSocket connected: {}{}", host, target);

    // Connection succeeded — reset backoff
    reconnect_policy_.reset();

    // Read loop
    beast::flat_buffer buffer;
    while (running.load(std::memory_order_relaxed) && !stopped_.load(std::memory_order_relaxed)) {
        buffer.clear();
        ws.read(buffer);

        if (on_message_) {
            auto data = beast::buffers_to_string(buffer.data());
            on_message_(data.data(), data.size());
        }
    }

    // Graceful close
    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);
    if (ec && ec != beast::errc::not_connected) {
        spdlog::warn("WebSocket close error: {}", ec.message());
    }
}

std::string WsClient::build_stream_path() const {
    // Binance combined stream format: /stream?streams=btcusdt@depth@100ms/ethusdt@depth@100ms
    // Single stream format: /ws/btcusdt@depth@100ms
    if (feed_cfg_.symbols.size() == 1 && feed_cfg_.streams.size() == 1) {
        return "/ws/" + feed_cfg_.symbols[0] + "@" + feed_cfg_.streams[0];
    }

    // Combined streams
    std::string path = "/stream?streams=";
    bool first = true;
    for (const auto& symbol : feed_cfg_.symbols) {
        for (const auto& stream : feed_cfg_.streams) {
            if (!first) path += "/";
            path += symbol + "@" + stream;
            first = false;
        }
    }
    return path;
}

} // namespace mde::feed
