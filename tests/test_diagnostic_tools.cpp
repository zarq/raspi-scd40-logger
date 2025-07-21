#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "diagnostic_tools.hpp"
#include "logging_system.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace sensor_daemon;
using namespace testing;

class DiagnosticToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::DEBUG, "", 1024*1024, 1, false);
        
        // Create test directory
        test_dir_ = std::filesystem::temp_directory_path() / "diagnostic_test";
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

TEST_F(DiagnosticToolsTest, DiagnosticResultBasics) {
    DiagnosticResult result("test_diagnostic");
    
    EXPECT_EQ(result.test_name, "test_diagnostic");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(result.message.empty());
    EXPECT_TRUE(result.details.empty());
    
    result.add_detail("Test detail 1");
    result.add_detail("Test detail 2");
    
    EXPECT_EQ(result.details.size(), 2);
    EXPECT_EQ(result.details[0], "Test detail 1");
    EXPECT_EQ(result.details[1], "Test detail 2");
}

TEST_F(DiagnosticToolsTest, DiagnosticReportGeneration) {
    DiagnosticReport report;
    report.timestamp = std::chrono::system_clock::now();
    
    // Add some test results
    DiagnosticResult result1("test1");
    result1.passed = true;
    result1.message = "Test 1 passed";
    result1.duration = std::chrono::milliseconds(100);
    result1.add_detail("Detail 1");
    
    DiagnosticResult result2("test2");
    result2.passed = false;
    result2.message = "Test 2 failed";
    result2.duration = std::chrono::milliseconds(200);
    result2.add_detail("Error detail");
    
    report.test_results.push_back(result1);
    report.test_results.push_back(result2);
    
    // Test overall status
    EXPECT_FALSE(report.all_tests_passed()); // One test failed
    
    // Test formatted report
    std::string formatted = report.get_formatted_report();
    EXPECT_NE(formatted.find("SENSOR DAEMON DIAGNOSTIC REPORT"), std::string::npos);
    EXPECT_NE(formatted.find("[PASS] test1"), std::string::npos);
    EXPECT_NE(formatted.find("[FAIL] test2"), std::string::npos);
    EXPECT_NE(formatted.find("Test 1 passed"), std::string::npos);
    EXPECT_NE(formatted.find("Test 2 failed"), std::string::npos);
    
    // Test JSON report
    std::string json = report.get_json_report();
    EXPECT_NE(json.find("\"test_name\": \"test1\""), std::string::npos);
    EXPECT_NE(json.find("\"passed\": true"), std::string::npos);
    EXPECT_NE(json.find("\"passed\": false"), std::string::npos);
    EXPECT_NE(json.find("\"overall_status\": \"FAIL\""), std::string::npos);
}

TEST_F(DiagnosticToolsTest, SystemResourcesTest) {
    DiagnosticResult result = DiagnosticTools::test_system_resources(test_dir_.string());
    
    EXPECT_EQ(result.test_name, "System Resources");
    EXPECT_FALSE(result.details.empty());
    
    // Should have memory usage information
    bool found_memory = false;
    for (const auto& detail : result.details) {
        if (detail.find("Memory usage") != std::string::npos) {
            found_memory = true;
            break;
        }
    }
    EXPECT_TRUE(found_memory);
}

TEST_F(DiagnosticToolsTest, FilePermissionsTest) {
    DiagnosticResult result = DiagnosticTools::test_file_permissions(test_dir_.string());
    
    EXPECT_EQ(result.test_name, "File Permissions");
    EXPECT_FALSE(result.details.empty());
    
    // Should check if test directory is writable
    bool found_writable = false;
    for (const auto& detail : result.details) {
        if (detail.find("writable") != std::string::npos) {
            found_writable = true;
            break;
        }
    }
    EXPECT_TRUE(found_writable);
}

TEST_F(DiagnosticToolsTest, ConfigurationTest) {
    // Create a test config file
    std::string test_config = test_dir_ / "test_config.toml";
    std::ofstream config_file(test_config);
    config_file << "[daemon]\n";
    config_file << "sampling_interval_seconds = 30\n";
    config_file << "[sensor]\n";
    config_file << "i2c_device = \"/dev/i2c-1\"\n";
    config_file << "[storage]\n";
    config_file << "data_directory = \"/tmp/test\"\n";
    config_file.close();
    
    DiagnosticResult result = DiagnosticTools::test_configuration(test_config);
    
    EXPECT_EQ(result.test_name, "Configuration");
    EXPECT_TRUE(result.passed); // Should pass with valid config
    EXPECT_NE(result.message.find("valid"), std::string::npos);
}

