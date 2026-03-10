#include "engine/order_book.h"

#include <gtest/gtest.h>

using namespace mde::engine;
using namespace mde::core;

namespace {

DepthUpdate make_update(uint64_t first_id, uint64_t last_id,
                        std::vector<PriceLevel> bids,
                        std::vector<PriceLevel> asks) {
    DepthUpdate u;
    u.symbol = "BTCUSDT";
    u.first_update_id = first_id;
    u.last_update_id = last_id;
    u.bid_count = static_cast<uint16_t>(std::min(bids.size(), static_cast<size_t>(DepthUpdate::MAX_LEVELS)));
    u.ask_count = static_cast<uint16_t>(std::min(asks.size(), static_cast<size_t>(DepthUpdate::MAX_LEVELS)));
    for (uint16_t i = 0; i < u.bid_count; ++i) u.bids[i] = bids[i];
    for (uint16_t i = 0; i < u.ask_count; ++i) u.asks[i] = asks[i];
    return u;
}

} // namespace

TEST(OrderBook, EmptyBook) {
    OrderBook book("BTCUSDT");
    EXPECT_EQ(book.best_bid_price(), 0.0);
    EXPECT_EQ(book.best_ask_price(), 0.0);
    EXPECT_EQ(book.spread(), 0.0);
    EXPECT_EQ(book.mid_price(), 0.0);
    EXPECT_EQ(book.bid_levels(), 0u);
    EXPECT_EQ(book.ask_levels(), 0u);
}

TEST(OrderBook, ApplyUpdate) {
    OrderBook book("BTCUSDT");

    auto update = make_update(1, 5,
        {{100.0, 1.5}, {99.0, 2.0}},
        {{101.0, 0.8}, {102.0, 1.2}}
    );
    book.apply_update(update);

    EXPECT_EQ(book.bid_levels(), 2u);
    EXPECT_EQ(book.ask_levels(), 2u);
    EXPECT_DOUBLE_EQ(book.best_bid_price(), 100.0);
    EXPECT_DOUBLE_EQ(book.best_ask_price(), 101.0);
    EXPECT_DOUBLE_EQ(book.best_bid_qty(), 1.5);
    EXPECT_DOUBLE_EQ(book.best_ask_qty(), 0.8);
    EXPECT_DOUBLE_EQ(book.spread(), 1.0);
    EXPECT_DOUBLE_EQ(book.mid_price(), 100.5);
    EXPECT_EQ(book.last_update_id(), 5u);
    EXPECT_EQ(book.update_count(), 1u);
}

TEST(OrderBook, RemoveLevelWithZeroQty) {
    OrderBook book("BTCUSDT");

    book.apply_update(make_update(1, 1,
        {{100.0, 1.0}, {99.0, 2.0}}, {}));
    EXPECT_EQ(book.bid_levels(), 2u);

    // Remove 100.0 by setting qty=0
    book.apply_update(make_update(2, 2,
        {{100.0, 0.0}}, {}));
    EXPECT_EQ(book.bid_levels(), 1u);
    EXPECT_DOUBLE_EQ(book.best_bid_price(), 99.0);
}

TEST(OrderBook, UpdateExistingLevel) {
    OrderBook book("BTCUSDT");

    book.apply_update(make_update(1, 1, {{100.0, 1.0}}, {}));
    EXPECT_DOUBLE_EQ(book.best_bid_qty(), 1.0);

    // Update quantity at same price
    book.apply_update(make_update(2, 2, {{100.0, 5.0}}, {}));
    EXPECT_DOUBLE_EQ(book.best_bid_qty(), 5.0);
    EXPECT_EQ(book.bid_levels(), 1u);
}

TEST(OrderBook, BidsSortedDescending) {
    OrderBook book("BTCUSDT");
    book.apply_update(make_update(1, 1,
        {{98.0, 1.0}, {100.0, 1.0}, {99.0, 1.0}}, {}));

    auto it = book.bids().begin();
    EXPECT_DOUBLE_EQ(it->first, 100.0); ++it;
    EXPECT_DOUBLE_EQ(it->first, 99.0);  ++it;
    EXPECT_DOUBLE_EQ(it->first, 98.0);
}

TEST(OrderBook, AsksSortedAscending) {
    OrderBook book("BTCUSDT");
    book.apply_update(make_update(1, 1,
        {}, {{103.0, 1.0}, {101.0, 1.0}, {102.0, 1.0}}));

    auto it = book.asks().begin();
    EXPECT_DOUBLE_EQ(it->first, 101.0); ++it;
    EXPECT_DOUBLE_EQ(it->first, 102.0); ++it;
    EXPECT_DOUBLE_EQ(it->first, 103.0);
}

TEST(OrderBook, TotalQuantity) {
    OrderBook book("BTCUSDT");
    book.apply_update(make_update(1, 1,
        {{100.0, 1.0}, {99.0, 2.0}, {98.0, 3.0}},
        {{101.0, 0.5}, {102.0, 1.5}}));

    EXPECT_DOUBLE_EQ(book.total_bid_qty(), 6.0);
    EXPECT_DOUBLE_EQ(book.total_ask_qty(), 2.0);
}

TEST(OrderBook, VWAP) {
    OrderBook book("BTCUSDT");
    book.apply_update(make_update(1, 1,
        {{100.0, 2.0}, {99.0, 3.0}},
        {{101.0, 1.0}, {102.0, 4.0}}));

    // Bid VWAP top 2: (100*2 + 99*3) / (2+3) = 497/5 = 99.4
    EXPECT_NEAR(book.bid_vwap(2), 99.4, 1e-9);

    // Ask VWAP top 1: 101*1/1 = 101
    EXPECT_DOUBLE_EQ(book.ask_vwap(1), 101.0);

    // Ask VWAP top 2: (101*1 + 102*4) / 5 = 509/5 = 101.8
    EXPECT_NEAR(book.ask_vwap(2), 101.8, 1e-9);
}

TEST(OrderBook, Clear) {
    OrderBook book("BTCUSDT");
    book.apply_update(make_update(1, 5,
        {{100.0, 1.0}}, {{101.0, 1.0}}));
    EXPECT_EQ(book.bid_levels(), 1u);

    book.clear();
    EXPECT_EQ(book.bid_levels(), 0u);
    EXPECT_EQ(book.ask_levels(), 0u);
    EXPECT_EQ(book.last_update_id(), 0u);
    EXPECT_EQ(book.update_count(), 0u);
}

TEST(OrderBook, MultipleUpdatesAccumulate) {
    OrderBook book("BTCUSDT");

    book.apply_update(make_update(1, 1, {{100.0, 1.0}}, {{101.0, 1.0}}));
    book.apply_update(make_update(2, 2, {{99.0, 2.0}}, {{102.0, 2.0}}));
    book.apply_update(make_update(3, 3, {{98.0, 3.0}}, {{103.0, 3.0}}));

    EXPECT_EQ(book.bid_levels(), 3u);
    EXPECT_EQ(book.ask_levels(), 3u);
    EXPECT_EQ(book.update_count(), 3u);
    EXPECT_DOUBLE_EQ(book.best_bid_price(), 100.0);
    EXPECT_DOUBLE_EQ(book.best_ask_price(), 101.0);
}
