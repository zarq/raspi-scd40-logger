#include "time_series_storage.hpp"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>

namespace sensor_daemon {

TimeSeriesStorage::~TimeSeriesStorage() {
    if (db_) {
        db_.reset();
    }
}

bool TimeSeriesStorage::initialize(const std::string& data_directory, 
                                 std::chrono::hours retention_hours) {
    data_directory_ = data_directory;
    retention_hours_ = retention_hours;
    
    try {
        // Create data directory if it doesn't exist
        std::filesystem::create_directories(data_directory);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to create data directory " << data_directory 
                  << ": " << e.what() << std::endl;
        return false;
    }
    
    // Check disk space before initializing
    if (!check_disk_space()) {
        std::cerr << "Insufficient disk space in " << data_directory << std::endl;
        return false;
    }
    
    // Configure RocksDB options
    rocksdb::Options options = get_db_options();
    
    // Open database with TTL support
    rocksdb::DBWithTTL* db_ptr = nullptr;
    int32_t ttl_seconds = static_cast<int32_t>(retention_hours.count() * 3600);
    
    rocksdb::Status status = rocksdb::DBWithTTL::Open(
        options, 
        data_directory, 
        &db_ptr, 
        ttl_seconds,
        false  // read_only = false
    );
    
    if (!status.ok()) {
        log_storage_error("database initialization", status);
        return false;
    }
    
    db_.reset(db_ptr);
    
    std::cout << "TimeSeriesStorage initialized successfully at " << data_directory 
              << " with " << retention_hours.count() << " hour retention" << std::endl;
    
    return true;
}

bool TimeSeriesStorage::store_reading(const SensorData& reading) {
    if (!db_) {
        std::cerr << "Storage engine not initialized" << std::endl;
        return false;
    }
    
    // Check disk space before writing
    if (!check_disk_space()) {
        std::cerr << "Insufficient disk space for storing reading" << std::endl;
        return false;
    }
    
    // Convert timestamp to key
    std::string key = timestamp_to_key(reading.timestamp);
    
    // Serialize reading to protobuf
    std::string value = SensorDataConverter::serialize(reading);
    if (value.empty()) {
        std::cerr << "Failed to serialize sensor reading" << std::endl;
        return false;
    }
    
    // Store in database
    rocksdb::WriteOptions write_options;
    write_options.sync = false;  // Async writes for better performance
    write_options.disableWAL = false;  // Keep WAL for crash recovery
    
    rocksdb::Status status = db_->Put(write_options, key, value);
    
    if (!status.ok()) {
        log_storage_error("storing sensor reading", status);
        return false;
    }
    
    return true;
}

bool TimeSeriesStorage::is_healthy() const {
    if (!db_) {
        return false;
    }
    
    // Check if we can perform a basic operation
    std::string value;
    rocksdb::ReadOptions read_options;
    rocksdb::Status status = db_->Get(read_options, "health_check", &value);
    
    // It's OK if the key doesn't exist, we just want to ensure DB is responsive
    return status.ok() || status.IsNotFound();
}

uint64_t TimeSeriesStorage::get_database_size() const {
    if (!db_) {
        return 0;
    }
    
    try {
        uint64_t total_size = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(data_directory_)) {
            if (entry.is_regular_file()) {
                total_size += entry.file_size();
            }
        }
        return total_size;
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

std::string TimeSeriesStorage::get_statistics() const {
    if (!db_) {
        return "Database not initialized";
    }
    
    std::string stats;
    if (!db_->GetProperty("rocksdb.stats", &stats)) {
        return "Unable to retrieve statistics";
    }
    
    return stats;
}

void TimeSeriesStorage::cleanup_old_data() {
    if (!db_) {
        return;
    }
    
    // Manual compaction to trigger TTL cleanup
    rocksdb::CompactRangeOptions options;
    db_->CompactRange(options, nullptr, nullptr);
}

rocksdb::Options TimeSeriesStorage::get_db_options() const {
    rocksdb::Options options;
    
    // Basic options
    options.create_if_missing = true;
    options.error_if_exists = false;
    
    // Memory management - keep it lightweight
    options.write_buffer_size = 4 * 1024 * 1024;  // 4MB write buffer
    options.max_write_buffer_number = 2;
    options.target_file_size_base = 8 * 1024 * 1024;  // 8MB SST files
    
    // Compression for space efficiency
    options.compression = rocksdb::kSnappyCompression;
    
    // Optimize for time-series workload (mostly sequential writes)
    options.level0_file_num_compaction_trigger = 4;
    options.level0_slowdown_writes_trigger = 8;
    options.level0_stop_writes_trigger = 12;
    
    // Table options for better performance
    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_size = 4 * 1024;  // 4KB blocks
    table_options.cache_index_and_filter_blocks = true;
    table_options.pin_l0_filter_and_index_blocks_in_cache = true;
    
    // Small block cache to stay within memory limits
    table_options.block_cache = rocksdb::NewLRUCache(2 * 1024 * 1024);  // 2MB cache
    
    // Bloom filter for faster lookups
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
    
    options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
    
    // Logging
    options.info_log_level = rocksdb::WARN_LEVEL;
    
    return options;
}

std::string TimeSeriesStorage::timestamp_to_key(std::chrono::system_clock::time_point timestamp) const {
    // Convert to microseconds since epoch
    auto duration = timestamp.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    uint64_t timestamp_us = static_cast<uint64_t>(microseconds.count());
    
    // Convert to big-endian for proper lexicographic ordering
    uint64_t big_endian_timestamp = htobe64(timestamp_us);
    
    // Create 8-byte key
    std::string key(8, '\0');
    std::memcpy(key.data(), &big_endian_timestamp, 8);
    
    return key;
}

std::optional<std::chrono::system_clock::time_point> 
TimeSeriesStorage::key_to_timestamp(const std::string& key) const {
    if (key.size() != 8) {
        return std::nullopt;
    }
    
    // Extract big-endian timestamp
    uint64_t big_endian_timestamp;
    std::memcpy(&big_endian_timestamp, key.data(), 8);
    
    // Convert from big-endian
    uint64_t timestamp_us = be64toh(big_endian_timestamp);
    
    // Convert to time_point
    auto microseconds = std::chrono::microseconds(timestamp_us);
    return std::chrono::system_clock::time_point(microseconds);
}

bool TimeSeriesStorage::check_disk_space() const {
    struct statvfs stat;
    
    if (statvfs(data_directory_.c_str(), &stat) != 0) {
        return true;  // Assume OK if we can't check
    }
    
    // Calculate available space in bytes
    uint64_t available_bytes = stat.f_bavail * stat.f_frsize;
    
    // Require at least 100MB free space
    const uint64_t min_free_space = 100 * 1024 * 1024;
    
    return available_bytes > min_free_space;
}

void TimeSeriesStorage::log_storage_error(const std::string& operation, 
                                        const rocksdb::Status& status) const {
    std::cerr << "Storage error during " << operation 
              << ": " << status.ToString() << std::endl;
    
    // Log additional context for specific error types
    if (status.IsIOError()) {
        std::cerr << "  - This may indicate disk space or permission issues" << std::endl;
    } else if (status.IsCorruption()) {
        std::cerr << "  - Database corruption detected, may need recovery" << std::endl;
    } else if (status.IsNotSupported()) {
        std::cerr << "  - Operation not supported by current RocksDB configuration" << std::endl;
    }
}

} // namespace sensor_daemon