TEST_F(DiagnosticToolsTest, TroubleshootingRecommendations) {
    DiagnosticReport report;
    
    // Add a failed I2C test
    DiagnosticResult i2c_result("I2C Communication");
    i2c_result.passed = false;
    i2c_result.message = "Sensor not connected";
    report.test_results.push_back(i2c_result);
    
    // Add a failed resources test
    DiagnosticResult resources_result("System Resources");
    resources_result.passed = false;
    resources_result.message = "High memory usage";
    report.test_results.push_back(resources_result);
    
    auto recommendations = DiagnosticTools::generate_troubleshooting_recommendations(report);
    
    EXPECT_FALSE(recommendations.empty());
    
    // Should have I2C-related recommendations
    bool found_i2c_rec = false;
    for (const auto& rec : recommendations) {
        if (rec.find("I2C") != std::string::npos || rec.find("i2c") != std::string::npos) {
            found_i2c_rec = true;
            break;
        }
    }
    EXPECT_TRUE(found_i2c_rec);
    
    // Should have memory-related recommendations
    bool found_memory_rec = false;
    for (const auto& rec : recommendations) {
        if (rec.find("memory") != std::string::npos || rec.find("Memory") != std::string::npos) {
            found_memory_rec = true;
            break;
        }
    }
    EXPECT_TRUE(found_memory_rec);
}

TEST_F(DiagnosticToolsTest, HealthEndpointCreation) {
    // Create a mock health monitor
    HealthMonitor monitor;
    AlertConfig config;
    EXPECT_TRUE(monitor.initialize(config));
    
    HealthEndpointConfig endpoint_config;
    endpoint_config.status_file_path = (test_dir_ / "health.json").string();
    endpoint_config.update_interval = std::chrono::seconds(1);
    
    // Test endpoint creation
    bool created = DiagnosticTools::create_health_endpoint(&monitor, endpoint_config);
    EXPECT_TRUE(created);
    
    // Wait a bit for the endpoint to update
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    // Check if status file was created
    EXPECT_TRUE(std::filesystem::exists(endpoint_config.status_file_path));
    
    // Test endpoint update
    bool updated = DiagnosticTools::update_health_endpoint(&monitor, endpoint_config);
    EXPECT_TRUE(updated);
    
    monitor.shutdown();
}

TEST_F(DiagnosticToolsTest, DiagnosticReportBasics) {
    DiagnosticReport report;
    report.timestamp = std::chrono::system_clock::now();
    
    // Add passing test
    DiagnosticResult passing_test("passing_test");
    passing_test.passed = true;
    passing_test.message = "Test passed";
    report.test_results.push_back(passing_test);
    
    // Add failing test
    DiagnosticResult failing_test("failing_test");
    failing_test.passed = false;
    failing_test.message = "Test failed";
    report.test_results.push_back(failing_test);
    
    EXPECT_FALSE(report.all_tests_passed());
    
    std::string formatted = report.get_formatted_report();
    EXPECT_THAT(formatted, HasSubstr("DIAGNOSTIC REPORT"));
    EXPECT_THAT(formatted, HasSubstr("PASS"));
    EXPECT_THAT(formatted, HasSubstr("FAIL"));
    EXPECT_THAT(formatted, HasSubstr("passing_test"));
    EXPECT_THAT(formatted, HasSubstr("failing_test"));
    
    std::string json = report.get_json_report();
    EXPECT_THAT(json, HasSubstr("\"overall_status\""));
    EXPECT_THAT(json, HasSubstr("\"test_results\""));
    EXPECT_THAT(json, HasSubstr("\"passing_test\""));
    EXPECT_THAT(json, HasSubstr("\"failing_test\""));
}

TEST_F(DiagnosticToolsTest, SystemResourcesTest) {
    DiagnosticResult result = DiagnosticTools::test_system_resources(test_dir_.string());
    
    EXPECT_EQ(result.test_name, "System Resources");
    EXPECT_TRUE(result.duration.count() >= 0);
    EXPECT_FALSE(result.message.empty());
    
    // Should have details about memory and disk
    bool has_memory_detail = false;
    bool has_disk_detail = false;
    
    for (const auto& detail : result.details) {
        if (detail.find("Memory usage") != std::string::npos) {
            has_memory_detail = true;
        }
        if (detail.find("disk space") != std::string::npos) {
            has_disk_detail = true;
        }
    }
    
    EXPECT_TRUE(has_memory_detail);
    EXPECT_TRUE(has_disk_detail);
}

