#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "health_monitor.hpp"
#include "logging_system.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace sensor_daemon;
using namespace testing;

class HealthMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::DEBUG, "", 1024*1024, 1, false);
        
        // Create test directory
        test_dir_ = std::filesystem::temp_directory_path() / "health_monitor_test";
        std::filesystem::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
        
        LoggingSystem::shutdown();
    }
    
    std::filesystem::path test_dir_;
};

TEST_F(HealthMonitorTest, InitializationAndShutdown) {
    HealthMonitor monitor;
    
    AlertConfig config;
    config.enabled = true;
    config.check_interval = std::chrono::minutes(1);
    
    EXPECT_TRUE(monitor.initialize(config));
    
    // Should be able to get health status even without checks
    SystemHealthStatus status = monitor.get_last_health_status();
    EXPECT_EQ(status.overall_status, HealthStatus::HEALTHY);
    
    monitor.shutdown();
}

TEST_F(HealthMonitorTest, MetricsCollector) {
    MetricsCollector collector;
    
    // Test sensor reading recording
    collector.record_sensor_reading(true, std::chrono::milliseconds(50));
    collector.record_sensor_reading(false, std::chrono::milliseconds(100));
    collector.record_sensor_reading(true, std::chrono::milliseconds(75));
    
    // Test success rate calculation
    double success_rate = collector.get_sensor_success_rate(std::chrono::minutes(60));
    EXPECT_NEAR(success_rate, 2.0/3.0, 0.01); // 2 out of 3 successful
    
    // Test average duration calculation
    auto avg_duration = collector.get_avg_sensor_duration(std::chrono::minutes(60));
    EXPECT_EQ(avg_duration.count(), 62); // (50 + 75) / 2 = 62.5, rounded down
    
    // Test storage operations
    collector.record_storage_operation(true, std::chrono::milliseconds(10));
    collector.record_storage_operation(true, std::chrono::milliseconds(15));
    
    double storage_rate = collector.get_storage_success_rate(std::chrono::minutes(60));
    EXPECT_EQ(storage_rate, 1.0); // 100% success rate
    
    // Test I2C events
    collector.record_i2c_event(true);
    collector.record_i2c_event(false, 5); // Error code 5
    collector.record_i2c_event(false, 5); // Same error code
    
    auto error_stats = collector.get_i2c_error_stats(std::chrono::minutes(60));
    EXPECT_EQ(error_stats[5], 2); // Two occurrences of error code 5
    
    // Test metrics report generation
    std::string report = collector.get_metrics_report();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("sensor_metrics"), std::string::npos);
    EXPECT_NE(report.find("storage_metrics"), std::string::npos);
    EXPECT_NE(report.find("i2c_metrics"), std::string::npos);
}

TEST_F(HealthMonitorTest, HealthEventRecording) {
    HealthMonitor monitor;
    
    AlertConfig config;
    config.enabled = true;
    
    EXPECT_TRUE(monitor.initialize(config));
    
    // Record some health events
    monitor.record_health_event("test_component", "test_event", "Test message 1");
    monitor.record_health_event("test_component", "error", "Test error message");
    monitor.record_health_event("another_component", "info", "Test info message");
    
    // Get diagnostic report which should include health events
    std::string diagnostic_report = monitor.get_diagnostic_report();
    
    EXPECT_NE(diagnostic_report.find("Recent Health Events"), std::string::npos);
    EXPECT_NE(diagnostic_report.find("test_component"), std::string::npos);
    EXPECT_NE(diagnostic_report.find("Test message 1"), std::string::npos);
    
    monitor.shutdown();
}

TEST_F(HealthMonitorTest, AlertingWithEnhancedContext) {
    HealthMonitor monitor;
    
    AlertConfig config;
    config.enabled = true;
    config.max_memory_mb = 10.0; // Low threshold for testing
    config.alert_cooldown = std::chrono::minutes(1);
    
    EXPECT_TRUE(monitor.initialize(config));
    
    // Register a mock health check that will trigger an alert
    monitor.register_health_check("test_memory", []() {
        HealthCheckResult result("test_memory", HealthStatus::CRITICAL, "Memory usage too high");
        result.add_detail("current_mb", "15.0")
              .add_detail("threshold_mb", "10.0");
        return result;
    });
    
    // Perform health check which should trigger alert
    SystemHealthStatus status = monitor.check_system_health();
    
    EXPECT_EQ(status.overall_status, HealthStatus::CRITICAL);
    EXPECT_FALSE(status.component_results.empty());
    
    // Check that the health event was recorded
    std::string diagnostic_report = monitor.get_diagnostic_report();
    EXPECT_NE(diagnostic_report.find("alert"), std::string::npos);
    
    monitor.shutdown();
}

