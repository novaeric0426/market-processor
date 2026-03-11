#pragma once

#include "core/types.h"

#include <simdjson.h>
#include <string_view>

namespace mde::feed {

// Parses Binance trade JSON into Trade struct using simdjson.
// Single parser instance should be reused across calls (simdjson requirement).
class TradeParser {
public:
    TradeParser() = default;

    // Parse a raw JSON trade message.
    // Returns true on success, false on malformed input.
    bool parse(std::string_view json, core::Trade& out);

private:
    simdjson::ondemand::parser parser_;
};

} // namespace mde::feed
