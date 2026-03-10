#include "output/ws_server.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace mde::output {

WsServer::WsServer(uint16_t port, SnapshotProvider provider,
                   ReplayController controller, uint32_t push_interval_ms)
    : port_(port)
    , provider_(std::move(provider))
    , controller_(std::move(controller))
    , push_interval_ms_(push_interval_ms) {}

WsServer::~WsServer() {
    stop();
}

void WsServer::start() {
    running_.store(true);

    accept_thread_ = std::thread([this]() { run_acceptor(); });
    broadcast_thread_ = std::thread([this]() { run_broadcaster(); });
}

void WsServer::stop() {
    if (!running_.exchange(false)) return;

    accept_ioc_.stop();

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            try {
                beast::error_code ec;
                client->ws.close(websocket::close_code::going_away, ec);
            } catch (...) {}
            client->alive = false;
        }
        clients_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
    if (broadcast_thread_.joinable()) broadcast_thread_.join();
}

size_t WsServer::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

void WsServer::run_acceptor() {
    try {
        auto addr = net::ip::make_address("0.0.0.0");
        acceptor_ = std::make_unique<tcp::acceptor>(accept_ioc_, tcp::endpoint{addr, port_});
        acceptor_->set_option(net::socket_base::reuse_address(true));

        spdlog::info("WsServer: listening on port {}", port_);

        // Async accept loop
        std::function<void()> do_accept = [this, &do_accept]() {
            if (!running_.load()) return;
            acceptor_->async_accept([this, &do_accept](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    accept_client(std::move(socket));
                } else if (running_.load()) {
                    spdlog::warn("WsServer: accept error: {}", ec.message());
                }
                do_accept();
            });
        };

        do_accept();
        accept_ioc_.run();
    } catch (const std::exception& e) {
        spdlog::error("WsServer acceptor: {}", e.what());
    }
    spdlog::info("WsServer: acceptor stopped");
}

void WsServer::accept_client(tcp::socket socket) {
    try {
        auto client = std::make_shared<Client>(std::move(socket));
        client->ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        client->ws.accept();

        spdlog::info("WsServer: client connected (total={})", client_count() + 1);

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(client);
        }

        // Spawn a detached thread for reading commands from this client
        if (controller_) {
            std::thread([this, client]() {
                read_commands(client);
            }).detach();
        }
    } catch (const std::exception& e) {
        spdlog::debug("WsServer: client accept failed: {}", e.what());
    }
}

void WsServer::read_commands(std::shared_ptr<Client> client) {
    try {
        beast::flat_buffer buffer;
        while (client->alive && running_.load()) {
            client->ws.read(buffer);
            std::string msg = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            if (controller_) {
                controller_(msg);
            }
        }
    } catch (const beast::system_error& e) {
        if (e.code() != websocket::error::closed) {
            spdlog::debug("WsServer: client read error: {}", e.what());
        }
    }
    client->alive = false;
}

void WsServer::run_broadcaster() {
    spdlog::info("WsServer: broadcaster started (interval={}ms)", push_interval_ms_);

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(push_interval_ms_));
        if (!running_.load()) break;

        std::string snapshot = provider_();
        if (!snapshot.empty()) {
            broadcast(snapshot);
        }

        remove_dead_clients();
    }

    spdlog::info("WsServer: broadcaster stopped");
}

void WsServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (!client->alive) continue;
        try {
            client->ws.text(true);
            client->ws.write(net::buffer(message));
        } catch (const std::exception&) {
            client->alive = false;
        }
    }
}

void WsServer::remove_dead_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto before = clients_.size();
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
            [](const auto& c) { return !c->alive; }),
        clients_.end());
    auto removed = before - clients_.size();
    if (removed > 0) {
        spdlog::info("WsServer: removed {} dead client(s), {} remaining", removed, clients_.size());
    }
}

} // namespace mde::output
