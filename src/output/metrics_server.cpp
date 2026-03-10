#include "output/metrics_server.h"

#include <spdlog/spdlog.h>
#include <boost/beast/version.hpp>

namespace mde::output {

MetricsServer::MetricsServer(uint16_t port, StatsProvider provider)
    : port_(port)
    , provider_(std::move(provider)) {}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    running_.store(true);
    thread_ = std::thread([this]() {
        run();
    });
}

void MetricsServer::stop() {
    if (!running_.exchange(false)) return;
    ioc_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MetricsServer::run() {
    try {
        auto addr = net::ip::make_address("0.0.0.0");
        acceptor_ = std::make_unique<tcp::acceptor>(ioc_, tcp::endpoint{addr, port_});
        acceptor_->set_option(net::socket_base::reuse_address(true));

        spdlog::info("MetricsServer: listening on port {}", port_);
        do_accept();
        ioc_.run();
    } catch (const std::exception& e) {
        spdlog::error("MetricsServer: {}", e.what());
    }
    spdlog::info("MetricsServer: stopped");
}

void MetricsServer::do_accept() {
    if (!running_.load()) return;

    acceptor_->async_accept([this](beast::error_code ec, tcp::socket socket) {
        if (ec) {
            if (running_.load()) {
                spdlog::warn("MetricsServer: accept error: {}", ec.message());
            }
        } else {
            handle_connection(std::move(socket));
        }
        if (running_.load()) {
            do_accept();
        }
    });
}

void MetricsServer::handle_connection(tcp::socket socket) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);

        http::response<http::string_body> res;
        res.version(req.version());
        res.set(http::field::server, "mde/0.1.0");
        res.set(http::field::access_control_allow_origin, "*");

        if (req.method() == http::verb::get && req.target() == "/stats") {
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = provider_();
        } else if (req.method() == http::verb::get && req.target() == "/health") {
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"ok"})";
        } else {
            res.result(http::status::not_found);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"not found"})";
        }

        res.prepare_payload();
        http::write(socket, res);

        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception& e) {
        spdlog::debug("MetricsServer: connection error: {}", e.what());
    }
}

} // namespace mde::output
