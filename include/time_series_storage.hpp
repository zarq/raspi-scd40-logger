#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/db_ttl.h>
#include "sensor_data.hpp"
#include "performance_cache.hpp"

namespace sensor_daemon {

/**
 * Time-series storage engine using RocksDB backend
 * Optimized for write-heavy workloads with efficient time-based queries
 */
class TimeSeriesStorage {
public:
    /**
     * Constructor
     */
    TimeSeriesStorage() = default;
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~TimeSeriesStorage();
    
    // Non-copyable but movable
    TimeSeriesStorage(const TimeSeriesStorage&) = delete;
    TimeSeriesStorage& operator=(const TimeSeriesStorage&) = delete;
    TimeSeriesStorage(TimeSeriesStorage&&) = default;
    TimeSeriesStorage& operator=(TimeSeriesStorage&&) = default;
    
    /**
     * Initialize the storage engine with the specified data directory
     * @param data_directory Path to directory where database files will be stored
     * @param retention_hours Data retention period in hours (default: 24*365 = 1 year)
     * @return true if initialization successful, false otherwise
     */
    bool initialize(const std::string& data_directory, 
                   std::chrono::hours retention_hours = std::chrono::hours(24 * 365));
    
    /**
     * Store a sensor reading in the time-series database
     * @param reading Sensor reading to store
     * @return true if storage successful, false otherwise
     */
    bool store_reading(const SensorData& reading);
    
    /**
     * Check if the storage engine is healthy and operational
     * @return true if healthy, false if there are issues
     */
    bool is_healthy() const;
    
    /**
     * Get the current database size in bytes
     * @return Database size in bytes, or 0 if unable to determine
     */
    uint64_t get_database_size() const;
    
    /**
     * Get statistics about storage operations
     * @return String containing database statistics
     */
    std::string get_statistics() const;
    
    /**
     * Manually trigger cleanup of old data (normally handled automatically by TTL)
     * This is primarily for testing purposes
     */
    void cleanup_old_data();
    
    /**
     * Get recent readings (newest first) with caching
     * @param count Maximum number of readings to retrieve (default: 100)
     * @return Vector of sensor readings in reverse chronological order
     */
    std::vector<SensorData> get_recent_readings(int count = 100) const;
    
    /**
     * Get recent readings without caching (for cache population)
     * @param count Maximum number of readings to retrieve
     * @return Vector of sensor readings in reverse chronological order
     */
    std::vector<SensorData> get_recent_readings_no_cache(int count) const;
    
    /**
     * Get readings in time range with optimized memory usage
     * @param start Start timestamp (inclusive)
     * @param end End timestamp (inclusive)
     * @param max_results Maximum number of results to prevent memory exhaustion (default: 10000)
     * @return Vector of sensor readings within the time range
     */
    std::vector<SensorData> get_readings_in_range(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end,
        int max_results = 10000) const;
    
    /**
     * Stream readings in time range for large result sets
     * @param start Start timestamp (inclusive)
     * @param end End timestamp (inclusive)
     * @param callback Function to call for each batch of readings
     * @param batch_size Number of readings per batch (default: 1000)
     * @param max_results Maximum total results (default: 50000)
     * @return Number of readings processed
     */
    size_t stream_readings_in_range(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end,
        std::function<bool(const std::vector<SensorData>&)> callback,
        size_t batch_size = 1000,
        size_t max_results = 50000) const;
    
    /**
     * Database information structure
     */
    struct DatabaseInfo {
        uint64_t total_records;
        uint64_t database_size_bytes;
        std::chrono::system_clock::time_point earliest_timestamp;
        std::chrono::system_clock::time_point latest_timestamp;
        std::string database_path;
        bool is_healthy;
        std::string implementation = "RocksDB via HTTP API";
    };
    
    /**
     * Get database information and statistics
     * @return Database information structure
     */
    DatabaseInfo get_database_info() const;
    
    /**
     * Get performance metrics for storage operations
     * @return Query performance metrics
     */
    QueryPerformanceMonitor::QueryMetrics get_performance_metrics() const;
    
    /**
     * Get cache metrics
     * @return Cache performance metrics
     */
    CacheMetrics get_cache_metrics() const;
    
    /**
     * Clear performance cache
     */
    void clear_cache();
    
    /**
     * Warm up cache with recent readings
     * @param counts List of count values to pre-cache
     */
    void warm_cache(const std::vector<int>& counts = {10, 50, 100, 500});
    
private:
    std::unique_ptr<rocksdb::DBWithTTL> db_;
    std::string data_directory_;
    std::chrono::hours retention_hours_;
    
    // Performance optimization components
    mutable std::unique_ptr<RecentReadingsCache> recent_cache_;
    mutable std::unique_ptr<QueryPerformanceMonitor> performance_monitor_;
    
    // Background cache maintenance
    mutable std::chrono::steady_clock::time_point last_cache_cleanup_;
    static constexpr std::chrono::minutes CACHE_CLEANUP_INTERVAL{5};
    
    /**
     * Get optimized RocksDB options for time-series data
     * @return Configured RocksDB options
     */
    rocksdb::Options get_db_options() const;
    
    /**
     * Convert timestamp to RocksDB key format
     * Uses big-endian 8-byte format for proper lexicographic ordering
     * @param timestamp Time point to convert
     * @return 8-byte key string
     */
    std::string timestamp_to_key(std::chrono::system_clock::time_point timestamp) const;
    
    /**
     * Convert RocksDB key back to timestamp
     * @param key 8-byte key string
     * @return Time point, or nullopt if key is invalid
     */
    std::optional<std::chrono::system_clock::time_point> key_to_timestamp(const std::string& key) const;
    
    /**
     * Create a RocksDB iterator for reading data
     * @return Unique pointer to iterator
     */
    std::unique_ptr<rocksdb::Iterator> create_iterator() const;
    
    /**
     * Get the earliest possible key (for range queries)
     * @return Key string representing earliest timestamp
     */
    std::string get_start_key() const;
    
    /**
     * Get the latest possible key (for range queries)
     * @return Key string representing latest timestamp
     */
    std::string get_end_key() const;
    
    /**
     * Check if there's sufficient disk space for operations
     * @return true if disk space is adequate
     */
    bool check_disk_space() const;
    
    /**
     * Log storage-related errors with context
     * @param operation Description of the operation that failed
     * @param status RocksDB status containing error information
     */
    void log_storage_error(const std::string& operation, const rocksdb::Status& status) const;
    
    /**
     * Check available disk space and return true if sufficient
     * @return true if disk space is adequate, false if running low
     */
    bool check_disk_space() const;
    
    /**
     * Log storage error with context information
     * @param operation Description of the operation that failed
     * @param status RocksDB status object with error details
     */
    void log_storage_error(const std::string& operation, const rocksdb::Status& status) const;
    
    /**
     * Initialize performance optimization components
     */
    void initialize_performance_components() const;
    
    /**
     * Perform periodic cache maintenance
     */
    void maintain_cache() const;
    
    /**
     * Create optimized RocksDB iterator with prefetching
     * @param prefetch_size Number of keys to prefetch (0 for default)
     * @return Unique pointer to iterator
     */
    std::unique_ptr<rocksdb::Iterator> create_optimized_iterator(size_t prefetch_size = 0) const;
};

} // namespace sensor_daemon