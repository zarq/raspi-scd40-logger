#include "performance_cache.hpp"
#include "logging_system.hpp"
#include <algorithm>

namespace sensor_daemon {

// QueryTimer destructor implementation
QueryPerformanceMonitor::QueryTimer::~QueryTimer() {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time_).count();
    
    monitor_.record_query(query_type_, duration, cached_, failed_);
}

// QueryPerformanceMonitor implementation
void QueryPerformanceMonitor::record_query(const std::string& query_type, uint64_t duration_ms, bool cached, bool failed) {
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
    
    // Log slow queries for monitoring
    if (duration_ms > SLOW_QUERY_THRESHOLD_MS) {
        LOG_WARN("Slow query detected", {
            {"query_type", query_type},
            {"duration_ms", std::to_string(duration_ms)},
            {"cached", cached ? "true" : "false"},
            {"failed", failed ? "true" : "false"}
        });
    }
}

} // namespace sensor_daemon