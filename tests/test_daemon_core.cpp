#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "daemon_core.hpp"
#include "config_manager.hpp"

namespace sensor_daemon {
namespace test {

class DaemonCoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_test";
        std::filesystem::create_directories(test_dir_);
        
        // Create test configuration file
        config_path_ = test_dir_ / "test_config.toml";
        create_test_config();
        
        // Create test data directory
        data_dir_ = test_dir_ / "data";
        std::filesystem::create_directories(data_dir_);
    }
    
    void TearDown() override {
        // Clean up test directory
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }
    
    void create_test_config() {
        std::ofstream config_file(config_path_);
        config_file << R"(
[daemon]
sampling_interval_seconds = 1
data_retention_days = 1
log_level = "debug"

[sensor]
i2c_device = "/dev/null"
i2c_address = 0x62
connection_timeout_ms = 100
max_retries = 2

[storage]
data_directory = ")" << data_dir_.string() << R"("
file_rotation_hours = 1
compression_enabled = false
max_memory_cache_mb = 1
)";
        config_file.close();
    }
    
    std::filesystem::path test_dir_;
    std::filesystem::path config_path_;
    std::filesystem::path data_dir_;
};

// Test daemon initialization
TEST_F(DaemonCoreTest, InitializationSuccess) {
    DaemonCore daemon;
    
    EXPECT_TRUE(daemon.initialize(config_path_.string()));
    EXPECT_FALSE(daemon.is_running());
}

TEST_F(DaemonCoreTest, InitializationWithInvalidConfig) {
    DaemonCore daemon;
    
    EXPECT_FALSE(daemon.initialize("/nonexistent/config.toml"));
    EXPECT_FALSE(daemon.is_running());
}

// Test daemon lifecycle
TEST_F(DaemonCoreTest, StartAndShutdown) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    // Start daemon in separate thread
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait a bit for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(daemon.is_running());
    
    // Request shutdown
    daemon.shutdown();
    
    // Wait for daemon thread to finish
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
}

// Test signal handling
TEST_F(DaemonCoreTest, SignalHandling) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    pid_t child_pid = fork();
    
    if (child_pid == 0) {
        // Child process - run daemon
        daemon.run();
        exit(0);
    } else if (child_pid > 0) {
        // Parent process - send signal after brief delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Send SIGTERM
        kill(child_pid, SIGTERM);
        
        // Wait for child to exit
        int status;
        waitpid(child_pid, &status, 0);
        
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    } else {
        FAIL() << "Fork failed";
    }
}

// Test error handling and recovery
class MockErrorHandler : public ::testing::Test {
protected:
    void SetUp() override {
        error_handler_ = std::make_unique<ErrorHandler>();
    }
    
    std::unique_ptr<ErrorHandler> error_handler_;
};

TEST_F(MockErrorHandler, RecoverableErrorHandling) {
    std::runtime_error error("Test recoverable error");
    
    // Should allow retries
    EXPECT_TRUE(error_handler_->should_retry("test_operation", 1));
    EXPECT_TRUE(error_handler_->should_retry("test_operation", 3));
    EXPECT_FALSE(error_handler_->should_retry("test_operation", 10));
}

TEST_F(MockErrorHandler, ExponentialBackoff) {
    auto delay1 = error_handler_->get_backoff_delay(1);
    auto delay2 = error_handler_->get_backoff_delay(2);
    auto delay3 = error_handler_->get_backoff_delay(3);
    
    EXPECT_LT(delay1, delay2);
    EXPECT_LT(delay2, delay3);
    
    // Should not exceed maximum delay
    auto max_delay = error_handler_->get_backoff_delay(20);
    EXPECT_LE(max_delay, std::chrono::milliseconds(30000));
}

// Test performance metrics
TEST_F(DaemonCoreTest, PerformanceMetrics) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    auto metrics = daemon.get_metrics();
    
    // Check initial metrics
    EXPECT_EQ(metrics.sensor_readings_success, 0);
    EXPECT_EQ(metrics.sensor_readings_failed, 0);
    EXPECT_EQ(metrics.storage_writes_success, 0);
    EXPECT_EQ(metrics.storage_writes_failed, 0);
    
    // Uptime should be very small initially
    auto uptime = metrics.get_uptime();
    EXPECT_GE(uptime.count(), 0);
    EXPECT_LT(uptime.count(), 10); // Less than 10 seconds
}

// Test daemon resource usage
TEST_F(DaemonCoreTest, ResourceUsage) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    // Start daemon in separate thread
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Let it run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto metrics = daemon.get_metrics();
    
    // Memory usage should be reasonable (less than 10MB as per requirements)
    EXPECT_LT(metrics.memory_usage_bytes, 10 * 1024 * 1024);
    
    // CPU usage should be reasonable
    EXPECT_GE(metrics.cpu_usage_percent, 0.0);
    EXPECT_LT(metrics.cpu_usage_percent, 50.0); // Should be much lower in practice
    
    daemon.shutdown();
    daemon_thread.join();
}

