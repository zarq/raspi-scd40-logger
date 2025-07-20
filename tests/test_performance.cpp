#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#include "daemon_core.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"
#include "logging_system.hpp"

namespace sensor_daemon {
namespace test {

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_perf_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create test configuration file
        config_path_ = temp_dir_ / "perf_config.toml";
        create_test_config();
        
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::INFO, "", 1024*1024, 3, true);
    }
    
    void TearDown() override {
        LoggingSystem::shutdown();
        
        // Clean up temporary directory
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }
    
    void create_test_config() {
        std::ofstream config_file(config_path_);
        config_file << R"(
[daemon]
sampling_interval_seconds = 1
data_retention_days = 1
log_level = "info"

[sensor]
i2c_device = "/dev/null"
i2c_address = 98
connection_timeout_ms = 100
max_retries = 1

[storage]
data_directory = ")" << (temp_dir_ / "data").string() << R"("
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 5
)";
        config_file.close();
    }
    
    // Get current memory usage in bytes
    size_t get_memory_usage() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss * 1024; // Convert from KB to bytes on Linux
    }
    
    // Get current CPU usage percentage (simplified)
    double get_cpu_usage() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        
        auto user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
        auto sys_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
        
        return (user_time + sys_time) * 100.0; // Simplified calculation
    }
    
    std::filesystem::path temp_dir_;
    std::filesystem::path config_path_;
};

// Test memory usage requirement (<10MB)
TEST_F(PerformanceTest, MemoryUsageTest) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    // Record initial memory usage
    size_t initial_memory = get_memory_usage();
    
    // Start daemon
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(daemon.is_running());
    
    // Let daemon run for extended period to stabilize memory usage
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    
    // Check memory usage during operation
    auto metrics = daemon.get_metrics();
    size_t current_memory = get_memory_usage();
    size_t daemon_memory = current_memory - initial_memory;
    
    // Requirement: daemon should use less than 10MB of RAM
    const size_t MAX_MEMORY_BYTES = 10 * 1024 * 1024; // 10MB
    EXPECT_LT(daemon_memory, MAX_MEMORY_BYTES) 
        << "Daemon memory usage: " << daemon_memory / (1024*1024) << "MB, "
        << "limit: " << MAX_MEMORY_BYTES / (1024*1024) << "MB";
    
    // Also check the daemon's internal memory tracking
    EXPECT_LT(metrics.memory_usage_bytes, MAX_MEMORY_BYTES)
        << "Internal memory tracking: " << metrics.memory_usage_bytes / (1024*1024) << "MB";
    
    daemon.shutdown();
    daemon_thread.join();
    
    LOG_INFO("Memory usage test completed", {
        {"daemon_memory_mb", std::to_string(daemon_memory / (1024*1024))},
        {"internal_memory_mb", std::to_string(metrics.memory_usage_bytes / (1024*1024))},
        {"limit_mb", "10"}
    });
}

// Test storage write performance
TEST_F(PerformanceTest, StorageWritePerformanceTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "perf_data").string()));
    
    const int NUM_WRITES = 1000;
    std::vector<SensorData> test_readings;
    
    // Prepare test data
    auto base_time = std::chrono::system_clock::now();
    for (int i = 0; i < NUM_WRITES; ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::milliseconds(i * 100);
        reading.co2_ppm = 400.0f + (i % 100);
        reading.temperature_c = 20.0f + (i % 20) * 0.5f;
        reading.humidity_percent = 50.0f + (i % 30) * 1.0f;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        test_readings.push_back(reading);
    }
    
    // Measure write performance
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto& reading : test_readings) {
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate performance metrics
    double writes_per_second = (NUM_WRITES * 1000.0) / duration.count();
    double avg_write_time_ms = static_cast<double>(duration.count()) / NUM_WRITES;
    
    // Performance expectations
    EXPECT_GT(writes_per_second, 100) << "Should achieve at least 100 writes/second";
    EXPECT_LT(avg_write_time_ms, 10) << "Average write time should be less than 10ms";
    
    LOG_INFO("Storage write performance test completed", {
        {"writes_per_second", std::to_string(writes_per_second)},
        {"avg_write_time_ms", std::to_string(avg_write_time_ms)},
        {"total_writes", std::to_string(NUM_WRITES)},
        {"total_time_ms", std::to_string(duration.count())}
    });
}

