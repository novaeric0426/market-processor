#include "replay/replay_engine.h"
#include "output/disk_logger.h"
#include "core/types.h"
#include "core/spsc_queue.h"
#include "feed/feed_handler.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

const std::string TEST_DIR = "test_replay_recordings";
const std::string TEST_FILE = TEST_DIR + "/replay_test.bin";

mde::core::DepthUpdate make_update(const std::string& symbol, uint64_t event_time,
                                    double bid, double ask) {
    mde::core::DepthUpdate update;
    update.symbol = symbol;
    update.event_time_ms = event_time;
    update.first_update_id = event_time;
    update.last_update_id = event_time;
    update.bid_count = 1;
    update.ask_count = 1;
    update.bids[0] = {bid, 1.0};
    update.asks[0] = {ask, 1.0};
    return update;
}

// Helper: write N records to a test file with controlled wall timestamps.
void write_test_file(int n, uint64_t interval_us = 100000) {
    fs::create_directories(TEST_DIR);
    mde::output::DiskLogger logger;
    ASSERT_TRUE(logger.open(TEST_FILE));

    for (int i = 0; i < n; ++i) {
        auto update = make_update("BTCUSDT", 1000 + i,
            50000.0 + i, 50001.0 + i);
        ASSERT_TRUE(logger.write(update));
    }
    logger.close();

    // Patch wall timestamps to have controlled intervals.
    // FileHeader + (RecordHeader + DepthRecord) per record.
    std::FILE* f = std::fopen(TEST_FILE.c_str(), "r+b");
    ASSERT_NE(f, nullptr);

    // Skip file header
    std::fseek(f, sizeof(mde::output::FileHeader), SEEK_SET);

    for (int i = 0; i < n; ++i) {
        mde::output::RecordHeader rhdr;
        std::fread(&rhdr, sizeof(rhdr), 1, f);

        // Rewind and overwrite wall_timestamp_us
        std::fseek(f, -(long)sizeof(rhdr), SEEK_CUR);
        rhdr.wall_timestamp_us = 1000000 + static_cast<uint64_t>(i) * interval_us;
        std::fwrite(&rhdr, sizeof(rhdr), 1, f);

        // Skip payload
        std::fseek(f, sizeof(mde::output::DepthRecord), SEEK_CUR);
    }
    std::fclose(f);
}

class ReplayEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::create_directories(TEST_DIR);
    }
    void TearDown() override {
        fs::remove_all(TEST_DIR);
    }
};

TEST_F(ReplayEngineTest, LoadNonexistentFile) {
    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    EXPECT_FALSE(engine.load("nonexistent.bin"));
}

TEST_F(ReplayEngineTest, ReplayAllRecords) {
    constexpr int N = 20;
    write_test_file(N);

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));
    EXPECT_EQ(engine.total_records(), static_cast<uint64_t>(N));

    // Replay at max speed
    engine.set_speed(mde::replay::PlaybackSpeed::MAX);
    std::atomic<bool> running{true};
    engine.start(running);

    // Wait for replay to finish
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.is_finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(engine.is_finished());
    EXPECT_EQ(engine.replayed_count(), static_cast<uint64_t>(N));

    // Verify all messages are in the queue
    int consumed = 0;
    mde::core::QueueMessage msg;
    while (queue->try_pop(msg)) {
        EXPECT_EQ(msg.depth.symbol, "BTCUSDT");
        EXPECT_EQ(msg.depth.event_time_ms, static_cast<uint64_t>(1000 + consumed));
        ++consumed;
    }
    EXPECT_EQ(consumed, N);

    running.store(false);
    engine.stop();
}

TEST_F(ReplayEngineTest, ReplayEmptyFile) {
    write_test_file(0);

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));

    engine.set_speed(mde::replay::PlaybackSpeed::MAX);
    std::atomic<bool> running{true};
    engine.start(running);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!engine.is_finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(engine.is_finished());
    EXPECT_EQ(engine.replayed_count(), 0u);

    running.store(false);
    engine.stop();
}

TEST_F(ReplayEngineTest, PauseAndResume) {
    constexpr int N = 50;
    write_test_file(N, 1000);  // 1ms intervals

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));

    engine.set_speed(mde::replay::PlaybackSpeed::REALTIME);
    std::atomic<bool> running{true};
    engine.start(running);

    // Let some messages replay
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Pause
    engine.pause();
    EXPECT_TRUE(engine.is_paused());
    auto count_at_pause = engine.replayed_count();

    // Wait and check count didn't change significantly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_LE(engine.replayed_count(), count_at_pause + 1);  // Allow off-by-one

    // Resume
    engine.resume();
    EXPECT_FALSE(engine.is_paused());

    // Wait for completion
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.is_finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(engine.is_finished());
    EXPECT_EQ(engine.replayed_count(), static_cast<uint64_t>(N));

    running.store(false);
    engine.stop();
}

TEST_F(ReplayEngineTest, StopDuringReplay) {
    constexpr int N = 100;
    write_test_file(N, 50000);  // 50ms intervals = 5 seconds total at 1x

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));

    engine.set_speed(mde::replay::PlaybackSpeed::REALTIME);
    std::atomic<bool> running{true};
    engine.start(running);

    // Let a few messages replay
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop early
    running.store(false);
    engine.stop();

    // Should have replayed some but not all
    EXPECT_GT(engine.replayed_count(), 0u);
    EXPECT_LT(engine.replayed_count(), static_cast<uint64_t>(N));
}

TEST_F(ReplayEngineTest, SpeedMaxNoDelay) {
    constexpr int N = 500;
    write_test_file(N, 1000000);  // 1s intervals — would take 500s at 1x

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));

    engine.set_speed(mde::replay::PlaybackSpeed::MAX);
    std::atomic<bool> running{true};

    auto start = std::chrono::steady_clock::now();
    engine.start(running);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine.is_finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(engine.is_finished());
    EXPECT_EQ(engine.replayed_count(), static_cast<uint64_t>(N));

    // Should complete in well under 1 second (max speed = no delays)
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000);

    running.store(false);
    engine.stop();
}

TEST_F(ReplayEngineTest, TimestampsSetForLatencyMeasurement) {
    write_test_file(1);

    auto queue = std::make_unique<mde::feed::FeedQueue>();
    mde::replay::ReplayEngine engine(*queue);
    ASSERT_TRUE(engine.load(TEST_FILE));

    engine.set_speed(mde::replay::PlaybackSpeed::MAX);
    std::atomic<bool> running{true};
    engine.start(running);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!engine.is_finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    mde::core::QueueMessage msg;
    ASSERT_TRUE(queue->try_pop(msg));

    // Verify timestamps were set (non-zero time_since_epoch)
    EXPECT_NE(msg.depth.ts_received.time_since_epoch().count(), 0);
    EXPECT_NE(msg.depth.ts_parsed.time_since_epoch().count(), 0);
    EXPECT_NE(msg.depth.ts_enqueued.time_since_epoch().count(), 0);

    running.store(false);
    engine.stop();
}

} // namespace
