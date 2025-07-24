#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <random>
#include "time_series_storage.hpp"
#include "performance_cache.hpp"
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"

using namespace sensor_daemon;

class PerformanceOptimizationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_performance_optimizations";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        // Initialize storage
        storage_ = std::make_unique<TimeSeriesStorage>();
        ASSERT_TRUE(storage_->initialize(test_dir_, std::chrono::hours(24)));
        
        // Initialize health monitor
        health_monitor_ = std::make_unique<HealthMonitor>();
        AlertConfig config;
        ASSERT_TRUE(health_monitor_->initialize(config));
        
        // Populate test data
        populate_test_data();
    }
    
    void TearDown() override {
        if (server_ && server_->is_running()) {
            server_->stop();
        }
        server_.reset();
        storage_.reset();
        health_monitor_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    void populate_test_data() {
        // Create test data with varying timestamps
        auto base_time = std::chrono::system_clock::now() - std::chrono::hours(24);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> co2_dist(400.0, 1200.0);
        std::uniform_real_distribution<> temp_dist(18.0, 28.0);
        std::uniform_real_distribution<> humidity_dist(30.0, 70.0);
        
        // Insert 10,000 readings over 24 hours
        for (int i = 0; i < 10000; ++i) {
            SensorData reading;
            reading.timestamp = base_time + std::chrono::minutes(i * 144 / 100); // ~1.44 minutes apart
            reading.co2_ppm = co2_dist(gen);
            reading.temperature_c = temp_dist(gen);
            reading.humidity_percent = humidity_dist(gen);
            reading.quality_flags = 0;
            
            ASSERT_TRUE(storage_->store_reading(reading));
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<HealthMonitorServer> server_;
};

TEST_F(PerformanceOptimizationsTest, CachePerformance) {
    // Test cache hit performance
    auto start_time = std::chrono::steady_clock::now();
    
    // First call - should be cache miss
    auto readings1 = storage_->get_recent_readings(100);
    auto first_call_time = std::chrono::steady_clock::now();
    
    // Second call - should be cache hit
    auto readings2 = storage_->get_recent_readings(100);
    auto second_call_time = std::chrono::steady_clock::now();
    
    // Calculate durations
    auto first_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        first_call_time - start_time).count();
    auto second_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        second_call_time - first_call_time).count();
    
    // Verify results are identical
    EXPECT_EQ(readings1.size(), readings2.size());
    EXPECT_EQ(readings1.size(), 100);
    
    // Cache hit should be significantly faster
    EXPECT_LT(second_duration, first_duration / 2);
    
    // Check cache metrics
    auto cache_metrics = storage_->get_cache_metrics();
    EXPECT_GE(cache_metrics.hits.load(), 1);
    EXPECT_GE(cache_metrics.misses.load(), 1);
    EXPECT_GT(cache_metrics.get_hit_ratio(), 0.0);
    
    std::cout << "First call (cache miss): " << first_duration << "ms" << std::endl;
    std::cout << "Second call (cache hit): " << second_duration << "ms" << std::endl;
    std::cout << "Cache hit ratio: " << cache_metrics.get_hit_ratio() << std::endl;
}

TEST_F(PerformanceOptimizationsTest, LargeQueryOptimization) {
    // Test memory usage optimization for large queries
    auto start_time = std::chrono::steady_clock::now();
    
    // Large query
    auto readings = storage_->get_recent_readings(5000);
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    EXPECT_LE(readings.size(), 5000);
    EXPECT_GT(readings.size(), 0);
    
    // Should complete within reasonable time (< 500ms for 5000 readings)
    EXPECT_LT(duration, 500);
    
    // Check performance metrics
    auto perf_metrics = storage_->get_performance_metrics();
    EXPECT_GT(perf_metrics.total_queries.load(), 0);
    EXPECT_GT(perf_metrics.get_average_duration_ms(), 0.0);
    
    std::cout << "Large query duration: " << duration << "ms" << std::endl;
    std::cout << "Results returned: " << readings.size() << std::endl;
    std::cout << "Average query time: " << perf_metrics.get_average_duration_ms() << "ms" << std::endl;
}

TEST_F(PerformanceOptimizationsTest, StreamingPerformance) {
    // Test streaming for very large result sets
    auto start_time = std::chrono::system_clock::now() - std::chrono::hours(12);
    auto end_time = std::chrono::system_clock::now();
    
    size_t total_processed = 0;
    size_t batch_count = 0;
    
    auto stream_start = std::chrono::steady_clock::now();
    
    size_t processed = storage_->stream_readings_in_range(
        start_time,
        end_time,
        [&total_processed, &batch_count](const std::vector<SensorData>& batch) -> bool {
            total_processed += batch.size();
            batch_count++;
            
            // Verify batch is not empty
            EXPECT_GT(batch.size(), 0);
            
            // Continue streaming
            return true;
        },
        1000, // Batch size
        10000 // Max results
    );
    
    auto stream_end = std::chrono::steady_clock::now();
    auto stream_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        stream_end - stream_start).count();
    
    EXPECT_EQ(processed, total_processed);
    EXPECT_GT(total_processed, 0);
    EXPECT_GT(batch_count, 0);
    
    // Streaming should be efficient
    EXPECT_LT(stream_duration, 1000); // < 1 second
    
    std::cout << "Streaming processed: " << total_processed << " readings" << std::endl;
    std::cout << "Batch count: " << batch_count << std::endl;
    std::cout << "Streaming duration: " << stream_duration << "ms" << std::endl;
    std::cout << "Throughput: " << (total_processed * 1000.0 / stream_duration) << " readings/sec" << std::endl;
}

