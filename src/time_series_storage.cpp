#include "time_series_storage.hpp"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>
#include <functional>
#include "logging_system.hpp"

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
    
    // Initialize performance optimization components
    initialize_performance_components();
    
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

std::vector<SensorData> TimeSeriesStorage::get_recent_readings(int count) const {
    if (!db_ || count <= 0) {
        return {};
    }
    
    // Initialize performance components if needed
    if (!recent_cache_ || !performance_monitor_) {
        initialize_performance_components();
    }
    
    // Start performance monitoring
    auto timer = performance_monitor_->start_query("recent_readings");
    
    // Perform cache maintenance if needed
    maintain_cache();
    
    // Check cache first
    auto cached_result = recent_cache_->get_recent_readings(count);
    if (cached_result.has_value()) {
        timer.mark_cached();
        LOG_DEBUG("Recent readings served from cache", {
            {"count", std::to_string(count)},
            {"cached_size", std::to_string(cached_result->size())}
        });
        return std::move(cached_result.value());
    }
    
    // Cache miss - fetch from database
    auto readings = get_recent_readings_no_cache(count);
    
    // Cache the result for future requests
    if (!readings.empty()) {
        std::vector<SensorData> readings_copy = readings; // Copy for caching
        recent_cache_->cache_recent_readings(count, std::move(readings_copy));
    }
    
    return readings;
}

std::vector<SensorData> TimeSeriesStorage::get_recent_readings_no_cache(int count) const {
    std::vector<SensorData> readings;
    
    if (!db_ || count <= 0) {
        return readings;
    }
    
    // Limit count to prevent excessive memory usage
    count = std::min(count, 10000);
    
    try {
        // Use optimized iterator with prefetching for better performance
        auto iterator = create_optimized_iterator(count);
        if (!iterator) {
            return readings;
        }
        
        // Reserve memory to avoid reallocations
        readings.reserve(count);
        
        // Start from the end (most recent) and work backwards
        iterator->SeekToLast();
        
        while (iterator->Valid() && static_cast<int>(readings.size()) < count) {
            // Deserialize the value
            auto sensor_data = SensorDataConverter::deserialize(iterator->value().ToString());
            if (sensor_data.has_value()) {
                readings.push_back(std::move(sensor_data.value()));
            }
            
            iterator->Prev();
        }
        
        if (!iterator->status().ok()) {
            LOG_ERROR("Iterator error in get_recent_readings_no_cache", {
                {"error", iterator->status().ToString()}
            });
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in get_recent_readings_no_cache", {
            {"error", e.what()}
        });
    }
    
    return readings;
}

std::vector<SensorData> TimeSeriesStorage::get_readings_in_range(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    int max_results) const {
    
    if (!db_ || start > end) {
        return {};
    }
    
    // Initialize performance components if needed
    if (!performance_monitor_) {
        initialize_performance_components();
    }
    
    // Start performance monitoring
    auto timer = performance_monitor_->start_query("range_readings");
    
    // Limit results to prevent excessive memory usage
    max_results = std::min(max_results, 50000);
    
    std::vector<SensorData> readings;
    
    try {
        // Use optimized iterator with prefetching
        auto iterator = create_optimized_iterator(std::min(max_results, 1000));
        if (!iterator) {
            timer.mark_failed();
            return readings;
        }
        
        // Reserve memory to avoid reallocations
        readings.reserve(std::min(max_results, 1000));
        
        // Create start and end keys
        std::string start_key = timestamp_to_key(start);
        std::string end_key = timestamp_to_key(end);
        
        // Seek to start position
        iterator->Seek(start_key);
        
        while (iterator->Valid() && 
               iterator->key().ToString() <= end_key && 
               static_cast<int>(readings.size()) < max_results) {
            
            // Deserialize the value
            auto sensor_data = SensorDataConverter::deserialize(iterator->value().ToString());
            if (sensor_data.has_value()) {
                readings.emplace_back(std::move(sensor_data.value()));
            }
            
            iterator->Next();
            
            // Periodically check memory usage for very large queries
            if (readings.size() % 5000 == 0) {
                // Log progress for large queries
                LOG_DEBUG("Range query progress", {
                    {"readings_processed", std::to_string(readings.size())},
                    {"max_results", std::to_string(max_results)}
                });
            }
        }
        
        if (!iterator->status().ok()) {
            LOG_ERROR("Iterator error in get_readings_in_range", {
                {"error", iterator->status().ToString()}
            });
            timer.mark_failed();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in get_readings_in_range", {
            {"error", e.what()}
        });
        timer.mark_failed();
    }
    
    return readings;
}

