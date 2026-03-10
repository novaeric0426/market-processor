#pragma once

#include "core/types.h"
#include "core/clock.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace mde::output {

// Binary file format for recording market data.
//
// File layout:
//   [FileHeader]
//   [Record][Record][Record]...
//
// Each Record:
//   [RecordHeader]
//   [DepthRecord]
//
// Symbol is stored as fixed-size char array to avoid heap allocation on read.

static constexpr uint32_t FILE_MAGIC = 0x4D444531;  // "MDE1"
static constexpr uint32_t FILE_VERSION = 1;
static constexpr size_t SYMBOL_MAX_LEN = 32;

#pragma pack(push, 1)

struct FileHeader {
    uint32_t magic = FILE_MAGIC;
    uint32_t version = FILE_VERSION;
    uint64_t created_us = 0;        // Wall-clock timestamp (microseconds since epoch)
    uint32_t record_count = 0;      // Updated on close
    uint32_t reserved = 0;
};

struct RecordHeader {
    uint64_t wall_timestamp_us = 0; // Wall-clock when recorded
    uint32_t payload_size = 0;      // Size of following DepthRecord
};

struct DepthRecord {
    uint64_t event_time_ms = 0;
    uint64_t first_update_id = 0;
    uint64_t last_update_id = 0;
    char symbol[SYMBOL_MAX_LEN] = {};

    uint16_t bid_count = 0;
    uint16_t ask_count = 0;

    // Inline price levels (fixed max)
    core::PriceLevel bids[core::DepthUpdate::MAX_LEVELS];
    core::PriceLevel asks[core::DepthUpdate::MAX_LEVELS];
};

#pragma pack(pop)

// Converts between DepthUpdate (runtime) and DepthRecord (on-disk).
inline DepthRecord to_record(const core::DepthUpdate& update) {
    DepthRecord rec;
    rec.event_time_ms = update.event_time_ms;
    rec.first_update_id = update.first_update_id;
    rec.last_update_id = update.last_update_id;
    rec.bid_count = update.bid_count;
    rec.ask_count = update.ask_count;

    // Copy symbol (truncate if necessary)
    std::size_t len = update.symbol.size();
    if (len >= SYMBOL_MAX_LEN) len = SYMBOL_MAX_LEN - 1;
    std::memcpy(rec.symbol, update.symbol.data(), len);
    rec.symbol[len] = '\0';

    std::memcpy(rec.bids, update.bids.data(), sizeof(rec.bids));
    std::memcpy(rec.asks, update.asks.data(), sizeof(rec.asks));
    return rec;
}

inline core::DepthUpdate from_record(const DepthRecord& rec) {
    core::DepthUpdate update;
    update.event_time_ms = rec.event_time_ms;
    update.first_update_id = rec.first_update_id;
    update.last_update_id = rec.last_update_id;
    update.bid_count = rec.bid_count;
    update.ask_count = rec.ask_count;
    update.symbol = rec.symbol;

    std::memcpy(update.bids.data(), rec.bids, sizeof(rec.bids));
    std::memcpy(update.asks.data(), rec.asks, sizeof(rec.asks));
    return update;
}

// Writes DepthUpdates to a binary file.
// Not thread-safe — call from a single thread (processing thread).
class DiskLogger {
public:
    DiskLogger() = default;
    ~DiskLogger();

    DiskLogger(const DiskLogger&) = delete;
    DiskLogger& operator=(const DiskLogger&) = delete;

    // Opens file for writing. Creates directories if needed.
    // Returns false on failure.
    bool open(const std::string& path);

    // Auto-generates filename: <output_dir>/mde_<YYYYMMDD_HHMMSS>.bin
    bool open_with_timestamp(const std::string& output_dir);

    // Writes a single depth update. Returns false on I/O error.
    bool write(const core::DepthUpdate& update);

    // Flushes and closes the file, updating the record count in header.
    void close();

    bool is_open() const { return file_ != nullptr; }
    uint32_t record_count() const { return record_count_; }
    const std::string& path() const { return path_; }

private:
    std::FILE* file_ = nullptr;
    std::string path_;
    uint32_t record_count_ = 0;
};

// Reads DepthRecords from a binary file.
// Used by ReplayEngine and tests.
class DiskReader {
public:
    DiskReader() = default;
    ~DiskReader();

    DiskReader(const DiskReader&) = delete;
    DiskReader& operator=(const DiskReader&) = delete;

    // Opens file for reading. Validates magic and version.
    bool open(const std::string& path);

    // Reads next record. Returns false at EOF or on error.
    bool read_next(DepthRecord& record, RecordHeader& header);

    // Resets to the first record (after header).
    bool rewind();

    void close();

    bool is_open() const { return file_ != nullptr; }
    const FileHeader& file_header() const { return header_; }

private:
    std::FILE* file_ = nullptr;
    FileHeader header_;
};

} // namespace mde::output
