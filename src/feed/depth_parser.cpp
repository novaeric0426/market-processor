#include "feed/depth_parser.h"

#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>

namespace mde::feed {

namespace {

// Parse string_view to double via strtod with a stack-local null-terminated copy.
// Binance price/qty strings are always < 32 chars.
double fast_atof(std::string_view sv) {
    char buf[32];
    auto len = std::min(sv.size(), sizeof(buf) - 1);
    std::memcpy(buf, sv.data(), len);
    buf[len] = '\0';
    return std::strtod(buf, nullptr);
}

} // namespace

bool DepthParser::parse(std::string_view json, core::DepthUpdate& out) {
    // Detect combined stream format by checking for "data" key in raw JSON.
    // This avoids consuming the simdjson ondemand document prematurely.
    bool combined = (json.find("\"data\"") != std::string_view::npos);

    std::string_view inner_json = json;
    simdjson::padded_string padded_outer;
    simdjson::ondemand::document doc_outer;

    if (combined) {
        // Parse outer envelope to extract "data" object as raw JSON
        padded_outer = simdjson::padded_string(json);
        auto err = parser_.iterate(padded_outer).get(doc_outer);
        if (err) {
            spdlog::warn("JSON parse error (outer): {}", simdjson::error_message(err));
            return false;
        }
        std::string_view data_raw;
        if (doc_outer["data"].raw_json().get(data_raw)) {
            return false;
        }
        inner_json = data_raw;
    }

    simdjson::padded_string padded(inner_json);
    simdjson::ondemand::document doc;

    auto err = parser_.iterate(padded).get(doc);
    if (err) {
        spdlog::warn("JSON parse error: {}", simdjson::error_message(err));
        return false;
    }

    // Event type check
    std::string_view event_type;
    if (doc["e"].get_string().get(event_type) || event_type != "depthUpdate") {
        return false;
    }

    // Extract fields
    uint64_t val;
    if (!doc["E"].get_uint64().get(val)) out.event_time_ms = val;
    if (!doc["U"].get_uint64().get(val)) out.first_update_id = val;
    if (!doc["u"].get_uint64().get(val)) out.last_update_id = val;

    std::string_view symbol_sv;
    if (!doc["s"].get_string().get(symbol_sv)) {
        out.symbol.assign(symbol_sv.data(), symbol_sv.size());
    }

    // Parse bid levels: [["price", "qty"], ...]
    out.bid_count = 0;
    simdjson::ondemand::array bids;
    if (!doc["b"].get_array().get(bids)) {
        for (auto level : bids) {
            if (out.bid_count >= core::DepthUpdate::MAX_LEVELS) break;
            simdjson::ondemand::array pair;
            if (level.get_array().get(pair)) continue;

            auto it = pair.begin();
            std::string_view price_sv, qty_sv;

            if ((*it).get_string().get(price_sv)) continue;
            ++it;
            if ((*it).get_string().get(qty_sv)) continue;

            out.bids[out.bid_count].price = fast_atof(price_sv);
            out.bids[out.bid_count].quantity = fast_atof(qty_sv);
            ++out.bid_count;
        }
    }

    // Parse ask levels
    out.ask_count = 0;
    simdjson::ondemand::array asks;
    if (!doc["a"].get_array().get(asks)) {
        for (auto level : asks) {
            if (out.ask_count >= core::DepthUpdate::MAX_LEVELS) break;
            simdjson::ondemand::array pair;
            if (level.get_array().get(pair)) continue;

            auto it = pair.begin();
            std::string_view price_sv, qty_sv;

            if ((*it).get_string().get(price_sv)) continue;
            ++it;
            if ((*it).get_string().get(qty_sv)) continue;

            out.asks[out.ask_count].price = fast_atof(price_sv);
            out.asks[out.ask_count].quantity = fast_atof(qty_sv);
            ++out.ask_count;
        }
    }

    return true;
}

} // namespace mde::feed
