#include "engine/aggregator.h"
#include "engine/order_book.h"

#include <gtest/gtest.h>

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

TEST(Aggregator, BasicComputation) {
    OrderBook book("BTCUSDT");
    Aggregator agg(2, 10); // vwap top 2, sma window 10

    book.apply_update(make_update(
        {{100.0, 2.0}, {99.0, 3.0}},
        {{101.0, 1.0}, {102.0, 4.0}}
    ));
    agg.update(book);

    auto& snap = agg.snapshot();
    EXPECT_DOUBLE_EQ(snap.spread, 1.0);
    EXPECT_DOUBLE_EQ(snap.mid_price, 100.5);
    EXPECT_NEAR(snap.bid_vwap, 99.4, 1e-9);    // (100*2+99*3)/5
    EXPECT_NEAR(snap.ask_vwap, 101.8, 1e-9);    // (101*1+102*4)/5
    EXPECT_EQ(snap.sample_count, 1u);
}

TEST(Aggregator, BidAskImbalance) {
    OrderBook book("BTCUSDT");
    Aggregator agg;

    // Equal quantities → imbalance = 0
    book.apply_update(make_update(
        {{100.0, 5.0}},
        {{101.0, 5.0}}
    ));
    agg.update(book);
    EXPECT_NEAR(agg.snapshot().bid_ask_imbalance, 0.0, 1e-9);

    // Reset and test bid-heavy
    book.clear();
    agg.reset();
    book.apply_update(make_update(
        {{100.0, 8.0}},
        {{101.0, 2.0}}
    ));
    agg.update(book);
    // (8-2)/(8+2) = 0.6
    EXPECT_NEAR(agg.snapshot().bid_ask_imbalance, 0.6, 1e-9);
}

TEST(Aggregator, MovingAverage) {
    OrderBook book("BTCUSDT");
    Aggregator agg(1, 3); // sma window = 3

    // Feed 3 different mid prices: 100.5, 101.5, 102.5
    book.clear();
    book.apply_update(make_update({{100.0, 1.0}}, {{101.0, 1.0}}));
    agg.update(book); // mid=100.5, sma=100.5

    book.clear();
    book.apply_update(make_update({{101.0, 1.0}}, {{102.0, 1.0}}));
    agg.update(book); // mid=101.5, sma=(100.5+101.5)/2=101.0

    book.clear();
    book.apply_update(make_update({{102.0, 1.0}}, {{103.0, 1.0}}));
    agg.update(book); // mid=102.5, sma=(100.5+101.5+102.5)/3=101.5

    EXPECT_NEAR(agg.snapshot().mid_price_sma, 101.5, 1e-9);

    // Feed 4th value — window slides
    book.clear();
    book.apply_update(make_update({{103.0, 1.0}}, {{104.0, 1.0}}));
    agg.update(book); // mid=103.5, sma=(101.5+102.5+103.5)/3=102.5

    EXPECT_NEAR(agg.snapshot().mid_price_sma, 102.5, 1e-9);
    EXPECT_EQ(agg.snapshot().sample_count, 4u);
}

TEST(Aggregator, Reset) {
    OrderBook book("BTCUSDT");
    Aggregator agg;

    book.apply_update(make_update({{100.0, 1.0}}, {{101.0, 1.0}}));
    agg.update(book);
    EXPECT_EQ(agg.snapshot().sample_count, 1u);

    agg.reset();
    EXPECT_EQ(agg.snapshot().sample_count, 0u);
    EXPECT_DOUBLE_EQ(agg.snapshot().mid_price, 0.0);
}

TEST(Aggregator, EmptyBook) {
    OrderBook book("BTCUSDT");
    Aggregator agg;
    agg.update(book);

    EXPECT_DOUBLE_EQ(agg.snapshot().spread, 0.0);
    EXPECT_DOUBLE_EQ(agg.snapshot().mid_price, 0.0);
    EXPECT_DOUBLE_EQ(agg.snapshot().bid_ask_imbalance, 0.0);
}
