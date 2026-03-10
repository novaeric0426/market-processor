#include "output/disk_logger.h"

#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <ctime>
#include <cstring>

namespace mde::output {

// --- DiskLogger ---

DiskLogger::~DiskLogger() {
    close();
}

bool DiskLogger::open(const std::string& path) {
    if (file_) close();

    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) {
        spdlog::error("DiskLogger: failed to open '{}' for writing", path);
        return false;
    }

    path_ = path;
    record_count_ = 0;

    FileHeader hdr;
    hdr.created_us = core::Clock::wall_us();
    if (std::fwrite(&hdr, sizeof(hdr), 1, file_) != 1) {
        spdlog::error("DiskLogger: failed to write file header");
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    spdlog::info("DiskLogger: opened '{}'", path);
    return true;
}

bool DiskLogger::open_with_timestamp(const std::string& output_dir) {
    // Create directory if it doesn't exist
    ::mkdir(output_dir.c_str(), 0755);

    // Generate filename: mde_YYYYMMDD_HHMMSS.bin
    auto now = std::time(nullptr);
    std::tm tm_buf;
    ::localtime_r(&now, &tm_buf);

    char filename[64];
    std::snprintf(filename, sizeof(filename),
        "mde_%04d%02d%02d_%02d%02d%02d.bin",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    std::string dir = output_dir;
    if (!dir.empty() && dir.back() != '/') dir += '/';

    return open(dir + filename);
}

bool DiskLogger::write(const core::DepthUpdate& update) {
    if (!file_) return false;

    RecordHeader rhdr;
    rhdr.wall_timestamp_us = core::Clock::wall_us();
    rhdr.payload_size = sizeof(DepthRecord);

    DepthRecord rec = to_record(update);

    if (std::fwrite(&rhdr, sizeof(rhdr), 1, file_) != 1) return false;
    if (std::fwrite(&rec, sizeof(rec), 1, file_) != 1) return false;

    ++record_count_;
    return true;
}

void DiskLogger::close() {
    if (!file_) return;

    // Update record count in header
    std::fseek(file_, offsetof(FileHeader, record_count), SEEK_SET);
    std::fwrite(&record_count_, sizeof(record_count_), 1, file_);

    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;

    spdlog::info("DiskLogger: closed '{}', {} records written", path_, record_count_);
}

// --- DiskReader ---

DiskReader::~DiskReader() {
    close();
}

bool DiskReader::open(const std::string& path) {
    if (file_) close();

    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        spdlog::error("DiskReader: failed to open '{}'", path);
        return false;
    }

    if (std::fread(&header_, sizeof(header_), 1, file_) != 1) {
        spdlog::error("DiskReader: failed to read file header");
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    if (header_.magic != FILE_MAGIC) {
        spdlog::error("DiskReader: invalid magic number 0x{:08X}", header_.magic);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    if (header_.version != FILE_VERSION) {
        spdlog::error("DiskReader: unsupported version {}", header_.version);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    return true;
}

bool DiskReader::read_next(DepthRecord& record, RecordHeader& rhdr) {
    if (!file_) return false;

    if (std::fread(&rhdr, sizeof(rhdr), 1, file_) != 1) return false;
    if (rhdr.payload_size != sizeof(DepthRecord)) {
        spdlog::warn("DiskReader: unexpected payload size {}", rhdr.payload_size);
        return false;
    }
    if (std::fread(&record, sizeof(record), 1, file_) != 1) return false;

    return true;
}

bool DiskReader::rewind() {
    if (!file_) return false;
    return std::fseek(file_, sizeof(FileHeader), SEEK_SET) == 0;
}

void DiskReader::close() {
    if (!file_) return;
    std::fclose(file_);
    file_ = nullptr;
}

} // namespace mde::output
