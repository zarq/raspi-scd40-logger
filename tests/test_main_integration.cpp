#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include "daemon_core.hpp"
#include "config_manager.hpp"
#include "logging_system.hpp"

namespace sensor_daemon {
namespace test {

class MainIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_main_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create test configuration file
        config_path_ = temp_dir_ / "main_test_config.toml";
        create_test_config();
        
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::DEBUG, "", 1024*1024, 3, true);
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
log_level = "debug"

[sensor]
i2c_device = "/dev/i2c-1"
i2c_address = 98
connection_timeout_ms = 200
max_retries = 1

[storage]
data_directory = ")" << (temp_dir_ / "data").string() << R"("
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 2
)";
        config_file.close();
    }
    
    std::filesystem::path temp_dir_;
    std::filesystem::path config_path_;
};

// Test main application initialization and component coordination
TEST_F(MainIntegrationTest, MainApplicationInitializationTest) {
    // Test configuration file access (similar to main.cpp logic)
    EXPECT_EQ(access(config_path_.string().c_str(), R_OK), 0) 
        << "Configuration file should be readable";
    
    // Test daemon initialization with valid configuration
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true))
        << "Daemon should initialize successfully with valid configuration";
    
    // Verify daemon is not running initially
    EXPECT_FALSE(daemon.is_running());
    
    // Test daemon startup and shutdown cycle
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    auto start_time = std::chrono::steady_clock::now();
    while (!daemon.is_running() && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(3)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_TRUE(daemon.is_running()) << "Daemon should be running after startup";
    
    // Let daemon run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    // Test graceful shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running()) << "Daemon should stop after shutdown";
    
    // Verify metrics were collected
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.get_uptime().count(), 0) << "Should have recorded uptime";
    
    LOG_INFO("Main application initialization test completed", {
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())},
        {"sensor_attempts", std::to_string(metrics.sensor_readings_success + metrics.sensor_readings_failed)}
    });
}

// Test error handling with invalid configuration (similar to main.cpp error paths)
TEST_F(MainIntegrationTest, InvalidConfigurationHandlingTest) {
    // Test with non-existent configuration file
    std::string invalid_config = "/nonexistent/path/config.toml";
    EXPECT_NE(access(invalid_config.c_str(), R_OK), 0) 
        << "Invalid config path should not be accessible";
    
    DaemonCore daemon1;
    EXPECT_FALSE(daemon1.initialize(invalid_config, true))
        << "Daemon should fail to initialize with invalid config path";
    
    // Test with malformed configuration file
    auto malformed_config_path = temp_dir_ / "malformed_config.toml";
    std::ofstream malformed_config(malformed_config_path);
    malformed_config << "invalid toml content [[[";
    malformed_config.close();
    
    DaemonCore daemon2;
    EXPECT_FALSE(daemon2.initialize(malformed_config_path.string(), true))
        << "Daemon should fail to initialize with malformed config";
    
    LOG_INFO("Invalid configuration handling test completed");
}

// Test component integration under error conditions
TEST_F(MainIntegrationTest, ComponentErrorIntegrationTest) {
    // Create configuration with problematic settings
    auto error_config_path = temp_dir_ / "error_config.toml";
    std::ofstream config_file(error_config_path);
    config_file << R"(
[daemon]
sampling_interval_seconds = 1
data_retention_days = 1
log_level = "debug"

[sensor]
i2c_device = "/dev/nonexistent"
i2c_address = 98
connection_timeout_ms = 100
max_retries = 1

[storage]
data_directory = ")" << (temp_dir_ / "data").string() << R"("
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 2
)";
    config_file.close();
    
    DaemonCore daemon;
    
    // Daemon should still initialize even with sensor errors
    ASSERT_TRUE(daemon.initialize(error_config_path.string(), true))
        << "Daemon should initialize despite sensor connection issues";
    
    // Run daemon briefly to test error handling
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_TRUE(daemon.is_running()) << "Daemon should continue running despite sensor errors";
    
    // Let daemon attempt several sensor cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Check that errors were handled gracefully
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.sensor_readings_failed, 0) 
        << "Should have recorded sensor reading failures";
    
    // Daemon should still be running
    EXPECT_TRUE(daemon.is_running()) << "Daemon should remain running despite errors";
    
    daemon.shutdown();
    daemon_thread.join();
    
    LOG_INFO("Component error integration test completed", {
        {"sensor_failures", std::to_string(metrics.sensor_readings_failed)},
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())}
    });
}

// Test complete application lifecycle with metrics collection
TEST_F(MainIntegrationTest, ApplicationLifecycleTest) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    // Record initial state
    auto initial_metrics = daemon.get_metrics();
    EXPECT_EQ(initial_metrics.sensor_readings_success, 0);
    EXPECT_EQ(initial_metrics.sensor_readings_failed, 0);
    
    // Start daemon
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for startup
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(daemon.is_running());
    
    // Let daemon run for multiple cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    // Check metrics progression
    auto running_metrics = daemon.get_metrics();
    EXPECT_GT(running_metrics.get_uptime().count(), 2);
    
    uint64_t total_attempts = running_metrics.sensor_readings_success + 
                             running_metrics.sensor_readings_failed;
    EXPECT_GT(total_attempts, 0) << "Should have attempted sensor operations";
    
    // Test shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    // Verify final state
    EXPECT_FALSE(daemon.is_running());
    auto final_metrics = daemon.get_metrics();
    EXPECT_GE(final_metrics.get_uptime().count(), running_metrics.get_uptime().count());
    
    LOG_INFO("Application lifecycle test completed", {
        {"final_uptime", std::to_string(final_metrics.get_uptime().count())},
        {"total_sensor_attempts", std::to_string(total_attempts)},
        {"sensor_success_rate", std::to_string(final_metrics.get_sensor_success_rate() * 100.0)}
    });
}

} // namespace test
} // namespace sensor_daemon