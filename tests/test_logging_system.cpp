#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "logging_system.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace sensor_daemon;

class LoggingSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test logs
        test_log_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_test_logs";
        std::filesystem::create_directories(test_log_dir_);
        
        test_log_file_ = test_log_dir_ / "test.log";
        
        // Ensure logging system is shutdown before each test
        LoggingSystem::shutdown();
    }
    
    void TearDown() override {
        // Clean up logging system and test files
        LoggingSystem::shutdown();
        
        if (std::filesystem::exists(test_log_dir_)) {
            std::filesystem::remove_all(test_log_dir_);
        }
    }
    
    std::string read_log_file() {
        if (!std::filesystem::exists(test_log_file_)) {
            return "";
        }
        
        std::ifstream file(test_log_file_);
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return content;
    }
    
    std::filesystem::path test_log_dir_;
    std::filesystem::path test_log_file_;
};

// Test basic initialization and shutdown
TEST_F(LoggingSystemTest, InitializationAndShutdown) {
    EXPECT_FALSE(LoggingSystem::is_initialized());
    
    // Test console-only initialization
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::INFO, "", 0, 0, true));
    EXPECT_TRUE(LoggingSystem::is_initialized());
    EXPECT_EQ(LoggingSystem::get_log_level(), LogLevel::INFO);
    
    LoggingSystem::shutdown();
    EXPECT_FALSE(LoggingSystem::is_initialized());
}

// Test file logging initialization
TEST_F(LoggingSystemTest, FileLoggingInitialization) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string(), 1024, 3, false));
    EXPECT_TRUE(LoggingSystem::is_initialized());
    
    // Log a test message
    LoggingSystem::info("Test message");
    LoggingSystem::shutdown();
    
    // Check that log file was created and contains the message
    EXPECT_TRUE(std::filesystem::exists(test_log_file_));
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("Test message"));
}

// Test log level conversion
TEST_F(LoggingSystemTest, LogLevelConversion) {
    EXPECT_EQ(LoggingSystem::string_to_log_level("trace"), LogLevel::TRACE);
    EXPECT_EQ(LoggingSystem::string_to_log_level("debug"), LogLevel::DEBUG);
    EXPECT_EQ(LoggingSystem::string_to_log_level("info"), LogLevel::INFO);
    EXPECT_EQ(LoggingSystem::string_to_log_level("warn"), LogLevel::WARN);
    EXPECT_EQ(LoggingSystem::string_to_log_level("warning"), LogLevel::WARN);
    EXPECT_EQ(LoggingSystem::string_to_log_level("error"), LogLevel::ERROR);
    EXPECT_EQ(LoggingSystem::string_to_log_level("critical"), LogLevel::CRITICAL);
    EXPECT_EQ(LoggingSystem::string_to_log_level("invalid"), LogLevel::INFO); // Default fallback
    
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::TRACE), "trace");
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::DEBUG), "debug");
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::INFO), "info");
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::WARN), "warn");
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::ERROR), "error");
    EXPECT_EQ(LoggingSystem::log_level_to_string(LogLevel::CRITICAL), "critical");
}

