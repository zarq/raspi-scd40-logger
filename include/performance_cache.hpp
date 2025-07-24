#pragma once

#include <memory>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <atomic>
#include "sensor_data.hpp"

namespace sensor_daemon {

/**
 * Cache entry for storing frequently requested data
 */
template<typename T>
struct CacheEntry {
    T data;
    std::chrono::steady_clock::time_point timestamp;
    std::atomic<uint64_t> access_count{0};
    
    CacheEntry(T&& d) : data(std::move(d)), timestamp(std::chrono::steady_clock::now()) {}
    
    bool is_expired(std::chrono::seconds max_age) const {
        auto now = std::chrono::steady_clock::now();
        return (now - timestamp) > max_age;
    }
    
    void touch() {
        access_count.fetch_add(1, std::memory_order_relaxed);
    }
};

/**
 * Performance metrics for cache operations
 */
struct CacheMetrics {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> total_requests{0};
    
    double get_hit_ratio() const {
        uint64_t total = total_requests.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        return static_cast<double>(hits.load(std::memory_order_relaxed)) / total;
    }
    
    void record_hit() {
        hits.fetch_add(1, std::memory_order_relaxed);
        total_requests.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_miss() {
        misses.fetch_add(1, std::memory_order_relaxed);
        total_requests.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_eviction() {
        evictions.fetch_add(1, std::memory_order_relaxed);
    }
};

/**
 * LRU cache for performance optimization
 */
template<typename Key, typename Value>
class LRUCache {
public:
    /**
     * Constructor
     * @param max_size Maximum number of entries to cache
     * @param max_age Maximum age of cached entries
     */
    LRUCache(size_t max_size, std::chrono::seconds max_age = std::chrono::seconds(60))
        : max_size_(max_size), max_age_(max_age) {}
    
    /**
     * Get value from cache
     * @param key Cache key
     * @return Cached value if found and not expired
     */
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            metrics_.record_miss();
            return std::nullopt;
        }
        
        auto& entry = it->second;
        if (entry.is_expired(max_age_)) {
            cache_.erase(it);
            metrics_.record_miss();
            return std::nullopt;
        }
        
        entry.touch();
        metrics_.record_hit();
        return entry.data;
    }
    
    /**
     * Put value in cache
     * @param key Cache key
     * @param value Value to cache
     */
    void put(const Key& key, Value&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Remove existing entry if present
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            cache_.erase(it);
        }
        
        // Check if we need to evict entries
        while (cache_.size() >= max_size_) {
            evict_lru();
        }
        
        // Insert new entry
        cache_.emplace(key, CacheEntry<Value>(std::move(value)));
    }
    
    /**
     * Clear all cached entries
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    /**
     * Get cache metrics
     * @return Current cache metrics
     */
    CacheMetrics get_metrics() const {
        return metrics_;
    }
    
    /**
     * Get cache size
     * @return Number of entries in cache
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }
    
    /**
     * Clean up expired entries
     */
    void cleanup_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_.begin();
        while (it != cache_.end()) {
            if (it->second.is_expired(max_age_)) {
                it = cache_.erase(it);
                metrics_.record_eviction();
            } else {
                ++it;
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<Key, CacheEntry<Value>> cache_;
    size_t max_size_;
    std::chrono::seconds max_age_;
    mutable CacheMetrics metrics_;
    
    /**
     * Evict least recently used entry
     */
    void evict_lru() {
        if (cache_.empty()) return;
        
        auto oldest_it = cache_.begin();
        auto oldest_time = oldest_it->second.timestamp;
        
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.timestamp < oldest_time) {
                oldest_time = it->second.timestamp;
                oldest_it = it;
            }
        }
        
        cache_.erase(oldest_it);
        metrics_.record_eviction();
    }
};

/**
 * Specialized cache for recent sensor readings
 */
class RecentReadingsCache {
public:
    /**
     * Constructor
     * @param max_entries Maximum number of different count values to cache
     * @param max_age Maximum age of cached readings
     */
    RecentReadingsCache(size_t max_entries = 10, std::chrono::seconds max_age = std::chrono::seconds(30))
        : cache_(max_entries, max_age) {}
    
    /**
     * Get recent readings from cache
     * @param count Number of readings requested
     * @return Cached readings if available and fresh
     */
    std::optional<std::vector<SensorData>> get_recent_readings(int count) {
        return cache_.get(count);
    }
    
    /**
     * Cache recent readings
     * @param count Number of readings
     * @param readings Sensor readings to cache
     */
    void cache_recent_readings(int count, std::vector<SensorData>&& readings) {
        cache_.put(count, std::move(readings));
    }
    
    /**
     * Clear cache
     */
    void clear() {
        cache_.clear();
    }
    