TEST_F(HealthMonitorTest, HealthCheckRegistration) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    bool check_called = false;
    monitor.register_health_check("test_component", [&check_called]() {
        check_called = true;
        return HealthCheckResult("test_component", HealthStatus::HEALTHY, "Test OK");
    });
    
    SystemHealthStatus status = monitor.check_system_health();
    
    EXPECT_TRUE(check_called);
    EXPECT_EQ(status.overall_status, HealthStatus::HEALTHY);
    EXPECT_EQ(status.component_results.size(), 1);
    EXPECT_EQ(status.component_results[0].component_name, "test_component");
    EXPECT_EQ(status.component_results[0].status, HealthStatus::HEALTHY);
}

TEST_F(HealthMonitorTest, OverallStatusDetermination) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    // Register multiple health checks with different statuses
    monitor.register_health_check("healthy_component", []() {
        return HealthCheckResult("healthy_component", HealthStatus::HEALTHY, "OK");
    });
    
    monitor.register_health_check("warning_component", []() {
        return HealthCheckResult("warning_component", HealthStatus::WARNING, "Warning");
    });
    
    SystemHealthStatus status = monitor.check_system_health();
    
    // Overall status should be WARNING (worst non-failed status)
    EXPECT_EQ(status.overall_status, HealthStatus::WARNING);
    EXPECT_EQ(status.component_results.size(), 2);
}

TEST_F(HealthMonitorTest, CriticalStatusHandling) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    monitor.register_health_check("critical_component", []() {
        return HealthCheckResult("critical_component", HealthStatus::CRITICAL, "Critical issue");
    });
    
    SystemHealthStatus status = monitor.check_system_health();
    
    EXPECT_EQ(status.overall_status, HealthStatus::CRITICAL);
    EXPECT_FALSE(status.is_operational()); // Critical status should not be operational
}

TEST_F(HealthMonitorTest, PerformanceMetricsUpdate) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    PerformanceMetrics metrics;
    metrics.sensor_readings_success = 100;
    metrics.sensor_readings_failed = 5;
    metrics.storage_writes_success = 95;
    metrics.storage_writes_failed = 0;
    metrics.memory_usage_bytes = 8 * 1024 * 1024; // 8MB
    metrics.cpu_usage_percent = 25.0;
    
    monitor.update_performance_metrics(metrics);
    
    SystemHealthStatus status = monitor.get_last_health_status();
    EXPECT_EQ(status.performance_metrics.sensor_readings_success, 100);
    EXPECT_EQ(status.performance_metrics.memory_usage_bytes, 8 * 1024 * 1024);
}

TEST_F(HealthMonitorTest, HealthStatusJSON) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    monitor.register_health_check("test_component", []() {
        return HealthCheckResult("test_component", HealthStatus::HEALTHY, "Test message");
    });
    
    monitor.check_system_health();
    
    std::string json = monitor.get_health_status_json();
    
    // Basic JSON structure checks
    EXPECT_THAT(json, HasSubstr("\"overall_status\""));
    EXPECT_THAT(json, HasSubstr("\"HEALTHY\""));
    EXPECT_THAT(json, HasSubstr("\"test_component\""));
    EXPECT_THAT(json, HasSubstr("\"performance\""));
}

TEST_F(HealthMonitorTest, StatusFileWriting) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    std::string status_file = test_dir_ / "health_status.json";
    
    EXPECT_TRUE(monitor.write_status_file(status_file));
    EXPECT_TRUE(std::filesystem::exists(status_file));
    
    // Read and verify file content
    std::ifstream file(status_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    EXPECT_THAT(content, HasSubstr("overall_status"));
    EXPECT_THAT(content, HasSubstr("performance"));
}