size_t TimeSeriesStorage::stream_readings_in_range(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::function<bool(const std::vector<SensorData>&)> callback,
    size_t batch_size,
    size_t max_results) const {
    
    if (!db_ || start > end || !callback) {
        return 0;
    }
    
    // Initialize performance components if needed
    if (!performance_monitor_) {
        initialize_performance_components();
    }
    
    // Start performance monitoring
    auto timer = performance_monitor_->start_query("stream_range_readings");
    
    size_t total_processed = 0;
    batch_size = std::min(batch_size, size_t(5000)); // Limit batch size
    max_results = std::min(max_results, size_t(100000)); // Limit total results
    
    try {
        // Use optimized iterator with prefetching
        auto iterator = create_optimized_iterator(batch_size);
        if (!iterator) {
            timer.mark_failed();
            return 0;
        }
        
        // Create start and end keys
        std::string start_key = timestamp_to_key(start);
        std::string end_key = timestamp_to_key(end);
        
        // Seek to start position
        iterator->Seek(start_key);
        
        std::vector<SensorData> batch;
        batch.reserve(batch_size);
        
        while (iterator->Valid() && 
               iterator->key().ToString() <= end_key && 
               total_processed < max_results) {
            
            // Deserialize the value
            auto sensor_data = SensorDataConverter::deserialize(iterator->value().ToString());
            if (sensor_data.has_value()) {
                batch.emplace_back(std::move(sensor_data.value()));
            }
            
            iterator->Next();
            
            // Process batch when full
            if (batch.size() >= batch_size) {
                if (!callback(batch)) {
                    // Callback requested to stop
                    break;
                }
                total_processed += batch.size();
                batch.clear();
                
                // Log progress for large streams
                if (total_processed % 10000 == 0) {
                    LOG_DEBUG("Stream query progress", {
                        {"readings_processed", std::to_string(total_processed)},
                        {"max_results", std::to_string(max_results)}
                    });
                }
            }
        }
        
        // Process remaining batch
        if (!batch.empty()) {
            if (callback(batch)) {
                total_processed += batch.size();
            }
        }
        
        if (!iterator->status().ok()) {
            LOG_ERROR("Iterator error in stream_readings_in_range", {
                {"error", iterator->status().ToString()}
            });
            timer.mark_failed();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in stream_readings_in_range", {
            {"error", e.what()}
        });
        timer.mark_failed();
    }
    
    return total_processed;
}

