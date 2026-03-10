#pragma once

#include "core/types.h"

#include <simdjson.h>
#include <string_view>

namespace mde::feed {

// Parses Binance depth update JSON into DepthUpdate struct using simdjson.
// Single parser instance should be reused across calls (simdjson requirement).
class DepthParser {
public:
    DepthParser() = default;

    // Parse a raw JSON depth update message.
    // Returns true on success, false on malformed input.
    bool parse(std::string_view json, core::DepthUpdate& out);

private:
    simdjson::ondemand::parser parser_;
};

} // namespace mde::feed
