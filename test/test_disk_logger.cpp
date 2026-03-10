#include "output/disk_logger.h"
#include "core/types.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

const std::string TEST_DIR = "test_recordings";
const std::string TEST_FILE = TEST_DIR + "/test_recording.bin";

mde::core::DepthUpdate make_sample_update(const std::string& symbol,
                                           uint64_t event_time,
                                           double bid_price, double bid_qty,
                                           double ask_price, double ask_qty) {
    mde::core::DepthUpdate update;
    update.symbol = symbol;
    update.event_time_ms = event_time;
    update.first_update_id = event_time * 10;
    update.last_update_id = event_time * 10 + 5;
    update.bid_count = 1;
    update.ask_count = 1;
    update.bids[0] = {bid_price, bid_qty};
    update.asks[0] = {ask_price, ask_qty};
    return update;
}

class DiskLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::create_directories(TEST_DIR);
    }
    void TearDown() override {
        fs::remove_all(TEST_DIR);
    }
};

// --- DiskLogger tests ---

TEST_F(DiskLoggerTest, OpenAndClose) {
    mde::output::DiskLogger logger;
    EXPECT_FALSE(logger.is_open());

    ASSERT_TRUE(logger.open(TEST_FILE));
    EXPECT_TRUE(logger.is_open());
    EXPECT_EQ(logger.record_count(), 0u);

    logger.close();
    EXPECT_FALSE(logger.is_open());
}

TEST_F(DiskLoggerTest, WriteSingleRecord) {
    mde::output::DiskLogger logger;
    ASSERT_TRUE(logger.open(TEST_FILE));

    auto update = make_sample_update("BTCUSDT", 1000, 50000.0, 1.5, 50010.0, 2.0);
    ASSERT_TRUE(logger.write(update));
    EXPECT_EQ(logger.record_count(), 1u);

    logger.close();
}

TEST_F(DiskLoggerTest, WriteMultipleRecords) {
    mde::output::DiskLogger logger;
    ASSERT_TRUE(logger.open(TEST_FILE));

    for (int i = 0; i < 100; ++i) {
        auto update = make_sample_update("ETHUSDT", 1000 + i,
            3000.0 + i, 10.0, 3001.0 + i, 10.0);
        ASSERT_TRUE(logger.write(update));
    }
    EXPECT_EQ(logger.record_count(), 100u);

    logger.close();
}

TEST_F(DiskLoggerTest, OpenWithTimestamp) {
    mde::output::DiskLogger logger;
    ASSERT_TRUE(logger.open_with_timestamp(TEST_DIR));
    EXPECT_TRUE(logger.is_open());
    EXPECT_TRUE(logger.path().find("mde_") != std::string::npos);
    EXPECT_TRUE(logger.path().find(".bin") != std::string::npos);
    logger.close();
}

// --- DiskReader tests ---

TEST_F(DiskLoggerTest, ReadEmptyFile) {
    {
        mde::output::DiskLogger logger;
        ASSERT_TRUE(logger.open(TEST_FILE));
        logger.close();
    }

    mde::output::DiskReader reader;
    ASSERT_TRUE(reader.open(TEST_FILE));
    EXPECT_EQ(reader.file_header().record_count, 0u);

    mde::output::DepthRecord rec;
    mde::output::RecordHeader rhdr;
    EXPECT_FALSE(reader.read_next(rec, rhdr));

    reader.close();
}

TEST_F(DiskLoggerTest, WriteAndReadRoundTrip) {
    auto original = make_sample_update("BTCUSDT", 1710000000,
        50000.50, 1.234, 50001.75, 5.678);
    original.bid_count = 2;
    original.bids[1] = {49999.0, 3.0};
    original.ask_count = 2;
    original.asks[1] = {50002.0, 4.0};

    // Write
    {
        mde::output::DiskLogger logger;
        ASSERT_TRUE(logger.open(TEST_FILE));
        ASSERT_TRUE(logger.write(original));
        logger.close();
    }

    // Read
    mde::output::DiskReader reader;
    ASSERT_TRUE(reader.open(TEST_FILE));
    EXPECT_EQ(reader.file_header().magic, mde::output::FILE_MAGIC);
    EXPECT_EQ(reader.file_header().version, mde::output::FILE_VERSION);
    EXPECT_EQ(reader.file_header().record_count, 1u);

    mde::output::DepthRecord rec;
    mde::output::RecordHeader rhdr;
    ASSERT_TRUE(reader.read_next(rec, rhdr));

    // Verify fields
    EXPECT_STREQ(rec.symbol, "BTCUSDT");
    EXPECT_EQ(rec.event_time_ms, 1710000000u);
    EXPECT_EQ(rec.bid_count, 2);
    EXPECT_EQ(rec.ask_count, 2);
    EXPECT_DOUBLE_EQ(rec.bids[0].price, 50000.50);
    EXPECT_DOUBLE_EQ(rec.bids[0].quantity, 1.234);
    EXPECT_DOUBLE_EQ(rec.bids[1].price, 49999.0);
    EXPECT_DOUBLE_EQ(rec.asks[0].price, 50001.75);
    EXPECT_DOUBLE_EQ(rec.asks[1].quantity, 4.0);

    // No more records
    EXPECT_FALSE(reader.read_next(rec, rhdr));
    reader.close();
}

