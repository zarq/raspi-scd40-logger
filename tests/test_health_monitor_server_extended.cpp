#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"

using namespace sensor_daemon;

class HealthMonitorServerExtendedTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_health_server_extended";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        // Initialize health monitor
        health_monitor_ = std::make_unique<HealthMonitor>();
        AlertConfig config;
        ASSERT_TRUE(health_monitor_->initialize(config));
        
        // Initialize storage
        storage_ = std::make_unique<TimeSeriesStorage>();
        ASSERT_TRUE(storage_->initialize(test_dir_, std::chrono::hours(24)));
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
    
    std::string test_dir_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitorServer> server_;
};

TEST_F(HealthMonitorServerExtendedTest, ConstructorWithStorage) {
    // Test constructor with storage
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    EXPECT_FALSE(server_->is_running());
}

TEST_F(HealthMonitorServerExtendedTest, ConstructorWithoutStorage) {
    // Test constructor without storage (backward compatibility)
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get());
    EXPECT_FALSE(server_->is_running());
}

TEST_F(HealthMonitorServerExtendedTest, ConstructorWithNullStorage) {
    // Test constructor with explicit null storage
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), nullptr);
    EXPECT_FALSE(server_->is_running());
}

TEST_F(HealthMonitorServerExtendedTest, ServerStartStop) {
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    
    // Test starting server
    EXPECT_TRUE(server_->start(8081, "127.0.0.1"));  // Use different port to avoid conflicts
    EXPECT_TRUE(server_->is_running());
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test stopping server
    server_->stop();
    EXPECT_FALSE(server_->is_running());
}

TEST_F(HealthMonitorServerExtendedTest, GetUrl) {
    server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    
    std::string expected_url = "http://127.0.0.1:8082/health";
    EXPECT_TRUE(server_->start(8082, "127.0.0.1"));
    
    EXPECT_EQ(server_->get_url(), expected_url);
}

TEST_F(HealthMonitorServerExtendedTest, HealthMonitorRequired) {
    // Test that server requires health monitor
    server_ = std::make_unique<HealthMonitorServer>(nullptr, storage_.get());
    EXPECT_FALSE(server_->start(8083, "127.0.0.1"));
    EXPECT_FALSE(server_->is_running());
}

// Note: Testing actual HTTP responses would require more complex setup
// and is better done as integration tests. These unit tests focus on
// the basic functionality and constructor behavior.