// Test query response time requirement (<10ms)
TEST_F(PerformanceTest, QueryResponseTimeTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "query_data").string()));
    
    // Store test data for querying
    const int NUM_RECORDS = 10000;
    auto base_time = std::chrono::system_clock::now();
    
    for (int i = 0; i < NUM_RECORDS; ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + (i % 200);
        reading.temperature_c = 20.0f + (i % 40) * 0.25f;
        reading.humidity_percent = 50.0f + (i % 50) * 0.5f;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    // Test database statistics query performance
    const int NUM_QUERIES = 100;
    std::vector<std::chrono::nanoseconds> query_times;
    
    for (int i = 0; i < NUM_QUERIES; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Perform query operation (statistics retrieval)
        std::string stats = storage.get_statistics();
        EXPECT_FALSE(stats.empty());
        
        auto end = std::chrono::high_resolution_clock::now();
        query_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
    
    // Calculate query performance metrics
    auto total_time = std::accumulate(query_times.begin(), query_times.end(), 
                                     std::chrono::nanoseconds(0));
    auto avg_time_ns = total_time.count() / NUM_QUERIES;
    auto avg_time_ms = avg_time_ns / 1000000.0;
    
    // Find max query time
    auto max_time_ns = std::max_element(query_times.begin(), query_times.end())->count();
    auto max_time_ms = max_time_ns / 1000000.0;
    
    // Requirement: query response time should be less than 10ms
    EXPECT_LT(avg_time_ms, 10.0) << "Average query time should be less than 10ms";
    EXPECT_LT(max_time_ms, 50.0) << "Maximum query time should be reasonable";
    
    LOG_INFO("Query response time test completed", {
        {"avg_query_time_ms", std::to_string(avg_time_ms)},
        {"max_query_time_ms", std::to_string(max_time_ms)},
        {"num_queries", std::to_string(NUM_QUERIES)},
        {"num_records", std::to_string(NUM_RECORDS)}
    });
}

// Test CPU usage during operation
TEST_F(PerformanceTest, CPUUsageTest) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    // Start daemon
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(daemon.is_running());
    
    // Monitor CPU usage over time
    std::vector<double> cpu_samples;
    const int NUM_SAMPLES = 10;
    
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto metrics = daemon.get_metrics();
        cpu_samples.push_back(metrics.cpu_usage_percent);
    }
    
    daemon.shutdown();
    daemon_thread.join();
    
    // Calculate CPU usage statistics
    double avg_cpu = std::accumulate(cpu_samples.begin(), cpu_samples.end(), 0.0) / NUM_SAMPLES;
    double max_cpu = *std::max_element(cpu_samples.begin(), cpu_samples.end());
    
    // CPU usage should be reasonable for a lightweight daemon
    EXPECT_LT(avg_cpu, 10.0) << "Average CPU usage should be less than 10%";
    EXPECT_LT(max_cpu, 25.0) << "Maximum CPU usage should be less than 25%";
    
    LOG_INFO("CPU usage test completed", {
        {"avg_cpu_percent", std::to_string(avg_cpu)},
        {"max_cpu_percent", std::to_string(max_cpu)},
        {"num_samples", std::to_string(NUM_SAMPLES)}
    });
}

// Test database size growth and compression effectiveness
TEST_F(PerformanceTest, DatabaseSizeAndCompressionTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "compression_data").string()));
    
    uint64_t initial_size = storage.get_database_size();
    
    // Store a significant amount of data
    const int NUM_RECORDS = 5000;
    auto base_time = std::chrono::system_clock::now();
    
    for (int i = 0; i < NUM_RECORDS; ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + (i % 100);
        reading.temperature_c = 20.0f + (i % 20) * 0.5f;
        reading.humidity_percent = 50.0f + (i % 30) * 1.0f;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    uint64_t final_size = storage.get_database_size();
    uint64_t size_growth = final_size - initial_size;
    
    // Calculate storage efficiency
    double bytes_per_record = static_cast<double>(size_growth) / NUM_RECORDS;
    
    // With compression, each record should be reasonably small
    EXPECT_LT(bytes_per_record, 200) << "Each record should use less than 200 bytes on average";
    EXPECT_GT(bytes_per_record, 20) << "Each record should use at least 20 bytes (sanity check)";
    
    LOG_INFO("Database size and compression test completed", {
        {"bytes_per_record", std::to_string(bytes_per_record)},
        {"total_size_kb", std::to_string(final_size / 1024)},
        {"num_records", std::to_string(NUM_RECORDS)},
        {"compression_enabled", "true"}
    });
}

// Test concurrent access performance
TEST_F(PerformanceTest, ConcurrentAccessTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "concurrent_data").string()));
    
    const int NUM_THREADS = 4;
    const int WRITES_PER_THREAD = 250;
    std::vector<std::thread> threads;
    std::atomic<int> successful_writes{0};
    std::atomic<int> failed_writes{0};
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Launch concurrent writer threads
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            auto base_time = std::chrono::system_clock::now();
            
            for (int i = 0; i < WRITES_PER_THREAD; ++i) {
                SensorData reading;
                reading.timestamp = base_time + std::chrono::microseconds(t * 1000000 + i * 1000);
                reading.co2_ppm = 400.0f + t * 100 + i;
                reading.temperature_c = 20.0f + t + i * 0.1f;
                reading.humidity_percent = 50.0f + t * 5 + i * 0.2f;
                reading.quality_flags = SensorData::CO2_VALID | 
                                       SensorData::TEMP_VALID | 
                                       SensorData::HUMIDITY_VALID;
                
                if (storage.store_reading(reading)) {
                    successful_writes++;
                } else {
                    failed_writes++;
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Verify results
    int total_expected = NUM_THREADS * WRITES_PER_THREAD;
    EXPECT_EQ(successful_writes.load(), total_expected) << "All writes should succeed";
    EXPECT_EQ(failed_writes.load(), 0) << "No writes should fail";
    
    // Performance metrics
    double writes_per_second = (total_expected * 1000.0) / duration.count();
    
    EXPECT_GT(writes_per_second, 500) << "Should handle concurrent writes efficiently";
    
    LOG_INFO("Concurrent access test completed", {
        {"successful_writes", std::to_string(successful_writes.load())},
        {"failed_writes", std::to_string(failed_writes.load())},
        {"writes_per_second", std::to_string(writes_per_second)},
        {"num_threads", std::to_string(NUM_THREADS)},
        {"duration_ms", std::to_string(duration.count())}
    });
}

} // namespace test
} // namespace sensor_daemon