TEST_F(HealthMonitorTest, HealthEventRecording) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    monitor.record_health_event("test_component", "test_event", "Test message");
    
    std::string diagnostic_report = monitor.get_diagnostic_report();
    
    EXPECT_THAT(diagnostic_report, HasSubstr("Recent Health Events"));
    EXPECT_THAT(diagnostic_report, HasSubstr("test_component"));
    EXPECT_THAT(diagnostic_report, HasSubstr("test_event"));
    EXPECT_THAT(diagnostic_report, HasSubstr("Test message"));
}

TEST_F(HealthMonitorTest, AlertCooldown) {
    HealthMonitor monitor;
    AlertConfig config;
    config.enabled = true;
    config.alert_cooldown = std::chrono::milliseconds(100); // Short cooldown for testing
    config.max_memory_mb = 1.0; // Very low threshold to trigger alert
    
    EXPECT_TRUE(monitor.initialize(config));
    
    // Set high memory usage to trigger alert
    PerformanceMetrics metrics;
    metrics.memory_usage_bytes = 10 * 1024 * 1024; // 10MB (above 1MB threshold)
    monitor.update_performance_metrics(metrics);
    
    SystemHealthStatus status;
    status.performance_metrics = metrics;
    
    // First alert should be sent
    monitor.check_and_trigger_alerts(status);
    
    // Second alert immediately should be blocked by cooldown
    monitor.check_and_trigger_alerts(status);
    
    // Wait for cooldown to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Third alert should be sent after cooldown
    monitor.check_and_trigger_alerts(status);
}

// Built-in health check tests
TEST_F(HealthMonitorTest, MemoryUsageHealthCheck) {
    HealthCheckResult result = health_checks::check_memory_usage(50.0); // 50MB limit
    
    EXPECT_EQ(result.component_name, "memory");
    EXPECT_TRUE(result.status == HealthStatus::HEALTHY || 
                result.status == HealthStatus::WARNING ||
                result.status == HealthStatus::CRITICAL);
    EXPECT_FALSE(result.message.empty());
    EXPECT_FALSE(result.details.empty());
}