TEST_F(DiskLoggerTest, WriteAndReadMultipleRoundTrip) {
    constexpr int N = 50;

    // Write N records
    {
        mde::output::DiskLogger logger;
        ASSERT_TRUE(logger.open(TEST_FILE));
        for (int i = 0; i < N; ++i) {
            auto update = make_sample_update("SOLUSDT", 2000 + i,
                100.0 + i * 0.1, 100.0, 101.0 + i * 0.1, 200.0);
            ASSERT_TRUE(logger.write(update));
        }
        logger.close();
    }

    // Read all N records
    mde::output::DiskReader reader;
    ASSERT_TRUE(reader.open(TEST_FILE));
    EXPECT_EQ(reader.file_header().record_count, static_cast<uint32_t>(N));

    mde::output::DepthRecord rec;
    mde::output::RecordHeader rhdr;
    int count = 0;
    while (reader.read_next(rec, rhdr)) {
        EXPECT_EQ(rec.event_time_ms, static_cast<uint64_t>(2000 + count));
        EXPECT_STREQ(rec.symbol, "SOLUSDT");
        ++count;
    }
    EXPECT_EQ(count, N);

    reader.close();
}

TEST_F(DiskLoggerTest, RewindReadsFromBeginning) {
    // Write 3 records
    {
        mde::output::DiskLogger logger;
        ASSERT_TRUE(logger.open(TEST_FILE));
        for (int i = 0; i < 3; ++i) {
            auto update = make_sample_update("BNBUSDT", 3000 + i,
                400.0, 50.0, 401.0, 50.0);
            ASSERT_TRUE(logger.write(update));
        }
        logger.close();
    }

    mde::output::DiskReader reader;
    ASSERT_TRUE(reader.open(TEST_FILE));

    // Read 2 records
    mde::output::DepthRecord rec;
    mde::output::RecordHeader rhdr;
    ASSERT_TRUE(reader.read_next(rec, rhdr));
    EXPECT_EQ(rec.event_time_ms, 3000u);
    ASSERT_TRUE(reader.read_next(rec, rhdr));
    EXPECT_EQ(rec.event_time_ms, 3001u);

    // Rewind and re-read from start
    ASSERT_TRUE(reader.rewind());
    ASSERT_TRUE(reader.read_next(rec, rhdr));
    EXPECT_EQ(rec.event_time_ms, 3000u);

    reader.close();
}

TEST_F(DiskLoggerTest, InvalidMagicRejected) {
    // Write garbage
    {
        std::FILE* f = std::fopen(TEST_FILE.c_str(), "wb");
        uint32_t bad_magic = 0xDEADBEEF;
        std::fwrite(&bad_magic, sizeof(bad_magic), 1, f);
        std::fclose(f);
    }

    mde::output::DiskReader reader;
    EXPECT_FALSE(reader.open(TEST_FILE));
}

TEST_F(DiskLoggerTest, FromRecordSymbolPreserved) {
    mde::output::DepthRecord rec{};
    std::strncpy(rec.symbol, "BTCUSDT", mde::output::SYMBOL_MAX_LEN);
    rec.event_time_ms = 999;
    rec.bid_count = 1;
    rec.bids[0] = {50000.0, 1.0};

    auto update = mde::output::from_record(rec);
    EXPECT_EQ(update.symbol, "BTCUSDT");
    EXPECT_EQ(update.event_time_ms, 999u);
    EXPECT_EQ(update.bid_count, 1);
    EXPECT_DOUBLE_EQ(update.bids[0].price, 50000.0);
}

} // namespace
