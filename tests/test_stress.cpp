#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <random>
#include <sys/resource.h>
#include "daemon_core.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"
#include "logging_system.hpp"

namespace sensor_daemon {
namespace test {

class StressTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_stress_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create test configuration file
        config_path_ = temp_dir_ / "stress_config.toml";
        create_test_config();
        
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::WARN, "", 1024*1024, 3, true);
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
log_level = "warn"

[sensor]
i2c_device = "/dev/null"
i2c_address = 98
connection_timeout_ms = 50
max_retries = 1

[storage]
data_directory = ")" << (temp_dir_ / "data").string() << R"("
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 8
)";
        config_file.close();
    }
    
    // Monitor resource usage during stress test
    struct ResourceUsage {
        size_t max_memory_bytes = 0;
        double max_cpu_percent = 0.0;
        uint64_t total_writes = 0;
        uint64_t failed_writes = 0;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point end_time;
    };
    
    ResourceUsage monitor_resources(std::function<void()> test_function) {
        ResourceUsage usage;
        usage.start_time = std::chrono::steady_clock::now();
        
        std::atomic<bool> monitoring{true};
        std::thread monitor_thread([&]() {
            while (monitoring) {
                struct rusage rusage;
                getrusage(RUSAGE_SELF, &rusage);
                
                size_t current_memory = rusage.ru_maxrss * 1024; // Convert KB to bytes
                usage.max_memory_bytes = std::max(usage.max_memory_bytes, current_memory);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        
        // Run the test function
        test_function();
        
        monitoring = false;
        monitor_thread.join();
        
        usage.end_time = std::chrono::steady_clock::now();
        return usage;
    }
    
    std::filesystem::path temp_dir_;
    std::filesystem::path config_path_;
};

// Test extended operation (30 seconds of continuous operation)
TEST_F(StressTest, ExtendedOperationTest) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    std::atomic<bool> test_running{true};
    ResourceUsage usage;
    
    auto test_function = [&]() {
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        // Wait for daemon to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ASSERT_TRUE(daemon.is_running());
        
        // Run for extended period (30 seconds)
        const auto TEST_DURATION = std::chrono::seconds(30);
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < TEST_DURATION) {
            EXPECT_TRUE(daemon.is_running()) << "Daemon should remain running during stress test";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        // Get final metrics
        auto metrics = daemon.get_metrics();
        usage.total_writes = metrics.storage_writes_success;
        usage.failed_writes = metrics.storage_writes_failed;
        
        daemon.shutdown();
        daemon_thread.join();
        test_running = false;
    };
    
    usage = monitor_resources(test_function);
    
    // Verify daemon survived extended operation
    EXPECT_FALSE(test_running) << "Test should complete successfully";
    
    // Memory usage should remain within limits
    const size_t MAX_MEMORY_BYTES = 15 * 1024 * 1024; // 15MB (allowing some overhead for stress)
    EXPECT_LT(usage.max_memory_bytes, MAX_MEMORY_BYTES)
        << "Memory usage during stress: " << usage.max_memory_bytes / (1024*1024) << "MB";
    
    // Should have attempted some operations
    EXPECT_GT(usage.total_writes + usage.failed_writes, 20) 
        << "Should have attempted multiple operations during 30 seconds";
    
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(usage.end_time - usage.start_time);
    
    LOG_INFO("Extended operation stress test completed", {
        {"duration_seconds", std::to_string(duration.count())},
        {"max_memory_mb", std::to_string(usage.max_memory_bytes / (1024*1024))},
        {"total_operations", std::to_string(usage.total_writes + usage.failed_writes)},
        {"success_rate", std::to_string(static_cast<double>(usage.total_writes) / 
                                       (usage.total_writes + usage.failed_writes) * 100.0)}
    });
}

// Test high-frequency data generation stress
TEST_F(StressTest, HighFrequencyDataStressTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "stress_data").string()));
    
    const int NUM_RECORDS = 50000; // Large number of records
    const int BATCH_SIZE = 1000;
    std::atomic<int> successful_writes{0};
    std::atomic<int> failed_writes{0};
    
    auto test_function = [&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> co2_dist(300.0f, 2000.0f);
        std::uniform_real_distribution<float> temp_dist(-10.0f, 50.0f);
        std::uniform_real_distribution<float> humidity_dist(0.0f, 100.0f);
        
        auto base_time = std::chrono::system_clock::now();
        
        for (int batch = 0; batch < NUM_RECORDS / BATCH_SIZE; ++batch) {
            for (int i = 0; i < BATCH_SIZE; ++i) {
                SensorData reading;
                reading.timestamp = base_time + std::chrono::microseconds(
                    (batch * BATCH_SIZE + i) * 100); // 100μs intervals
                reading.co2_ppm = co2_dist(gen);
                reading.temperature_c = temp_dist(gen);
                reading.humidity_percent = humidity_dist(gen);
                reading.quality_flags = SensorData::CO2_VALID | 
                                       SensorData::TEMP_VALID | 
                                       SensorData::HUMIDITY_VALID;
                
                if (storage.store_reading(reading)) {
                    successful_writes++;
                } else {
                    failed_writes++;
                }
            }
            
            // Brief pause between batches to prevent overwhelming the system
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    
    auto usage = monitor_resources(test_function);
    
    // Verify results
    int total_attempts = successful_writes.load() + failed_writes.load();
    double success_rate = static_cast<double>(successful_writes.load()) / total_attempts * 100.0;
    
    EXPECT_EQ(total_attempts, NUM_RECORDS) << "Should have attempted all writes";
    EXPECT_GT(success_rate, 95.0) << "Success rate should be high during stress test";
    
    // Storage should remain healthy
    EXPECT_TRUE(storage.is_healthy()) << "Storage should remain healthy after stress test";
    
    // Database size should be reasonable
    uint64_t db_size = storage.get_database_size();
    double bytes_per_record = static_cast<double>(db_size) / successful_writes.load();
    EXPECT_LT(bytes_per_record, 500) << "Storage efficiency should remain good under stress";
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(usage.end_time - usage.start_time);
    double writes_per_second = (successful_writes.load() * 1000.0) / duration.count();
    
    LOG_INFO("High-frequency data stress test completed", {
        {"successful_writes", std::to_string(successful_writes.load())},
        {"failed_writes", std::to_string(failed_writes.load())},
        {"success_rate_percent", std::to_string(success_rate)},
        {"writes_per_second", std::to_string(writes_per_second)},
        {"bytes_per_record", std::to_string(bytes_per_record)},
        {"duration_ms", std::to_string(duration.count())}
    });
}

// Test memory leak detection during extended operation
TEST_F(StressTest, MemoryLeakDetectionTest) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    std::vector<size_t> memory_samples;
    std::atomic<bool> test_running{true};
    
    auto test_function = [&]() {
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        // Wait for daemon to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ASSERT_TRUE(daemon.is_running());
        
        // Sample memory usage over time
        const int NUM_SAMPLES = 20;
        const auto SAMPLE_INTERVAL = std::chrono::milliseconds(1000);
        
        for (int i = 0; i < NUM_SAMPLES; ++i) {
            struct rusage usage;
            getrusage(RUSAGE_SELF, &usage);
            memory_samples.push_back(usage.ru_maxrss * 1024); // Convert to bytes
            
            std::this_thread::sleep_for(SAMPLE_INTERVAL);
        }
        
        daemon.shutdown();
        daemon_thread.join();
        test_running = false;
    };
    
    auto usage = monitor_resources(test_function);
    
    // Analyze memory usage trend
    ASSERT_GE(memory_samples.size(), 10) << "Should have collected enough samples";
    
    // Calculate memory growth trend
    size_t initial_memory = memory_samples[2]; // Skip first few samples for stability
    size_t final_memory = memory_samples.back();
    size_t max_memory = *std::max_element(memory_samples.begin(), memory_samples.end());
    
    // Memory should not grow significantly over time (indicating no major leaks)
    double memory_growth_percent = static_cast<double>(final_memory - initial_memory) / 
                                  initial_memory * 100.0;
    
    EXPECT_LT(std::abs(memory_growth_percent), 50.0) 
        << "Memory growth should be limited (current: " << memory_growth_percent << "%)";
    
    // Maximum memory should be within reasonable bounds
    const size_t MAX_MEMORY_BYTES = 20 * 1024 * 1024; // 20MB for stress test
    EXPECT_LT(max_memory, MAX_MEMORY_BYTES)
        << "Maximum memory usage: " << max_memory / (1024*1024) << "MB";
    
    LOG_INFO("Memory leak detection test completed", {
        {"initial_memory_mb", std::to_string(initial_memory / (1024*1024))},
        {"final_memory_mb", std::to_string(final_memory / (1024*1024))},
        {"max_memory_mb", std::to_string(max_memory / (1024*1024))},
        {"memory_growth_percent", std::to_string(memory_growth_percent)},
        {"num_samples", std::to_string(memory_samples.size())}
    });
}

