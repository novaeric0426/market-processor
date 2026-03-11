#include "engine/trade_aggregator.h"

#include <gtest/gtest.h>

using namespace mde::engine;
using namespace mde::core;

namespace {

Trade make_trade(const std::string& symbol, double price, double qty,
                 bool is_buyer_maker, uint64_t event_time_ms) {
    Trade t;
    t.symbol = symbol;
    t.price = price;
    t.quantity = qty;
    t.is_buyer_maker = is_buyer_maker;
    t.event_time_ms = event_time_ms;
    return t;
}

} // namespace

TEST(TradeAggregator, BasicImbalance) {
    TradeAggregator agg(1000, 20);

    // 3 buy-aggressor trades (is_buyer_maker=false)
    agg.update(make_trade("BTCUSDT", 50000, 1.0, false, 1000));
    agg.update(make_trade("BTCUSDT", 50000, 1.0, false, 1001));
    agg.update(make_trade("BTCUSDT", 50000, 1.0, false, 1002));

    // 1 sell-aggressor trade (is_buyer_maker=true)
    agg.update(make_trade("BTCUSDT", 50000, 1.0, true, 1003));

    const auto& snap = agg.snapshot();
    EXPECT_DOUBLE_EQ(snap.buy_volume, 3.0);
    EXPECT_DOUBLE_EQ(snap.sell_volume, 1.0);
    EXPECT_DOUBLE_EQ(snap.total_volume, 4.0);
    // imbalance = (3 - 1) / (3 + 1) = 0.5
    EXPECT_DOUBLE_EQ(snap.trade_imbalance, 0.5);
    EXPECT_EQ(snap.trade_count, 4u);
}

TEST(TradeAggregator, SellHeavyImbalance) {
    TradeAggregator agg(1000, 20);

    agg.update(make_trade("BTCUSDT", 50000, 1.0, true, 1000));  // sell
    agg.update(make_trade("BTCUSDT", 50000, 4.0, true, 1001));  // sell

    const auto& snap = agg.snapshot();
    EXPECT_DOUBLE_EQ(snap.buy_volume, 0.0);
    EXPECT_DOUBLE_EQ(snap.sell_volume, 5.0);
    // imbalance = (0 - 5) / (0 + 5) = -1.0
    EXPECT_DOUBLE_EQ(snap.trade_imbalance, -1.0);
}

TEST(TradeAggregator, WindowExpiration) {
    // 500ms window
    TradeAggregator agg(500, 20);

    // Trade at t=1000
    agg.update(make_trade("BTCUSDT", 50000, 2.0, false, 1000));

    // Trade at t=1600 — the first trade is outside the 500ms window
    agg.update(make_trade("BTCUSDT", 50000, 1.0, true, 1600));

    const auto& snap = agg.snapshot();
    // Only the second trade should remain
    EXPECT_EQ(snap.trade_count, 1u);
    EXPECT_DOUBLE_EQ(snap.sell_volume, 1.0);
    EXPECT_DOUBLE_EQ(snap.buy_volume, 0.0);
}

TEST(TradeAggregator, VolumeSMA) {
    // 100ms window, 3 window SMA
    TradeAggregator agg(100, 3);

    // Window 1: t=0-99, volume=5.0
    agg.update(make_trade("BTCUSDT", 50000, 5.0, false, 50));

    // Window 2: t=100-199, volume=3.0 (triggers window 1 archive)
    agg.update(make_trade("BTCUSDT", 50000, 3.0, false, 150));

    auto snap = agg.snapshot();
    EXPECT_EQ(snap.window_count, 1u);
    EXPECT_DOUBLE_EQ(snap.volume_sma, 5.0);  // Only one completed window so far

    // Window 3: t=200-299, volume=7.0
    agg.update(make_trade("BTCUSDT", 50000, 7.0, false, 250));

    snap = agg.snapshot();
    EXPECT_EQ(snap.window_count, 2u);
    // SMA of completed windows: (5.0 + 3.0) / 2 = 4.0
    EXPECT_DOUBLE_EQ(snap.volume_sma, 4.0);
}

TEST(TradeAggregator, Reset) {
    TradeAggregator agg(1000, 20);
    agg.update(make_trade("BTCUSDT", 50000, 1.0, false, 1000));

    agg.reset();
    const auto& snap = agg.snapshot();
    EXPECT_EQ(snap.trade_count, 0u);
    EXPECT_DOUBLE_EQ(snap.total_volume, 0.0);
    EXPECT_DOUBLE_EQ(snap.trade_imbalance, 0.0);
}
