#include "feed/trade_parser.h"

#include <gtest/gtest.h>
#include <string>

using namespace mde::feed;
using namespace mde::core;

static const std::string SAMPLE_TRADE = R"({
    "e": "trade",
    "E": 1672531200000,
    "s": "BTCUSDT",
    "t": 28457,
    "p": "50000.50000000",
    "q": "0.12340000",
    "b": 88,
    "a": 50,
    "T": 1672531200000,
    "m": true,
    "M": true
})";

TEST(TradeParser, ParseValidMessage) {
    TradeParser parser;
    Trade trade;

    ASSERT_TRUE(parser.parse(SAMPLE_TRADE, trade));

    EXPECT_EQ(trade.symbol, "BTCUSDT");
    EXPECT_EQ(trade.event_time_ms, 1672531200000ULL);
    EXPECT_EQ(trade.trade_id, 28457ULL);
    EXPECT_DOUBLE_EQ(trade.price, 50000.5);
    EXPECT_DOUBLE_EQ(trade.quantity, 0.1234);
    EXPECT_TRUE(trade.is_buyer_maker);
}

TEST(TradeParser, BuyerAggressor) {
    TradeParser parser;
    Trade trade;

    std::string msg = R"({
        "e": "trade", "E": 1, "s": "ETHUSDT", "t": 100,
        "p": "3000.00", "q": "1.5", "b": 1, "a": 2, "T": 1, "m": false
    })";

    ASSERT_TRUE(parser.parse(msg, trade));
    EXPECT_FALSE(trade.is_buyer_maker);
    EXPECT_EQ(trade.symbol, "ETHUSDT");
    EXPECT_DOUBLE_EQ(trade.price, 3000.0);
    EXPECT_DOUBLE_EQ(trade.quantity, 1.5);
}

TEST(TradeParser, RejectDepthMessage) {
    TradeParser parser;
    Trade trade;

    std::string msg = R"({"e": "depthUpdate", "E": 123, "s": "BTCUSDT", "U": 1, "u": 2, "b": [], "a": []})";
    EXPECT_FALSE(parser.parse(msg, trade));
}

TEST(TradeParser, RejectMalformedJSON) {
    TradeParser parser;
    Trade trade;

    EXPECT_FALSE(parser.parse("{invalid json}", trade));
    EXPECT_FALSE(parser.parse("", trade));
}

TEST(TradeParser, ParseCombinedStreamFormat) {
    TradeParser parser;
    Trade trade;

    std::string msg = R"({
        "stream": "btcusdt@trade",
        "data": {
            "e": "trade", "E": 1672531200000, "s": "BTCUSDT", "t": 999,
            "p": "42000.00", "q": "0.5", "b": 1, "a": 2, "T": 1672531200000, "m": true
        }
    })";

    ASSERT_TRUE(parser.parse(msg, trade));
    EXPECT_EQ(trade.symbol, "BTCUSDT");
    EXPECT_EQ(trade.trade_id, 999ULL);
    EXPECT_DOUBLE_EQ(trade.price, 42000.0);
    EXPECT_DOUBLE_EQ(trade.quantity, 0.5);
    EXPECT_TRUE(trade.is_buyer_maker);
}

TEST(TradeParser, ReuseParser) {
    TradeParser parser;
    Trade trade;

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(parser.parse(SAMPLE_TRADE, trade));
        EXPECT_EQ(trade.symbol, "BTCUSDT");
        EXPECT_EQ(trade.trade_id, 28457ULL);
    }
}