// Test error recovery under continuous stress
TEST_F(StressTest, ErrorRecoveryStressTest) {
    // Create configuration with problematic settings to induce errors
    auto error_config_path = temp_dir_ / "error_stress_config.toml";
    std::ofstream config_file(error_config_path);
    config_file << R"(
[daemon]
sampling_interval_seconds = 1
data_retention_days = 1
log_level = "error"

[sensor]
i2c_device = "/dev/nonexistent"
i2c_address = 98
connection_timeout_ms = 10
max_retries = 1

[storage]
data_directory = ")" << (temp_dir_ / "error_data").string() << R"("
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 2
)";
    config_file.close();
    
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(error_config_path.string(), true));
    
    std::atomic<bool> test_completed{false};
    
    auto test_function = [&]() {
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        // Wait for daemon to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ASSERT_TRUE(daemon.is_running());
        
        // Run under error conditions for extended period
        const auto TEST_DURATION = std::chrono::seconds(15);
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < TEST_DURATION) {
            // Daemon should continue running despite errors
            EXPECT_TRUE(daemon.is_running()) 
                << "Daemon should remain running despite continuous errors";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        daemon.shutdown();
        daemon_thread.join();
        test_completed = true;
    };
    
    auto usage = monitor_resources(test_function);
    
    EXPECT_TRUE(test_completed) << "Error recovery stress test should complete";
    
    // Get final metrics to verify error handling
    auto metrics = daemon.get_metrics();
    
    // Should have many failed sensor readings due to invalid device
    EXPECT_GT(metrics.sensor_readings_failed, 10) 
        << "Should have recorded sensor failures during stress test";
    
    // Daemon should have remained stable despite errors
    EXPECT_GT(metrics.get_uptime().count(), 10) 
        << "Daemon should have run for significant time despite errors";
    
    // Memory usage should remain reasonable even with errors
    const size_t MAX_MEMORY_BYTES = 15 * 1024 * 1024; // 15MB
    EXPECT_LT(usage.max_memory_bytes, MAX_MEMORY_BYTES)
        << "Memory usage should remain controlled during error conditions";
    
    LOG_INFO("Error recovery stress test completed", {
        {"sensor_failures", std::to_string(metrics.sensor_readings_failed)},
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())},
        {"max_memory_mb", std::to_string(usage.max_memory_bytes / (1024*1024))},
        {"test_duration_seconds", "15"}
    });
}