TEST_F(PerformanceOptimizationsTest, CacheWarmup) {
    // Test cache warmup functionality
    storage_->clear_cache();
    
    // Verify cache is empty
    auto initial_metrics = storage_->get_cache_metrics();
    EXPECT_EQ(initial_metrics.total_requests.load(), 0);
    
    // Warm up cache
    storage_->warm_cache({10, 50, 100, 500});
    
    // Test that warmed values are cached
    auto start_time = std::chrono::steady_clock::now();
    auto readings = storage_->get_recent_readings(100);
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    // Should be fast due to cache warmup
    EXPECT_LT(duration, 10); // < 10ms for cached result
    
    // Check cache metrics
    auto cache_metrics = storage_->get_cache_metrics();
    EXPECT_GT(cache_metrics.hits.load(), 0);
    
    std::cout << "Warmed cache query duration: " << duration << "ms" << std::endl;
    std::cout << "Cache hit ratio after warmup: " << cache_metrics.get_hit_ratio() << std::endl;
}

TEST_F(PerformanceOptimizationsTest, ServerPerformanceMetrics) {
    // Test server performance monitoring
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    ASSERT_TRUE(server_->start(8090, "127.0.0.1"));
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Make several requests to generate metrics
    for (int i = 0; i < 5; ++i) {
        auto readings = storage_->get_recent_readings(100);
        EXPECT_GT(readings.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Check performance metrics
    auto perf_metrics = storage_->get_performance_metrics();
    EXPECT_GT(perf_metrics.total_queries.load(), 0);
    EXPECT_GT(perf_metrics.get_average_duration_ms(), 0.0);
    
    // Check cache metrics
    auto cache_metrics = storage_->get_cache_metrics();
    EXPECT_GT(cache_metrics.total_requests.load(), 0);
    
    std::cout << "Total queries: " << perf_metrics.total_queries.load() << std::endl;
    std::cout << "Average duration: " << perf_metrics.get_average_duration_ms() << "ms" << std::endl;
    std::cout << "Cache hit ratio: " << cache_metrics.get_hit_ratio() << std::endl;
}

TEST_F(PerformanceOptimizationsTest, MemoryUsageOptimization) {
    // Test that large queries don't consume excessive memory
    const size_t large_count = 8000;
    
    // Monitor memory usage (simplified check)
    auto readings = storage_->get_recent_readings(large_count);
    
    // Verify results are reasonable
    EXPECT_LE(readings.size(), large_count);
    EXPECT_GT(readings.size(), 0);
    
    // Check that performance monitoring detected the query
    auto perf_metrics = storage_->get_performance_metrics();
    EXPECT_GT(perf_metrics.total_queries.load(), 0);
    
    // Large queries should still complete in reasonable time
    EXPECT_LT(perf_metrics.get_average_duration_ms(), 1000.0); // < 1 second average
    
    std::cout << "Large query returned: " << readings.size() << " readings" << std::endl;
    std::cout << "Average query time: " << perf_metrics.get_average_duration_ms() << "ms" << std::endl;
}

TEST_F(PerformanceOptimizationsTest, ConcurrentAccessPerformance) {
    // Test performance under concurrent access
    const int num_threads = 4;
    const int queries_per_thread = 10;
    
    std::vector<std::thread> threads;
    std::vector<std::chrono::milliseconds> durations(num_threads);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Launch concurrent queries
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, queries_per_thread, &durations]() {
            auto thread_start = std::chrono::steady_clock::now();
            
            for (int i = 0; i < queries_per_thread; ++i) {
                auto readings = storage_->get_recent_readings(100 + (t * 10));
                EXPECT_GT(readings.size(), 0);
            }
            
            auto thread_end = std::chrono::steady_clock::now();
            durations[t] = std::chrono::duration_cast<std::chrono::milliseconds>(
                thread_end - thread_start);
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    // Check that concurrent access performed well
    EXPECT_LT(total_duration, 5000); // < 5 seconds total
    
    // Check cache performance under concurrent load
    auto cache_metrics = storage_->get_cache_metrics();
    EXPECT_GT(cache_metrics.total_requests.load(), num_threads * queries_per_thread);
    
    std::cout << "Concurrent test duration: " << total_duration << "ms" << std::endl;
    std::cout << "Cache hit ratio under load: " << cache_metrics.get_hit_ratio() << std::endl;
    
    for (int t = 0; t < num_threads; ++t) {
        std::cout << "Thread " << t << " duration: " << durations[t].count() << "ms" << std::endl;
    }
}