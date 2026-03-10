#pragma once

#include "config/config_loader.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <thread>
#include <string>
#include <functional>
#include <cstdint>

namespace mde::output {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Callback that returns JSON stats string on demand.
using StatsProvider = std::function<std::string()>;

// Minimal HTTP server exposing GET /stats as JSON.
// Runs on its own thread with a single-threaded io_context.
class MetricsServer {
public:
    explicit MetricsServer(uint16_t port, StatsProvider provider);
    ~MetricsServer();

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;

    void start();
    void stop();

private:
    void run();
    void do_accept();
    void handle_connection(tcp::socket socket);

    uint16_t port_;
    StatsProvider provider_;
    net::io_context ioc_{1};
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace mde::output