// Test storage under extreme load
TEST_F(StressTest, StorageExtremeLoadTest) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize((temp_dir_ / "extreme_data").string()));
    
    const int NUM_THREADS = 8;
    const int WRITES_PER_THREAD = 2500;
    const int TOTAL_WRITES = NUM_THREADS * WRITES_PER_THREAD;
    
    std::vector<std::thread> threads;
    std::atomic<int> successful_writes{0};
    std::atomic<int> failed_writes{0};
    std::atomic<bool> storage_healthy{true};
    
    auto test_function = [&]() {
        // Launch multiple writer threads
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> co2_dist(300.0f, 1500.0f);
                std::uniform_real_distribution<float> temp_dist(15.0f, 35.0f);
                std::uniform_real_distribution<float> humidity_dist(30.0f, 80.0f);
                
                auto base_time = std::chrono::system_clock::now();
                
                for (int i = 0; i < WRITES_PER_THREAD; ++i) {
                    SensorData reading;
                    reading.timestamp = base_time + std::chrono::microseconds(
                        t * 10000000 + i * 1000); // Spread timestamps across threads
                    reading.co2_ppm = co2_dist(gen);
                    reading.temperature_c = temp_dist(gen);
                    reading.humidity_percent = humidity_dist(gen);
                    reading.quality_flags = SensorData::CO2_VALID | 
                                           SensorData::TEMP_VALID | 
                                           SensorData::HUMIDITY_VALID;
                    
                    if (storage.store_reading(reading)) {
                        successful_writes++;
                    } else {
                        failed_writes++;
                    }
                    
                    // Periodically check storage health
                    if (i % 100 == 0 && !storage.is_healthy()) {
                        storage_healthy = false;
                        break;
                    }
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
    };
    
    auto usage = monitor_resources(test_function);
    
    // Verify results
    int total_attempts = successful_writes.load() + failed_writes.load();
    double success_rate = static_cast<double>(successful_writes.load()) / total_attempts * 100.0;
    
    EXPECT_GT(total_attempts, TOTAL_WRITES * 0.9) << "Should have attempted most writes";
    EXPECT_GT(success_rate, 90.0) << "Success rate should remain high under extreme load";
    EXPECT_TRUE(storage_healthy.load()) << "Storage should remain healthy under extreme load";
    
    // Final storage health check
    EXPECT_TRUE(storage.is_healthy()) << "Storage should be healthy after extreme load test";
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(usage.end_time - usage.start_time);
    double writes_per_second = (successful_writes.load() * 1000.0) / duration.count();
    
    LOG_INFO("Storage extreme load test completed", {
        {"successful_writes", std::to_string(successful_writes.load())},
        {"failed_writes", std::to_string(failed_writes.load())},
        {"success_rate_percent", std::to_string(success_rate)},
        {"writes_per_second", std::to_string(writes_per_second)},
        {"num_threads", std::to_string(NUM_THREADS)},
        {"duration_ms", std::to_string(duration.count())},
        {"max_memory_mb", std::to_string(usage.max_memory_bytes / (1024*1024))}
    });
}