TimeSeriesStorage::DatabaseInfo TimeSeriesStorage::get_database_info() const {
    DatabaseInfo info;
    info.database_path = data_directory_;
    info.is_healthy = is_healthy();
    info.database_size_bytes = get_database_size();
    
    if (!db_) {
        info.total_records = 0;
        info.earliest_timestamp = std::chrono::system_clock::now();
        info.latest_timestamp = std::chrono::system_clock::now();
        return info;
    }
    
    try {
        // Get approximate record count
        std::string count_str;
        if (db_->GetProperty("rocksdb.estimate-num-keys", &count_str)) {
            info.total_records = std::stoull(count_str);
        } else {
            info.total_records = 0;
        }
        
        // Find earliest and latest timestamps
        auto iterator = create_iterator();
        if (iterator) {
            // Get earliest timestamp
            iterator->SeekToFirst();
            if (iterator->Valid()) {
                auto earliest = key_to_timestamp(iterator->key().ToString());
                if (earliest.has_value()) {
                    info.earliest_timestamp = earliest.value();
                } else {
                    info.earliest_timestamp = std::chrono::system_clock::now();
                }
            } else {
                info.earliest_timestamp = std::chrono::system_clock::now();
            }
            
            // Get latest timestamp
            iterator->SeekToLast();
            if (iterator->Valid()) {
                auto latest = key_to_timestamp(iterator->key().ToString());
                if (latest.has_value()) {
                    info.latest_timestamp = latest.value();
                } else {
                    info.latest_timestamp = std::chrono::system_clock::now();
                }
            } else {
                info.latest_timestamp = info.earliest_timestamp;
            }
        } else {
            info.earliest_timestamp = std::chrono::system_clock::now();
            info.latest_timestamp = std::chrono::system_clock::now();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in get_database_info: " << e.what() << std::endl;
        info.total_records = 0;
        info.earliest_timestamp = std::chrono::system_clock::now();
        info.latest_timestamp = std::chrono::system_clock::now();
    }
    
    return info;
}

std::unique_ptr<rocksdb::Iterator> TimeSeriesStorage::create_iterator() const {
    if (!db_) {
        return nullptr;
    }
    
    rocksdb::ReadOptions read_options;
    read_options.total_order_seek = true;  // Ensure consistent ordering
    
    return std::unique_ptr<rocksdb::Iterator>(db_->NewIterator(read_options));
}

std::string TimeSeriesStorage::get_start_key() const {
    // Return key for timestamp 0 (earliest possible)
    return timestamp_to_key(std::chrono::system_clock::time_point{});
}

std::string TimeSeriesStorage::get_end_key() const {
    // Return key for maximum timestamp
    auto max_time = std::chrono::system_clock::time_point::max();
    return timestamp_to_key(max_time);
}

QueryPerformanceMonitor::QueryMetrics TimeSeriesStorage::get_performance_metrics() const {
    if (!performance_monitor_) {
        initialize_performance_components();
    }
    return performance_monitor_->get_overall_metrics();
}

CacheMetrics TimeSeriesStorage::get_cache_metrics() const {
    if (!recent_cache_) {
        initialize_performance_components();
    }
    return recent_cache_->get_metrics();
}

void TimeSeriesStorage::clear_cache() {
    if (recent_cache_) {
        recent_cache_->clear();
        LOG_INFO("Storage cache cleared");
    }
}

void TimeSeriesStorage::warm_cache(const std::vector<int>& counts) {
    if (!recent_cache_) {
        initialize_performance_components();
    }
    
    LOG_INFO("Warming storage cache", {
        {"count_values", std::to_string(counts.size())}
    });
    
    for (int count : counts) {
        try {
            auto readings = get_recent_readings_no_cache(count);
            if (!readings.empty()) {
                recent_cache_->cache_recent_readings(count, std::move(readings));
                LOG_DEBUG("Cache warmed for count", {
                    {"count", std::to_string(count)},
                    {"readings", std::to_string(readings.size())}
                });
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to warm cache for count", {
                {"count", std::to_string(count)},
                {"error", e.what()}
            });
        }
    }
}

void TimeSeriesStorage::initialize_performance_components() const {
    if (!recent_cache_) {
        recent_cache_ = std::make_unique<RecentReadingsCache>(
            10,  // Cache up to 10 different count values
            std::chrono::seconds(30)  // Cache for 30 seconds
        );
    }
    
    if (!performance_monitor_) {
        performance_monitor_ = std::make_unique<QueryPerformanceMonitor>();
    }
    
    last_cache_cleanup_ = std::chrono::steady_clock::now();
}

void TimeSeriesStorage::maintain_cache() const {
    auto now = std::chrono::steady_clock::now();
    if (now - last_cache_cleanup_ >= CACHE_CLEANUP_INTERVAL) {
        if (recent_cache_) {
            recent_cache_->cleanup_expired();
        }
        last_cache_cleanup_ = now;
    }
}

std::unique_ptr<rocksdb::Iterator> TimeSeriesStorage::create_optimized_iterator(size_t prefetch_size) const {
    if (!db_) {
        return nullptr;
    }
    
    rocksdb::ReadOptions read_options;
    read_options.total_order_seek = true;  // Ensure consistent ordering
    
    // Enable prefetching for better performance on range scans
    if (prefetch_size > 0) {
        // Estimate prefetch size based on expected data size
        // Each sensor reading is roughly 100-200 bytes serialized
        size_t estimated_bytes = prefetch_size * 150;
        read_options.readahead_size = std::min(estimated_bytes, size_t(1024 * 1024)); // Max 1MB
    }
    
    // Use fill cache for frequently accessed data
    read_options.fill_cache = true;
    
    return std::unique_ptr<rocksdb::Iterator>(db_->NewIterator(read_options));
}

} // namespace sensor_daemon