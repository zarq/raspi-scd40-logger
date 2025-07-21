#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/db_ttl.h>
#include "sensor_data.hpp"

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
     * Get recent readings (newest first)
     * @param count Maximum number of readings to retrieve (default: 100)
     * @return Vector of sensor readings in reverse chronological order
     */
    std::vector<SensorData> get_recent_readings(int count = 100) const;
    
    /**
     * Get readings in time range
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
    
private:
    std::unique_ptr<rocksdb::DBWithTTL> db_;
    std::string data_directory_;
    std::chrono::hours retention_hours_;
    
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
};

} // namespace sensor_daemon