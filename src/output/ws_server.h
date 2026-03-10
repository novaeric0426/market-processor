#pragma once

#include "engine/processing_thread.h"
#include "replay/replay_engine.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace mde::output {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Callback that returns the current engine snapshot JSON.
using SnapshotProvider = std::function<std::string()>;

// Optional callback for replay control commands from clients.
// Command format: {"cmd":"pause"}, {"cmd":"resume"}, {"cmd":"speed","value":"2x"}
using ReplayController = std::function<void(const std::string&)>;

// WebSocket server that pushes engine state to connected dashboard clients.
// Accepts connections, periodically broadcasts snapshots, and receives
// replay control commands from clients.
class WsServer {
public:
    WsServer(uint16_t port, SnapshotProvider provider,
             ReplayController controller = nullptr,
             uint32_t push_interval_ms = 100);
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    void start();
    void stop();

    size_t client_count() const;

private:
    struct Client {
        websocket::stream<tcp::socket> ws;
        bool alive = true;
        explicit Client(tcp::socket socket)
            : ws(std::move(socket)) {}
    };

    void run_acceptor();
    void run_broadcaster();
    void accept_client(tcp::socket socket);
    void read_commands(std::shared_ptr<Client> client);
    void broadcast(const std::string& message);
    void remove_dead_clients();

    uint16_t port_;
    SnapshotProvider provider_;
    ReplayController controller_;
    uint32_t push_interval_ms_;

    net::io_context accept_ioc_{1};
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::thread accept_thread_;
    std::thread broadcast_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex clients_mutex_;
    std::vector<std::shared_ptr<Client>> clients_;
};

} // namespace mde::output
