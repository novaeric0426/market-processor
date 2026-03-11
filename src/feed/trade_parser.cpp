#include "feed/trade_parser.h"

#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>

namespace mde::feed {

namespace {

double fast_atof(std::string_view sv) {
    char buf[32];
    auto len = std::min(sv.size(), sizeof(buf) - 1);
    std::memcpy(buf, sv.data(), len);
    buf[len] = '\0';
    return std::strtod(buf, nullptr);
}

} // namespace

bool TradeParser::parse(std::string_view json, core::Trade& out) {
    // Detect combined stream format
    bool combined = (json.find("\"data\"") != std::string_view::npos);

    std::string_view inner_json = json;
    simdjson::padded_string padded_outer;
    simdjson::ondemand::document doc_outer;

    if (combined) {
        padded_outer = simdjson::padded_string(json);
        auto err = parser_.iterate(padded_outer).get(doc_outer);
        if (err) {
            spdlog::warn("TradeParser: JSON parse error (outer): {}", simdjson::error_message(err));
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
        spdlog::warn("TradeParser: JSON parse error: {}", simdjson::error_message(err));
        return false;
    }

    // Event type check
    std::string_view event_type;
    if (doc["e"].get_string().get(event_type) || event_type != "trade") {
        return false;
    }

    // Extract fields
    uint64_t val;
    if (!doc["E"].get_uint64().get(val)) out.event_time_ms = val;
    if (!doc["t"].get_uint64().get(val)) out.trade_id = val;

    std::string_view symbol_sv;
    if (!doc["s"].get_string().get(symbol_sv)) {
        out.symbol.assign(symbol_sv.data(), symbol_sv.size());
    }

    std::string_view price_sv, qty_sv;
    if (!doc["p"].get_string().get(price_sv)) {
        out.price = fast_atof(price_sv);
    }
    if (!doc["q"].get_string().get(qty_sv)) {
        out.quantity = fast_atof(qty_sv);
    }

    bool is_maker;
    if (!doc["m"].get_bool().get(is_maker)) {
        out.is_buyer_maker = is_maker;
    }

    return true;
}

} // namespace mde::feed