TEST_F(DiagnosticToolsTest, FilePermissionsTest) {
    DiagnosticResult result = DiagnosticTools::test_file_permissions(test_dir_.string());
    
    EXPECT_EQ(result.test_name, "File Permissions");
    EXPECT_FALSE(result.message.empty());
    
    // Should check the test directory
    bool found_test_dir = false;
    for (const auto& detail : result.details) {
        if (detail.find(test_dir_.string()) != std::string::npos) {
            found_test_dir = true;
            break;
        }
    }
    EXPECT_TRUE(found_test_dir);
}

TEST_F(DiagnosticToolsTest, ConfigurationTestWithValidFile) {
    // Create a test configuration file
    std::string config_file = test_dir_ / "test_config.toml";
    std::ofstream file(config_file);
    file << "[daemon]\n";
    file << "sampling_interval_seconds = 30\n";
    file << "data_retention_days = 365\n";
    file << "log_level = \"info\"\n";
    file << "\n";
    file << "[sensor]\n";
    file << "i2c_device = \"/dev/i2c-1\"\n";
    file << "i2c_address = 0x62\n";
    file << "\n";
    file << "[storage]\n";
    file << "data_directory = \"/tmp/test\"\n";
    file.close();
    
    DiagnosticResult result = DiagnosticTools::test_configuration(config_file);
    
    EXPECT_EQ(result.test_name, "Configuration");
    EXPECT_TRUE(result.passed);
    EXPECT_THAT(result.message, HasSubstr("valid"));
    
    // Should have configuration details
    bool has_config_details = false;
    for (const auto& detail : result.details) {
        if (detail.find("Sampling interval") != std::string::npos ||
            detail.find("Data retention") != std::string::npos) {
            has_config_details = true;
            break;
        }
    }
    EXPECT_TRUE(has_config_details);
}

TEST_F(DiagnosticToolsTest, ConfigurationTestWithMissingFile) {
    std::string nonexistent_file = test_dir_ / "nonexistent.toml";
    
    DiagnosticResult result = DiagnosticTools::test_configuration(nonexistent_file);
    
    EXPECT_EQ(result.test_name, "Configuration");
    EXPECT_TRUE(result.passed); // Should pass with defaults
    EXPECT_THAT(result.message, HasSubstr("default"));
}

TEST_F(DiagnosticToolsTest, LoggingSystemTest) {
    DiagnosticResult result = DiagnosticTools::test_logging_system();
    
    EXPECT_EQ(result.test_name, "Logging System");
    EXPECT_TRUE(result.passed); // Should pass since we initialized logging in SetUp
    EXPECT_THAT(result.message, HasSubstr("functional"));
    
    // Should have logging details
    bool has_log_level = false;
    for (const auto& detail : result.details) {
        if (detail.find("Log level") != std::string::npos) {
            has_log_level = true;
            break;
        }
    }
    EXPECT_TRUE(has_log_level);
}

TEST_F(DiagnosticToolsTest, SystemDependenciesTest) {
    DiagnosticResult result = DiagnosticTools::test_system_dependencies();
    
    EXPECT_EQ(result.test_name, "System Dependencies");
    EXPECT_FALSE(result.message.empty());
    
    // Should check for I2C and systemd
    bool has_i2c_check = false;
    bool has_systemd_check = false;
    
    for (const auto& detail : result.details) {
        if (detail.find("I2C") != std::string::npos) {
            has_i2c_check = true;
        }
        if (detail.find("systemd") != std::string::npos) {
            has_systemd_check = true;
        }
    }
    
    EXPECT_TRUE(has_i2c_check);
    EXPECT_TRUE(has_systemd_check);
}

TEST_F(DiagnosticToolsTest, DaemonStatusTest) {
    DiagnosticResult result = DiagnosticTools::test_daemon_status();
    
    EXPECT_EQ(result.test_name, "Daemon Status");
    EXPECT_FALSE(result.message.empty());
    
    // Should check daemon process and systemd service
    bool has_process_check = false;
    bool has_service_check = false;
    
    for (const auto& detail : result.details) {
        if (detail.find("process running") != std::string::npos) {
            has_process_check = true;
        }
        if (detail.find("service active") != std::string::npos) {
            has_service_check = true;
        }
    }
    
    EXPECT_TRUE(has_process_check);
    EXPECT_TRUE(has_service_check);
}