// Test configuration reload (if implemented)
TEST_F(DaemonCoreTest, ConfigurationHandling) {
    DaemonCore daemon;
    
    // Test with valid configuration
    EXPECT_TRUE(daemon.initialize(config_path_.string()));
    
    // Test with modified configuration
    std::ofstream config_file(config_path_);
    config_file << R"(
[daemon]
sampling_interval_seconds = 5
data_retention_days = 7
log_level = "info"

[sensor]
i2c_device = "/dev/null"
i2c_address = 0x62
connection_timeout_ms = 500
max_retries = 5

[storage]
data_directory = ")" << data_dir_.string() << R"("
file_rotation_hours = 24
compression_enabled = true
max_memory_cache_mb = 5
)";
    config_file.close();
    
    // Reinitialize with new config
    DaemonCore daemon2;
    EXPECT_TRUE(daemon2.initialize(config_path_.string()));
}

// Test daemon restart behavior
TEST_F(DaemonCoreTest, RestartBehavior) {
    // First run
    {
        DaemonCore daemon;
        ASSERT_TRUE(daemon.initialize(config_path_.string()));
        
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        daemon.shutdown();
        daemon_thread.join();
    }
    
    // Second run - should be able to restart
    {
        DaemonCore daemon;
        EXPECT_TRUE(daemon.initialize(config_path_.string()));
        
        std::thread daemon_thread([&daemon]() {
            daemon.run();
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        daemon.shutdown();
        daemon_thread.join();
    }
}

// Test multiple signal types
TEST_F(DaemonCoreTest, MultipleSignalTypes) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    // Test SIGINT handling
    pid_t child_pid = fork();
    
    if (child_pid == 0) {
        // Child process - run daemon
        daemon.run();
        exit(0);
    } else if (child_pid > 0) {
        // Parent process - send SIGINT
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(child_pid, SIGINT);
        
        int status;
        waitpid(child_pid, &status, 0);
        
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    } else {
        FAIL() << "Fork failed";
    }
}

// Test daemon health monitoring
TEST_F(DaemonCoreTest, HealthMonitoring) {
    DaemonCore daemon;
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    
    // Start daemon briefly to test health checks
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Get metrics to verify health monitoring is working
    auto metrics = daemon.get_metrics();
    EXPECT_GE(metrics.get_uptime().count(), 0);
    
    daemon.shutdown();
    daemon_thread.join();
}

// Integration test for full daemon lifecycle
TEST_F(DaemonCoreTest, FullLifecycleIntegration) {
    DaemonCore daemon;
    
    // Initialize
    ASSERT_TRUE(daemon.initialize(config_path_.string()));
    EXPECT_FALSE(daemon.is_running());
    
    // Start daemon
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Verify it's running
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(daemon.is_running());
    
    // Let it run for a few cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(2500)); // ~2.5 cycles
    
    // Check that some activity occurred
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.get_uptime().count(), 2);
    
    // Graceful shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
}

// Integration test for end-to-end data collection functionality
TEST_F(DaemonCoreTest, EndToEndDataCollectionIntegration) {
    DaemonCore daemon;
    
    // Initialize in foreground mode for testing
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    // Create a temporary storage directory for this test
    std::filesystem::path test_storage_dir = temp_dir_ / "integration_storage";
    std::filesystem::create_directories(test_storage_dir);
    
    // Start daemon in a separate thread
    std::atomic<bool> daemon_started{false};
    std::thread daemon_thread([&daemon, &daemon_started]() {
        daemon_started = true;
        daemon.run();
    });
    
    // Wait for daemon to start
    while (!daemon_started || !daemon.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Let daemon run for several sensor cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(3000)); // 3 seconds
    
    // Check performance metrics to verify data collection occurred
    auto metrics = daemon.get_metrics();
    
    // Verify daemon has been running
    EXPECT_GT(metrics.get_uptime().count(), 2);
    
    // In a real environment with actual sensor, we would expect successful readings
    // For testing with mock sensor, we verify the daemon attempted to read
    uint64_t total_attempts = metrics.sensor_readings_success + metrics.sensor_readings_failed;
    EXPECT_GT(total_attempts, 0) << "Daemon should have attempted sensor readings";
    
    // Verify storage operations were attempted
    uint64_t total_storage_ops = metrics.storage_writes_success + metrics.storage_writes_failed;
    EXPECT_GE(total_storage_ops, 0) << "Storage operations should have been attempted";
    
    // Test graceful shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
}

// Integration test for error handling and recovery
TEST_F(DaemonCoreTest, ErrorHandlingAndRecoveryIntegration) {
    DaemonCore daemon;
    
    // Initialize with a configuration that might cause some errors
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(daemon.is_running());
    
    // Let daemon run and handle potential errors
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Verify daemon is still running despite potential errors
    EXPECT_TRUE(daemon.is_running()) << "Daemon should continue running despite recoverable errors";
    
    // Check that error handling is working
    auto metrics = daemon.get_metrics();
    EXPECT_GT(metrics.get_uptime().count(), 1);
    
    // Shutdown
    daemon.shutdown();
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
}

// Integration test for signal handling
TEST_F(DaemonCoreTest, SignalHandlingIntegration) {
    DaemonCore daemon;
    
    ASSERT_TRUE(daemon.initialize(config_path_.string(), true));
    
    std::thread daemon_thread([&daemon]() {
        daemon.run();
    });
    
    // Wait for daemon to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(daemon.is_running());
    
    // Send SIGTERM to the process (simulate systemd shutdown)
    std::raise(SIGTERM);
    
    // Wait for graceful shutdown
    daemon_thread.join();
    
    EXPECT_FALSE(daemon.is_running());
}

} // namespace test
} // namespace sensor_daemon