    /**
     * Get cache metrics
     * @return Cache performance metrics
     */
    CacheMetrics get_metrics() const {
        return cache_.get_metrics();
    }
    
    /**
     * Clean up expired entries
     */
    void cleanup_expired() {
        cache_.cleanup_expired();
    }

private:
    LRUCache<int, std::vector<SensorData>> cache_;
};

/**
 * Performance monitoring for query operations
 */
class QueryPerformanceMonitor {
public:
    /**
     * Query performance metrics
     */
    struct QueryMetrics {
        std::atomic<uint64_t> total_queries{0};
        std::atomic<uint64_t> total_duration_ms{0};
        std::atomic<uint64_t> slow_queries{0};  // Queries > 100ms
        std::atomic<uint64_t> failed_queries{0};
        std::atomic<uint64_t> cached_queries{0};
        
        double get_average_duration_ms() const {
            uint64_t total = total_queries.load(std::memory_order_relaxed);
            if (total == 0) return 0.0;
            return static_cast<double>(total_duration_ms.load(std::memory_order_relaxed)) / total;
        }
        
        double get_slow_query_ratio() const {
            uint64_t total = total_queries.load(std::memory_order_relaxed);
            if (total == 0) return 0.0;
            return static_cast<double>(slow_queries.load(std::memory_order_relaxed)) / total;
        }
        
        double get_cache_hit_ratio() const {
            uint64_t total = total_queries.load(std::memory_order_relaxed);
            if (total == 0) return 0.0;
            return static_cast<double>(cached_queries.load(std::memory_order_relaxed)) / total;
        }
    };
    
    /**
     * RAII timer for measuring query performance
     */
    class QueryTimer {
    public:
        QueryTimer(QueryPerformanceMonitor& monitor, const std::string& query_type)
            : monitor_(monitor), query_type_(query_type), 
              start_time_(std::chrono::steady_clock::now()) {}
        
        ~QueryTimer() {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time_).count();
            
            monitor_.record_query(query_type_, duration, false, false);
        }
        
        void mark_cached() {
            cached_ = true;
        }
        
        void mark_failed() {
            failed_ = true;
        }

    private:
        QueryPerformanceMonitor& monitor_;
        std::string query_type_;
        std::chrono::steady_clock::time_point start_time_;
        bool cached_ = false;
        bool failed_ = false;
        
        friend class QueryPerformanceMonitor;
    };
    
    /**
     * Create a query timer
     * @param query_type Type of query being performed
     * @return RAII timer object
     */
    QueryTimer start_query(const std::string& query_type) {
        return QueryTimer(*this, query_type);
    }
    
    /**
     * Get metrics for a specific query type
     * @param query_type Query type to get metrics for
     * @return Query metrics
     */
    QueryMetrics get_metrics(const std::string& query_type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(query_type);
        if (it != metrics_.end()) {
            return it->second;
        }
        return QueryMetrics{};
    }
    
    /**
     * Get overall metrics across all query types
     * @return Aggregated query metrics
     */
    QueryMetrics get_overall_metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        QueryMetrics overall;
        for (const auto& [type, metrics] : metrics_) {
            overall.total_queries += metrics.total_queries.load(std::memory_order_relaxed);
            overall.total_duration_ms += metrics.total_duration_ms.load(std::memory_order_relaxed);
            overall.slow_queries += metrics.slow_queries.load(std::memory_order_relaxed);
            overall.failed_queries += metrics.failed_queries.load(std::memory_order_relaxed);
            overall.cached_queries += metrics.cached_queries.load(std::memory_order_relaxed);
        }
        
        return overall;
    }
    
    /**
     * Reset all metrics
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, QueryMetrics> metrics_;
    
    static constexpr uint64_t SLOW_QUERY_THRESHOLD_MS = 100;
    
    /**
     * Record query performance
     * @param query_type Type of query
     * @param duration_ms Query duration in milliseconds
     * @param cached Whether query was served from cache
     * @param failed Whether query failed
     */
    void record_query(const std::string& query_type, uint64_t duration_ms, bool cached, bool failed) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& metrics = metrics_[query_type];
        metrics.total_queries.fetch_add(1, std::memory_order_relaxed);
        metrics.total_duration_ms.fetch_add(duration_ms, std::memory_order_relaxed);
        
        if (duration_ms > SLOW_QUERY_THRESHOLD_MS) {
            metrics.slow_queries.fetch_add(1, std::memory_order_relaxed);
        }
        
        if (cached) {
            metrics.cached_queries.fetch_add(1, std::memory_order_relaxed);
        }
        
        if (failed) {
            metrics.failed_queries.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    friend class QueryTimer;
};

} // namespace sensor_daemon