// Test structured logging with context
TEST_F(LoggingSystemTest, StructuredLogging) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    // Test logging with context
    std::unordered_map<std::string, std::string> context = {
        {"component", "test"},
        {"operation", "unit_test"},
        {"value", "123"}
    };
    
    LoggingSystem::info("Test message with context", context);
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("Test message with context"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("component=test"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("operation=unit_test"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("value=123"));
}

// Test daemon lifecycle logging
TEST_F(LoggingSystemTest, DaemonLifecycleLogging) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::INFO, test_log_file_.string()));
    
    LoggingSystem::log_daemon_startup("1.0.0", "/etc/config.toml");
    LoggingSystem::log_daemon_shutdown("user_request");
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("daemon:startup"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("version=1.0.0"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("config_path=/etc/config.toml"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("daemon:shutdown"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("reason=user_request"));
}

// Test sensor logging
TEST_F(LoggingSystemTest, SensorLogging) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    LoggingSystem::log_sensor_reading(true, "CO2: 400ppm");
    LoggingSystem::log_sensor_reading(false, "I2C timeout");
    LoggingSystem::log_sensor_connection(true, "/dev/i2c-1");
    LoggingSystem::log_sensor_connection(false, "/dev/i2c-1");
    
    ErrorContext error_ctx("sensor", "read_data", "I2C_TIMEOUT");
    error_ctx.add_data("device", "/dev/i2c-1").add_data("attempt", "3");
    LoggingSystem::log_sensor_error("Failed to read sensor data", error_ctx);
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("sensor:reading"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("CO2: 400ppm"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("I2C timeout"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("sensor:connection"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("sensor:error"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("I2C_TIMEOUT"));
}

// Test storage logging
TEST_F(LoggingSystemTest, StorageLogging) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    LoggingSystem::log_storage_write(true, "1 record");
    LoggingSystem::log_storage_write(false, "disk full");
    
    ErrorContext error_ctx("storage", "write_data", "DISK_FULL");
    error_ctx.add_data("path", "/var/lib/sensor-daemon").add_data("size", "1024");
    LoggingSystem::log_storage_error("Failed to write data", error_ctx);
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("storage:write"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("1 record"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("disk full"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("storage:error"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("DISK_FULL"));
}

// Test performance metrics logging
TEST_F(LoggingSystemTest, PerformanceMetricsLogging) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::INFO, test_log_file_.string()));
    
    PerformanceMetrics metrics;
    metrics.start_time = std::chrono::steady_clock::now() - std::chrono::seconds(3600); // 1 hour ago
    metrics.sensor_readings_success = 1000;
    metrics.sensor_readings_failed = 10;
    metrics.storage_writes_success = 995;
    metrics.storage_writes_failed = 5;
    metrics.i2c_connection_failures = 2;
    metrics.memory_usage_bytes = 8 * 1024 * 1024; // 8MB
    metrics.cpu_usage_percent = 2.5;
    
    LoggingSystem::log_performance_metrics(metrics);
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("performance:metrics"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("uptime_seconds"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("sensor_success_rate"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("storage_success_rate"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("memory_usage_mb=8.00"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("cpu_usage_percent=2.50"));
}

// Test error context functionality
TEST_F(LoggingSystemTest, ErrorContext) {
    ErrorContext ctx("test_component", "test_operation", "TEST_ERROR");
    ctx.add_data("key1", "value1").add_data("key2", "value2");
    
    EXPECT_EQ(ctx.component, "test_component");
    EXPECT_EQ(ctx.operation, "test_operation");
    EXPECT_EQ(ctx.error_code, "TEST_ERROR");
    EXPECT_EQ(ctx.additional_data["key1"], "value1");
    EXPECT_EQ(ctx.additional_data["key2"], "value2");
}

// Test performance metrics calculations
TEST_F(LoggingSystemTest, PerformanceMetricsCalculations) {
    PerformanceMetrics metrics;
    metrics.start_time = std::chrono::steady_clock::now() - std::chrono::seconds(100);
    metrics.sensor_readings_success = 80;
    metrics.sensor_readings_failed = 20;
    metrics.storage_writes_success = 90;
    metrics.storage_writes_failed = 10;
    
    // Test uptime calculation (should be around 100 seconds)
    auto uptime = metrics.get_uptime();
    EXPECT_GE(uptime.count(), 99);
    EXPECT_LE(uptime.count(), 101);
    
    // Test success rate calculations
    EXPECT_DOUBLE_EQ(metrics.get_sensor_success_rate(), 0.8);
    EXPECT_DOUBLE_EQ(metrics.get_storage_success_rate(), 0.9);
    
    // Test with zero totals
    PerformanceMetrics empty_metrics;
    EXPECT_DOUBLE_EQ(empty_metrics.get_sensor_success_rate(), 0.0);
    EXPECT_DOUBLE_EQ(empty_metrics.get_storage_success_rate(), 0.0);
}

// Test performance timer
TEST_F(LoggingSystemTest, PerformanceTimer) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    {
        PerformanceTimer timer("test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // Timer destructor should log timing
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("performance:timer"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("test_operation"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("duration_us"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("duration_ms"));
}

// Test log level filtering
TEST_F(LoggingSystemTest, LogLevelFiltering) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::WARN, test_log_file_.string()));
    
    LoggingSystem::trace("Trace message");
    LoggingSystem::debug("Debug message");
    LoggingSystem::info("Info message");
    LoggingSystem::warn("Warning message");
    LoggingSystem::error("Error message");
    LoggingSystem::critical("Critical message");
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    
    // Only WARN, ERROR, and CRITICAL should be logged
    EXPECT_THAT(log_content, ::testing::Not(::testing::HasSubstr("Trace message")));
    EXPECT_THAT(log_content, ::testing::Not(::testing::HasSubstr("Debug message")));
    EXPECT_THAT(log_content, ::testing::Not(::testing::HasSubstr("Info message")));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Warning message"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Error message"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Critical message"));
}

// Test log rotation (basic test - actual rotation requires larger files)
TEST_F(LoggingSystemTest, LogRotationSetup) {
    // Test that we can initialize with rotation parameters
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::INFO, test_log_file_.string(), 1024, 5));
    
    // Write some log messages
    for (int i = 0; i < 10; ++i) {
        LoggingSystem::info("Log message " + std::to_string(i));
    }
    
    LoggingSystem::shutdown();
    
    // Verify log file exists
    EXPECT_TRUE(std::filesystem::exists(test_log_file_));
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("Log message 0"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Log message 9"));
}

// Test logging without initialization (should not crash)
TEST_F(LoggingSystemTest, LoggingWithoutInitialization) {
    EXPECT_FALSE(LoggingSystem::is_initialized());
    
    // These should not crash even though logging is not initialized
    LoggingSystem::info("Test message");
    LoggingSystem::error("Error message");
    LoggingSystem::log_daemon_startup("1.0.0", "/config");
    
    PerformanceMetrics metrics;
    LoggingSystem::log_performance_metrics(metrics);
    
    // No assertions needed - just ensuring no crashes
}

// Test macro convenience functions
TEST_F(LoggingSystemTest, LoggingMacros) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    LOG_TRACE("Trace via macro");
    LOG_DEBUG("Debug via macro");
    LOG_INFO("Info via macro");
    LOG_WARN("Warning via macro");
    LOG_ERROR("Error via macro");
    LOG_CRITICAL("Critical via macro");
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("Trace via macro"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Debug via macro"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Info via macro"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Warning via macro"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Error via macro"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("Critical via macro"));
}

// Test PERF_TIMER macro
TEST_F(LoggingSystemTest, PerfTimerMacro) {
    EXPECT_TRUE(LoggingSystem::initialize(LogLevel::DEBUG, test_log_file_.string()));
    
    {
        PERF_TIMER("macro_test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    LoggingSystem::shutdown();
    
    std::string log_content = read_log_file();
    EXPECT_THAT(log_content, ::testing::HasSubstr("performance:timer"));
    EXPECT_THAT(log_content, ::testing::HasSubstr("macro_test_operation"));
}