// Test daemon restart resilience
TEST_F(StressTest, DaemonRestartResilienceTest) {
    const int NUM_RESTART_CYCLES = 5;
    std::vector<std::chrono::milliseconds> startup_times;
    
    for (int cycle = 0; cycle < NUM_RESTART_CYCLES; ++cycle) {
        DaemonCore daemon;
        ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        // Wait for daemon to start
        while (!daemon.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto startup_time = std::chrono::high_resolution_clock::now();
        startup_times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
            startup_time - start_time));
        
        // Let daemon run briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        
        // Verify daemon is still running
        EXPECT_TRUE(daemon.is_running()) << "Daemon should be running in cycle " << cycle;
        
        // Shutdown daemon
        daemon.shutdown();
        daemon_thread.join();
        
        EXPECT_FALSE(daemon.is_running()) << "Daemon should stop after shutdown in cycle " << cycle;
        
        // Brief pause between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Analyze startup performance
    auto avg_startup = std::accumulate(startup_times.begin(), startup_times.end(), 
                                      std::chrono::milliseconds(0)) / NUM_RESTART_CYCLES;
    auto max_startup = *std::max_element(startup_times.begin(), startup_times.end());
    
    // Startup should be reasonably fast
    EXPECT_LT(avg_startup.count(), 1000) << "Average startup should be less than 1 second";
    EXPECT_LT(max_startup.count(), 2000) << "Maximum startup should be less than 2 seconds";
    
    LOG_INFO("Daemon restart resilience test completed", {
        {"num_cycles", std::to_string(NUM_RESTART_CYCLES)},
        {"avg_startup_ms", std::to_string(avg_startup.count())},
        {"max_startup_ms", std::to_string(max_startup.count())}
    });
}

} // namespace test
} // namespace sensor_daemon