TEST_F(DiagnosticToolsTest, I2CTestWithNullPointer) {
    DiagnosticResult result = DiagnosticTools::test_i2c_communication(nullptr);
    
    EXPECT_EQ(result.test_name, "I2C Communication");
    EXPECT_FALSE(result.passed);
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

TEST_F(DiagnosticToolsTest, StorageTestWithNullPointer) {
    DiagnosticResult result = DiagnosticTools::test_storage_functionality(nullptr);
    
    EXPECT_EQ(result.test_name, "Storage Functionality");
    EXPECT_FALSE(result.passed);
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

TEST_F(DiagnosticToolsTest, ComprehensiveDiagnostics) {
    DiagnosticReport report = DiagnosticTools::run_comprehensive_diagnostics();
    
    EXPECT_FALSE(report.test_results.empty());
    EXPECT_FALSE(report.summary.empty());
    
    // Should have basic system tests
    std::vector<std::string> expected_tests = {
        "System Resources",
        "System Dependencies", 
        "Configuration",
        "File Permissions",
        "Logging System",
        "Daemon Status"
    };
    
    for (const auto& expected_test : expected_tests) {
        bool found = false;
        for (const auto& result : report.test_results) {
            if (result.test_name == expected_test) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected test not found: " << expected_test;
    }
}

TEST_F(DiagnosticToolsTest, TroubleshootingRecommendations) {
    DiagnosticReport report;
    
    // Add a failing I2C test
    DiagnosticResult i2c_test("I2C Communication");
    i2c_test.passed = false;
    i2c_test.message = "Sensor not connected";
    report.test_results.push_back(i2c_test);
    
    // Add a failing resource test
    DiagnosticResult resource_test("System Resources");
    resource_test.passed = false;
    resource_test.message = "Low disk space";
    report.test_results.push_back(resource_test);
    
    auto recommendations = DiagnosticTools::generate_troubleshooting_recommendations(report);
    
    EXPECT_FALSE(recommendations.empty());
    
    // Should have I2C-related recommendations
    bool has_i2c_rec = false;
    bool has_disk_rec = false;
    
    for (const auto& rec : recommendations) {
        if (rec.find("I2C") != std::string::npos || rec.find("i2c") != std::string::npos) {
            has_i2c_rec = true;
        }
        if (rec.find("disk") != std::string::npos) {
            has_disk_rec = true;
        }
    }
    
    EXPECT_TRUE(has_i2c_rec);
    EXPECT_TRUE(has_disk_rec);
}

TEST_F(DiagnosticToolsTest, TroubleshootingRecommendationsAllPassed) {
    DiagnosticReport report;
    
    // Add passing tests
    DiagnosticResult test1("Test1");
    test1.passed = true;
    report.test_results.push_back(test1);
    
    DiagnosticResult test2("Test2");
    test2.passed = true;
    report.test_results.push_back(test2);
    
    auto recommendations = DiagnosticTools::generate_troubleshooting_recommendations(report);
    
    EXPECT_EQ(recommendations.size(), 1);
    EXPECT_THAT(recommendations[0], HasSubstr("healthy"));
}

// Test timing functionality
TEST_F(DiagnosticToolsTest, TestTiming) {
    auto start = std::chrono::steady_clock::now();
    
    DiagnosticResult result = DiagnosticTools::test_system_resources();
    
    auto end = std::chrono::steady_clock::now();
    auto expected_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Duration should be reasonable (within 10x of actual time, accounting for test overhead)
    EXPECT_LE(result.duration.count(), expected_duration.count() * 10);
    EXPECT_GT(result.duration.count(), 0);
}

// Test JSON output format
TEST_F(DiagnosticToolsTest, JSONOutputFormat) {
    DiagnosticReport report;
    report.timestamp = std::chrono::system_clock::now();
    report.summary = "Test summary";
    
    DiagnosticResult test("test_name");
    test.passed = true;
    test.message = "test message";
    test.duration = std::chrono::milliseconds(100);
    test.add_detail("detail1");
    test.add_detail("detail2");
    report.test_results.push_back(test);
    
    std::string json = report.get_json_report();
    
    // Verify JSON structure
    EXPECT_THAT(json, HasSubstr("\"timestamp\""));
    EXPECT_THAT(json, HasSubstr("\"overall_status\""));
    EXPECT_THAT(json, HasSubstr("\"test_results\""));
    EXPECT_THAT(json, HasSubstr("\"test_name\": \"test_name\""));
    EXPECT_THAT(json, HasSubstr("\"passed\": true"));
    EXPECT_THAT(json, HasSubstr("\"message\": \"test message\""));
    EXPECT_THAT(json, HasSubstr("\"duration_ms\": 100"));
    EXPECT_THAT(json, HasSubstr("\"details\""));
    EXPECT_THAT(json, HasSubstr("\"summary\": \"Test summary\""));
}

// Test health endpoint creation and update
TEST_F(DiagnosticToolsTest, HealthEndpointCreationAndUpdate) {
    // Create a mock health monitor
    HealthMonitor monitor;
    AlertConfig config;
    ASSERT_TRUE(monitor.initialize(config));
    
    // Register a simple health check
    monitor.register_health_check("test_component", []() {
        return HealthCheckResult("test_component", HealthStatus::HEALTHY, "Test OK");
    });
    
    // Run health check to populate status
    monitor.check_system_health();
    
    // Configure health endpoint
    HealthEndpointConfig endpoint_config;
    endpoint_config.status_file_path = test_dir_ / "health.json";
    endpoint_config.update_interval = std::chrono::milliseconds(100); // Short interval for testing
    
    // Create health endpoint
    EXPECT_TRUE(DiagnosticTools::create_health_endpoint(&monitor, endpoint_config));
    
    // Verify file was created
    EXPECT_TRUE(std::filesystem::exists(endpoint_config.status_file_path));
    
    // Read file content
    std::ifstream file(endpoint_config.status_file_path);
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    // Verify basic content
    EXPECT_THAT(content, HasSubstr("\"status\""));
    EXPECT_THAT(content, HasSubstr("\"operational\""));
    EXPECT_THAT(content, HasSubstr("\"timestamp\""));
    
    // Update health endpoint manually
    EXPECT_TRUE(DiagnosticTools::update_health_endpoint(&monitor, endpoint_config));
    
    // Wait for automatic update
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Clean up (stop update thread)
    HealthEndpointConfig disabled_config;
    disabled_config.enabled = false;
    DiagnosticTools::create_health_endpoint(nullptr, disabled_config);
}

// Test I2C bus scanning
TEST_F(DiagnosticToolsTest, I2CBusScan) {
    // This test may fail if no I2C device is available
    // We're just testing the function doesn't crash
    DiagnosticResult result = DiagnosticTools::test_i2c_bus("/dev/null");
    
    EXPECT_EQ(result.test_name, "I2C Bus Scan");
    EXPECT_FALSE(result.passed); // Should fail with /dev/null
    EXPECT_FALSE(result.message.empty());
}

// Test sensor data quality check
TEST_F(DiagnosticToolsTest, SensorDataQualityCheck) {
    DiagnosticResult result = DiagnosticTools::test_sensor_data_quality(nullptr, 1);
    
    EXPECT_EQ(result.test_name, "Sensor Data Quality");
    EXPECT_FALSE(result.passed); // Should fail with nullptr
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

// Test storage query performance
TEST_F(DiagnosticToolsTest, StorageQueryPerformance) {
    DiagnosticResult result = DiagnosticTools::test_storage_query_performance(nullptr);
    
    EXPECT_EQ(result.test_name, "Storage Query Performance");
    EXPECT_FALSE(result.passed); // Should fail with nullptr
    EXPECT_THAT(result.message, HasSubstr("not available"));
}

// Test parent directory creation
TEST_F(DiagnosticToolsTest, CreateParentDirectories) {
    std::string nested_path = test_dir_ / "nested" / "directories" / "file.txt";
    
    // Create a file with nested directories
    std::ofstream file(nested_path);
    file << "Test content";
    file.close();
    
    // Check if file exists
    EXPECT_TRUE(std::filesystem::exists(nested_path));
}

// Test CLI specific test execution
TEST_F(DiagnosticToolsTest, CLISpecificTest) {
    std::vector<std::string> args;
    int result = DiagnosticCLI::run_specific_test("resources", args);
    
    // We can't assert much about the result since it depends on the system
    // Just verify it returns without crashing
    EXPECT_TRUE(result == 0 || result == 1);
}