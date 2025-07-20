#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include "daemon_core.hpp"
#include "config_manager.hpp"
#include "logging_system.hpp"
#include "scd40_interface.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

namespace sensor_daemon {
namespace test {

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_integration_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create test configuration file
        config_path_ = temp_dir_ / "test_config.toml";
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
connection_timeout_ms = 500
max_retries = 2

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

// Test complete component integration
TEST_F(IntegrationTest, ComponentIntegrationTest) {
    // Test individual component initialization
    auto config = ConfigManager::load_config(config_path_.string());
    
    // Test storage initialization
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(config.storage.data_directory, config.daemon.data_retention));
    EXPECT_TRUE(storage.is_healthy());
    
    // Test sensor interface initialization (will fail without actual hardware, but should handle gracefully)
    SCD40Interface sensor(config.sensor);
    // Note: sensor.initialize() will likely fail in test environment without actual I2C hardware
    // This is expected and tests error handling
    
    // Test data flow: create a mock sensor reading and store it
    SensorData test_reading;
    test_reading.timestamp = std::chrono::system_clock::now();
    test_reading.co2_ppm = 450.0f;
    test_reading.temperature_c = 22.5f;
    test_reading.humidity_percent = 45.0f;
    test_reading.quality_flags = SensorData::CO2_VALID | 
                                SensorData::TEMP_VALID | 
                                SensorData::HUMIDITY_VALID;
    
    EXPECT_TRUE(storage.store_reading(test_reading));
    
    // Verify storage statistics
    std::string stats = storage.get_statistics();
    EXPECT_FALSE(stats.empty());
    
    LOG_INFO("Integration test completed successfully", {
        {"storage_stats", stats},
        {"db_size", std::to_string(storage.get_database_size())}
    });
}

// Test error handling coordination between components
TEST_F(IntegrationTest, ErrorHandlingCoordinationTest) {
    auto config = ConfigManager::load_config(config_path_.string());
    
    // Test storage with invalid directory (should handle gracefully)
    TimeSeriesStorage storage;
    std::string invalid_path = "/invalid/nonexistent/path";
    EXPECT_FALSE(storage.initialize(invalid_path, config.daemon.data_retention));
    EXPECT_FALSE(storage.is_healthy());
    
    // Test sensor with invalid I2C device (should handle gracefully)
    auto sensor_config = config.sensor;
    sensor_config.i2c_device = "/dev/nonexistent_i2c";
    SCD40Interface sensor(sensor_config);
    EXPECT_FALSE(sensor.initialize());
    EXPECT_FALSE(sensor.is_connected());
    
    // Verify error messages are available
    std::string last_error = sensor.get_last_error();
    EXPECT_FALSE(last_error.empty());
    
    LOG_INFO("Error handling coordination test completed", {
        {"sensor_error", last_error}
    });
}

// Test configuration validation and error handling
TEST_F(IntegrationTest, ConfigurationValidationTest) {
    // Test with invalid configuration
    std::filesystem::path invalid_config_path = temp_dir_ / "invalid_config.toml";
    std::ofstream invalid_config(invalid_config_path);
    invalid_config << R"(
[daemon]
sampling_interval_seconds = -1
log_level = "invalid_level"

[sensor]
i2c_address = 300
max_retries = -5

[storage]
data_directory = ""
max_memory_cache_mb = -1
)";
    invalid_config.close();
    
    // Should throw ConfigurationError
    EXPECT_THROW(ConfigManager::load_config(invalid_config_path.string()), ConfigurationError);
    
    // Test with missing configuration file
    EXPECT_THROW(ConfigManager::load_config("/nonexistent/config.toml"), ConfigurationError);
    
    LOG_INFO("Configuration validation test completed");
}