TEST_F(HealthMonitorTest, CPUUsageHealthCheck) {
    HealthCheckResult result = health_checks::check_cpu_usage(80.0); // 80% limit
    
    EXPECT_EQ(result.component_name, "cpu");
    EXPECT_TRUE(result.status == HealthStatus::HEALTHY || 
                result.status == HealthStatus::WARNING ||
                result.status == HealthStatus::CRITICAL);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(HealthMonitorTest, DiskSpaceHealthCheck) {
    HealthCheckResult result = health_checks::check_disk_space(test_dir_.string(), 1.0); // 1MB minimum
    
    EXPECT_EQ(result.component_name, "disk");
    EXPECT_TRUE(result.status == HealthStatus::HEALTHY || 
                result.status == HealthStatus::WARNING ||
                result.status == HealthStatus::CRITICAL);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(HealthMonitorTest, SensorHealthCheckWithNullPointer) {
    HealthCheckResult result = health_checks::check_sensor_health(nullptr, 0.8);
    
    EXPECT_EQ(result.component_name, "sensor");
    EXPECT_EQ(result.status, HealthStatus::FAILED);
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

TEST_F(HealthMonitorTest, StorageHealthCheckWithNullPointer) {
    HealthCheckResult result = health_checks::check_storage_health(nullptr, 0.95);
    
    EXPECT_EQ(result.component_name, "storage");
    EXPECT_EQ(result.status, HealthStatus::FAILED);
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

// Test health status string conversions
TEST_F(HealthMonitorTest, HealthStatusStringConversions) {
    EXPECT_EQ(HealthMonitor::health_status_to_string(HealthStatus::HEALTHY), "HEALTHY");
    EXPECT_EQ(HealthMonitor::health_status_to_string(HealthStatus::WARNING), "WARNING");
    EXPECT_EQ(HealthMonitor::health_status_to_string(HealthStatus::CRITICAL), "CRITICAL");
    EXPECT_EQ(HealthMonitor::health_status_to_string(HealthStatus::FAILED), "FAILED");
    
    EXPECT_EQ(HealthMonitor::string_to_health_status("HEALTHY"), HealthStatus::HEALTHY);
    EXPECT_EQ(HealthMonitor::string_to_health_status("WARNING"), HealthStatus::WARNING);
    EXPECT_EQ(HealthMonitor::string_to_health_status("CRITICAL"), HealthStatus::CRITICAL);
    EXPECT_EQ(HealthMonitor::string_to_health_status("FAILED"), HealthStatus::FAILED);
}

// Test SystemHealthStatus methods
TEST_F(HealthMonitorTest, SystemHealthStatusMethods) {
    SystemHealthStatus status;
    status.overall_status = HealthStatus::HEALTHY;
    status.last_check = std::chrono::system_clock::now();
    
    EXPECT_EQ(status.get_status_string(), "HEALTHY");
    EXPECT_TRUE(status.is_operational());
    
    std::string report = status.get_detailed_report();
    EXPECT_THAT(report, HasSubstr("System Health Report"));
    EXPECT_THAT(report, HasSubstr("HEALTHY"));
}

// Integration test with exception handling
TEST_F(HealthMonitorTest, ExceptionHandlingInHealthChecks) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    // Register a health check that throws an exception
    monitor.register_health_check("exception_component", []() -> HealthCheckResult {
        throw std::runtime_error("Test exception");
    });
    
    SystemHealthStatus status = monitor.check_system_health();
    
    // Should handle exception gracefully
    EXPECT_EQ(status.component_results.size(), 1);
    EXPECT_EQ(status.component_results[0].status, HealthStatus::FAILED);
    EXPECT_THAT(status.component_results[0].message, HasSubstr("exception"));
}

// Test health monitoring with multiple components
TEST_F(HealthMonitorTest, MultiComponentHealthMonitoring) {
    HealthMonitor monitor;
    AlertConfig config;
    config.enabled = true;
    EXPECT_TRUE(monitor.initialize(config));
    
    // Register multiple health checks with different statuses
    monitor.register_health_check("healthy_component", []() {
        return HealthCheckResult("healthy_component", HealthStatus::HEALTHY, "OK");
    });
    
    monitor.register_health_check("warning_component", []() {
        return HealthCheckResult("warning_component", HealthStatus::WARNING, "Warning");
    });
    
    monitor.register_health_check("critical_component", []() {
        return HealthCheckResult("critical_component", HealthStatus::CRITICAL, "Critical");
    });
    
    SystemHealthStatus status = monitor.check_system_health();
    
    // Overall status should be CRITICAL (worst status)
    EXPECT_EQ(status.overall_status, HealthStatus::CRITICAL);
    EXPECT_EQ(status.component_results.size(), 3);
    
    // Check if components are correctly identified
    bool found_healthy = false;
    bool found_warning = false;
    bool found_critical = false;
    
    for (const auto& result : status.component_results) {
        if (result.component_name == "healthy_component") {
            found_healthy = true;
            EXPECT_EQ(result.status, HealthStatus::HEALTHY);
        } else if (result.component_name == "warning_component") {
            found_warning = true;
            EXPECT_EQ(result.status, HealthStatus::WARNING);
        } else if (result.component_name == "critical_component") {
            found_critical = true;
            EXPECT_EQ(result.status, HealthStatus::CRITICAL);
        }
    }
    
    EXPECT_TRUE(found_healthy);
    EXPECT_TRUE(found_warning);
    EXPECT_TRUE(found_critical);
}

// Test health monitoring with dynamic status changes
TEST_F(HealthMonitorTest, DynamicStatusChanges) {
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    // Use atomic to safely change health status from multiple threads
    std::atomic<HealthStatus> component_status{HealthStatus::HEALTHY};
    
    // Register health check that returns dynamic status
    monitor.register_health_check("dynamic_component", [&component_status]() {
        return HealthCheckResult("dynamic_component", component_status, "Dynamic status");
    });
    
    // Initial status should be HEALTHY
    SystemHealthStatus status1 = monitor.check_system_health();
    EXPECT_EQ(status1.overall_status, HealthStatus::HEALTHY);
    
    // Change status to WARNING
    component_status = HealthStatus::WARNING;
    SystemHealthStatus status2 = monitor.check_system_health();
    EXPECT_EQ(status2.overall_status, HealthStatus::WARNING);
    
    // Change status to CRITICAL
    component_status = HealthStatus::CRITICAL;
    SystemHealthStatus status3 = monitor.check_system_health();
    EXPECT_EQ(status3.overall_status, HealthStatus::CRITICAL);
    
    // Change status back to HEALTHY
    component_status = HealthStatus::HEALTHY;
    SystemHealthStatus status4 = monitor.check_system_health();
    EXPECT_EQ(status4.overall_status, HealthStatus::HEALTHY);
}