#include "feed/depth_parser.h"

#include <gtest/gtest.h>
#include <string>

using namespace mde::feed;
using namespace mde::core;

// Sample Binance depth update JSON
static const std::string SAMPLE_DEPTH = R"({
    "e": "depthUpdate",
    "E": 1672531200000,
    "s": "BTCUSDT",
    "U": 100001,
    "u": 100005,
    "b": [
        ["16500.00000000", "1.50000000"],
        ["16499.50000000", "2.30000000"],
        ["16498.00000000", "0.75000000"]
    ],
    "a": [
        ["16501.00000000", "0.80000000"],
        ["16502.50000000", "1.20000000"]
    ]
})";

TEST(DepthParser, ParseValidMessage) {
    DepthParser parser;
    DepthUpdate update;

    ASSERT_TRUE(parser.parse(SAMPLE_DEPTH, update));

    EXPECT_EQ(update.symbol, "BTCUSDT");
    EXPECT_EQ(update.event_time_ms, 1672531200000ULL);
    EXPECT_EQ(update.first_update_id, 100001ULL);
    EXPECT_EQ(update.last_update_id, 100005ULL);

    EXPECT_EQ(update.bid_count, 3);
    EXPECT_DOUBLE_EQ(update.bids[0].price, 16500.0);
    EXPECT_DOUBLE_EQ(update.bids[0].quantity, 1.5);
    EXPECT_DOUBLE_EQ(update.bids[1].price, 16499.5);
    EXPECT_DOUBLE_EQ(update.bids[1].quantity, 2.3);
    EXPECT_DOUBLE_EQ(update.bids[2].price, 16498.0);
    EXPECT_DOUBLE_EQ(update.bids[2].quantity, 0.75);

    EXPECT_EQ(update.ask_count, 2);
    EXPECT_DOUBLE_EQ(update.asks[0].price, 16501.0);
    EXPECT_DOUBLE_EQ(update.asks[0].quantity, 0.8);
    EXPECT_DOUBLE_EQ(update.asks[1].price, 16502.5);
    EXPECT_DOUBLE_EQ(update.asks[1].quantity, 1.2);
}

TEST(DepthParser, RejectInvalidEvent) {
    DepthParser parser;
    DepthUpdate update;

    std::string trade_msg = R"({"e": "trade", "E": 123, "s": "BTCUSDT"})";
    EXPECT_FALSE(parser.parse(trade_msg, update));
}

TEST(DepthParser, RejectMalformedJSON) {
    DepthParser parser;
    DepthUpdate update;

    EXPECT_FALSE(parser.parse("{invalid json}", update));
    EXPECT_FALSE(parser.parse("", update));
}

TEST(DepthParser, EmptyBidsAsks) {
    DepthParser parser;
    DepthUpdate update;

    std::string msg = R"({
        "e": "depthUpdate",
        "E": 1672531200000,
        "s": "BTCUSDT",
        "U": 100001,
        "u": 100005,
        "b": [],
        "a": []
    })";

    ASSERT_TRUE(parser.parse(msg, update));
    EXPECT_EQ(update.bid_count, 0);
    EXPECT_EQ(update.ask_count, 0);
}

TEST(DepthParser, ZeroQuantity) {
    DepthParser parser;
    DepthUpdate update;

    std::string msg = R"({
        "e": "depthUpdate",
        "E": 1672531200000,
        "s": "BTCUSDT",
        "U": 100001,
        "u": 100005,
        "b": [["16500.00000000", "0.00000000"]],
        "a": [["16501.00000000", "0.00000000"]]
    })";

    ASSERT_TRUE(parser.parse(msg, update));
    EXPECT_EQ(update.bid_count, 1);
    EXPECT_DOUBLE_EQ(update.bids[0].quantity, 0.0);
    EXPECT_EQ(update.ask_count, 1);
    EXPECT_DOUBLE_EQ(update.asks[0].quantity, 0.0);
}

TEST(DepthParser, ManyLevelsCapped) {
    DepthParser parser;
    DepthUpdate update;

    // Build a message with 100 bid levels (should be capped at MAX_LEVELS=64)
    std::string msg = R"({"e":"depthUpdate","E":1,"s":"TEST","U":1,"u":1,"b":[)";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) msg += ",";
        msg += "[\"" + std::to_string(1000 + i) + ".00\",\"1.00\"]";
    }
    msg += R"(],"a":[]})";

    ASSERT_TRUE(parser.parse(msg, update));
    EXPECT_EQ(update.bid_count, DepthUpdate::MAX_LEVELS);
}

TEST(DepthParser, ReuseParser) {
    DepthParser parser;
    DepthUpdate update;

    // Parse multiple messages with the same parser instance
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(parser.parse(SAMPLE_DEPTH, update));
        EXPECT_EQ(update.symbol, "BTCUSDT");
        EXPECT_EQ(update.bid_count, 3);
    }
}