// Test data serialization and storage integrity
TEST_F(IntegrationTest, DataIntegrityTest) {
    auto config = ConfigManager::load_config(config_path_.string());
    
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(config.storage.data_directory, config.daemon.data_retention));
    
    // Create multiple test readings with different data patterns
    std::vector<SensorData> test_readings;
    
    // Reading with all values
    SensorData reading1;
    reading1.timestamp = std::chrono::system_clock::now();
    reading1.co2_ppm = 400.0f;
    reading1.temperature_c = 20.0f;
    reading1.humidity_percent = 50.0f;
    reading1.quality_flags = SensorData::CO2_VALID | 
                            SensorData::TEMP_VALID | 
                            SensorData::HUMIDITY_VALID;
    test_readings.push_back(reading1);
    
    // Reading with missing CO2
    SensorData reading2;
    reading2.timestamp = std::chrono::system_clock::now() + std::chrono::seconds(1);
    reading2.temperature_c = 21.0f;
    reading2.humidity_percent = 51.0f;
    reading2.quality_flags = SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
    test_readings.push_back(reading2);
    
    // Reading with only CO2
    SensorData reading3;
    reading3.timestamp = std::chrono::system_clock::now() + std::chrono::seconds(2);
    reading3.co2_ppm = 450.0f;
    reading3.quality_flags = SensorData::CO2_VALID;
    test_readings.push_back(reading3);
    
    // Store all readings
    for (const auto& reading : test_readings) {
        EXPECT_TRUE(storage.store_reading(reading)) << "Failed to store reading";
    }
    
    // Verify storage health after multiple writes
    EXPECT_TRUE(storage.is_healthy());
    EXPECT_GT(storage.get_database_size(), 0);
    
    LOG_INFO("Data integrity test completed", {
        {"readings_stored", std::to_string(test_readings.size())},
        {"db_size", std::to_string(storage.get_database_size())}
    });
}

// Test daemon lifecycle with real component coordination
TEST_F(IntegrationTest, DaemonLifecycleIntegrationTest) {
    DaemonCore daemon;
    
    // Initialize daemon in foreground mode for testing
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    // Start daemon in separate thread
    std::atomic<bool> daemon_running{false};
    std::thread daemon_thread([&daemon, &daemon_running]() {
        daemon_running = true;
        daemon.run();
        daemon_running = false;
    });
    
    // Wait for daemon to start
    auto start_time = std::chrono::steady_clock::now();
    while (!daemon.is_running() && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    ASSERT_TRUE(daemon.is_running()) << "Daemon failed to start within timeout";
    
    // Let daemon run for several cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    // Check metrics to verify daemon activity
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.get_uptime().count(), 2);
    
    // Verify daemon attempted sensor operations (may fail without hardware)
    uint64_t total_sensor_attempts = metrics.sensor_readings_success + metrics.sensor_readings_failed;
    EXPECT_GT(total_sensor_attempts, 0) << "Daemon should have attempted sensor readings";
    
    // Test graceful shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
    EXPECT_FALSE(daemon_running);
    
    LOG_INFO("Daemon lifecycle integration test completed", {
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())},
        {"sensor_attempts", std::to_string(total_sensor_attempts)},
        {"storage_attempts", std::to_string(metrics.storage_writes_success + metrics.storage_writes_failed)}
    });
}

// Test complete end-to-end data flow integration
TEST_F(IntegrationTest, EndToEndDataFlowTest) {
    auto config = ConfigManager::load_config(config_path_.string());
    
    // Initialize components individually to test integration
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(config.storage.data_directory, config.daemon.data_retention));
    
    SCD40Interface sensor(config.sensor);
    // Sensor initialization may fail without hardware - that's expected
    
    // Test data flow with mock sensor readings
    std::vector<SensorData> test_readings;
    auto base_time = std::chrono::system_clock::now();
    
    for (int i = 0; i < 5; ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + i * 10.0f;
        reading.temperature_c = 20.0f + i * 0.5f;
        reading.humidity_percent = 50.0f + i * 2.0f;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        test_readings.push_back(reading);
    }
    
    // Store all readings
    for (const auto& reading : test_readings) {
        EXPECT_TRUE(storage.store_reading(reading)) << "Failed to store reading";
    }
    
    // Verify storage health and statistics
    EXPECT_TRUE(storage.is_healthy());
    EXPECT_GT(storage.get_database_size(), 0);
    
    std::string stats = storage.get_statistics();
    EXPECT_FALSE(stats.empty());
    
    LOG_INFO("End-to-end data flow test completed", {
        {"readings_stored", std::to_string(test_readings.size())},
        {"db_size", std::to_string(storage.get_database_size())},
        {"storage_stats", stats}
    });
}

