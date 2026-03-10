#include "engine/signal_detector.h"
#include "engine/order_book.h"
#include "engine/aggregator.h"

#include <gtest/gtest.h>
#include <vector>

using namespace mde::engine;
using namespace mde::core;

namespace {

DepthUpdate make_update(std::vector<PriceLevel> bids, std::vector<PriceLevel> asks) {
    DepthUpdate u;
    u.symbol = "BTCUSDT";
    u.first_update_id = 1;
    u.last_update_id = 1;
    u.bid_count = static_cast<uint16_t>(std::min(bids.size(), static_cast<size_t>(DepthUpdate::MAX_LEVELS)));
    u.ask_count = static_cast<uint16_t>(std::min(asks.size(), static_cast<size_t>(DepthUpdate::MAX_LEVELS)));
    for (uint16_t i = 0; i < u.bid_count; ++i) u.bids[i] = bids[i];
    for (uint16_t i = 0; i < u.ask_count; ++i) u.asks[i] = asks[i];
    return u;
}

} // namespace

TEST(SignalDetector, SpreadWide) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::SPREAD_WIDE, 5.0});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    // Spread = 10, should fire
    book.apply_update(make_update({{100.0, 1.0}}, {{110.0, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].type, SignalType::SPREAD_WIDE);
    EXPECT_DOUBLE_EQ(fired[0].value, 10.0);
    EXPECT_EQ(fired[0].symbol, "BTCUSDT");
}

TEST(SignalDetector, SpreadNarrow) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::SPREAD_NARROW, 1.0});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    // Spread = 0.5, should fire (< threshold)
    book.apply_update(make_update({{100.0, 1.0}}, {{100.5, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].type, SignalType::SPREAD_NARROW);
}

TEST(SignalDetector, NoFire) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::SPREAD_WIDE, 100.0});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    book.apply_update(make_update({{100.0, 1.0}}, {{101.0, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    EXPECT_TRUE(fired.empty());
}

TEST(SignalDetector, ImbalanceBid) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::IMBALANCE_BID, 0.5});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    // bid=9, ask=1 → imbalance = (9-1)/(9+1) = 0.8
    book.apply_update(make_update({{100.0, 9.0}}, {{101.0, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].type, SignalType::IMBALANCE_BID);
    EXPECT_NEAR(fired[0].value, 0.8, 1e-9);
}

TEST(SignalDetector, ImbalanceAsk) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::IMBALANCE_ASK, 0.5});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    // bid=1, ask=9 → imbalance = (1-9)/(1+9) = -0.8
    book.apply_update(make_update({{100.0, 1.0}}, {{101.0, 9.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].type, SignalType::IMBALANCE_ASK);
}

TEST(SignalDetector, PriceDeviation) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::PRICE_DEVIATION, 0.01}); // 1% threshold

    OrderBook book("BTCUSDT");
    Aggregator agg(1, 3);

    // Build SMA around 100.5
    for (int i = 0; i < 3; ++i) {
        book.clear();
        book.apply_update(make_update({{100.0, 1.0}}, {{101.0, 1.0}}));
        agg.update(book);
    }
    fired.clear(); // Clear any previous signals

    // Now jump to mid=150.5 — deviation = |150.5-100.5|/100.5 ≈ 49.75%
    book.clear();
    book.apply_update(make_update({{150.0, 1.0}}, {{151.0, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    ASSERT_GE(fired.size(), 1u);
    bool found_deviation = false;
    for (const auto& s : fired) {
        if (s.type == SignalType::PRICE_DEVIATION) found_deviation = true;
    }
    EXPECT_TRUE(found_deviation);
}

TEST(SignalDetector, MultipleConditions) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::SPREAD_WIDE, 5.0});
    detector.add_condition({SignalType::IMBALANCE_BID, 0.5});

    OrderBook book("BTCUSDT");
    Aggregator agg;

    // Spread=10 (fires), imbalance=(9-1)/10=0.8 (fires)
    book.apply_update(make_update({{100.0, 9.0}}, {{110.0, 1.0}}));
    agg.update(book);
    detector.evaluate("BTCUSDT", book, agg.snapshot());

    EXPECT_EQ(fired.size(), 2u);
    EXPECT_EQ(detector.signals_fired(), 2u);
}

TEST(SignalDetector, SkipsEmptyAggregator) {
    std::vector<Signal> fired;
    SignalDetector detector([&](const Signal& s) { fired.push_back(s); });
    detector.add_condition({SignalType::SPREAD_WIDE, 0.01});

    OrderBook book("BTCUSDT");
    AggregatorSnapshot empty_snap; // sample_count = 0

    detector.evaluate("BTCUSDT", book, empty_snap);
    EXPECT_TRUE(fired.empty());
}

TEST(SignalDetector, SignalTypeName) {
    EXPECT_STREQ(signal_type_name(SignalType::SPREAD_WIDE), "SPREAD_WIDE");
    EXPECT_STREQ(signal_type_name(SignalType::IMBALANCE_ASK), "IMBALANCE_ASK");
    EXPECT_STREQ(signal_type_name(SignalType::PRICE_DEVIATION), "PRICE_DEVIATION");
}
