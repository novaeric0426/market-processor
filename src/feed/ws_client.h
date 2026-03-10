#pragma once

#include "config/config_loader.h"
#include "feed/reconnect_policy.h"

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <functional>
#include <string>
#include <atomic>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace mde::feed {

// Callback invoked for each received WebSocket text message.
using MessageCallback = std::function<void(const char* data, std::size_t len)>;

// Synchronous SSL WebSocket client for Binance streams.
// Designed to run on a dedicated feed thread.
class WsClient {
public:
    WsClient(const config::FeedConfig& feed_cfg, MessageCallback on_message);
    ~WsClient();

    // Blocking: connects, subscribes, and enters the read loop.
    // Returns when stop() is called or on unrecoverable error.
    // Automatically reconnects using the configured backoff policy.
    void run(std::atomic<bool>& running);

    // Thread-safe: signals the client to disconnect and exit the read loop.
    void stop();

private:
    void connect_and_run(std::atomic<bool>& running);
    std::string build_stream_path() const;

    config::FeedConfig feed_cfg_;
    MessageCallback on_message_;
    ReconnectPolicy reconnect_policy_;
    std::atomic<bool> stopped_{false};
};

} // namespace mde::feed