// Test error recovery and component coordination
TEST_F(IntegrationTest, ErrorRecoveryCoordinationTest) {
    DaemonCore daemon;
    
    // Create configuration with invalid sensor device to test error handling
    auto invalid_config_path = temp_dir_ / "invalid_sensor_config.toml";
    std::ofstream config_file(invalid_config_path);
    config_file << R"(
[daemon]
sampling_interval_seconds = 1
data_retention_days = 1
log_level = "debug"

[sensor]
i2c_device = "/dev/nonexistent_i2c"
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
    
    // Initialize daemon with invalid sensor config
    ASSERT_TRUE(daemon.initialize(invalid_config_path.string(), true));
    
    // Run daemon briefly to test error handling
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start and attempt sensor operations
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Check that daemon is still running despite sensor errors
    EXPECT_TRUE(daemon.is_running());
    
    // Get metrics to verify error handling
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.sensor_readings_failed, 0) << "Should have failed sensor readings due to invalid device";
    
    // Shutdown daemon
    daemon.shutdown();
    daemon_thread.join();
    
    LOG_INFO("Error recovery coordination test completed", {
        {"sensor_failures", std::to_string(metrics.sensor_readings_failed)},
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())}
    });
}

// Test command-line argument parsing and main application entry point
TEST_F(IntegrationTest, MainApplicationIntegrationTest) {
    // Test configuration file validation
    std::string valid_config = config_path_.string();
    std::string invalid_config = "/nonexistent/config.toml";
    
    // Test access function used by main
    EXPECT_EQ(access(valid_config.c_str(), R_OK), 0) << "Valid config should be readable";
    EXPECT_NE(access(invalid_config.c_str(), R_OK), 0) << "Invalid config should not be readable";
    
    // Test daemon initialization with valid config
    DaemonCore daemon1;
    EXPECT_TRUE(daemon1.initialize(valid_config, true)) << "Should initialize with valid config";
    
    // Test daemon initialization with invalid config
    DaemonCore daemon2;
    EXPECT_FALSE(daemon2.initialize(invalid_config, true)) << "Should fail with invalid config";
    
    LOG_INFO("Main application integration test completed");
}

// Test comprehensive component error handling and recovery
TEST_F(IntegrationTest, ComprehensiveErrorHandlingTest) {
    auto config = ConfigManager::load_config(config_path_.string());
    
    // Test storage error handling
    {
        TimeSeriesStorage storage;
        // Try to initialize with read-only directory
        std::string readonly_path = "/tmp/readonly_test_dir";
        system(("mkdir -p " + readonly_path).c_str());
        system(("chmod 444 " + readonly_path).c_str());
        
        EXPECT_FALSE(storage.initialize(readonly_path, config.daemon.data_retention));
        EXPECT_FALSE(storage.is_healthy());
        
        // Cleanup
        system(("chmod 755 " + readonly_path).c_str());
        system(("rmdir " + readonly_path).c_str());
    }
    
    // Test sensor error handling with various invalid configurations
    {
        auto sensor_config = config.sensor;
        
        // Test with invalid I2C address
        sensor_config.i2c_address = 0;
        SCD40Interface sensor1(sensor_config);
        EXPECT_FALSE(sensor1.initialize());
        EXPECT_FALSE(sensor1.is_connected());
        
        // Test with invalid device path
        sensor_config.i2c_address = 98;
        sensor_config.i2c_device = "/dev/null";
        SCD40Interface sensor2(sensor_config);
        EXPECT_FALSE(sensor2.initialize());
        EXPECT_FALSE(sensor2.is_connected());
    }
    
    LOG_INFO("Comprehensive error handling test completed");
}

} // namespace test
} // namespace sensor_daemon