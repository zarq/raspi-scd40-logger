#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class DataRecentEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_data_recent_endpoint";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        // Initialize health monitor
        health_monitor_ = std::make_unique<HealthMonitor>();
        AlertConfig config;
        ASSERT_TRUE(health_monitor_->initialize(config));
        
        // Initialize storage
        storage_ = std::make_unique<TimeSeriesStorage>();
        ASSERT_TRUE(storage_->initialize(test_dir_, std::chrono::hours(24)));
        
        // Create server with storage
        server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    }
    
    void TearDown() override {
        server_.reset();
        storage_.reset();
        health_monitor_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    void insert_test_data(int count = 5) {
        auto base_time = std::chrono::system_clock::now() - std::chrono::hours(1);
        
        for (int i = 0; i < count; ++i) {
            SensorData reading;
            reading.timestamp = base_time + std::chrono::minutes(i * 5);
            reading.co2_ppm = 400.0f + i * 10.0f;
            reading.temperature_c = 20.0f + i * 0.5f;
            reading.humidity_percent = 40.0f + i * 2.0f;
            reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            
            ASSERT_TRUE(storage_->store_reading(reading));
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitorServer> server_;
};

TEST_F(DataRecentEndpointTest, BasicRecentRequest) {
    insert_test_data(3);
    
    std::string request = "GET /data/recent HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 3") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestWithCount) {
    insert_test_data(10);
    
    std::string request = "GET /data/recent?count=5 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 5") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestEmptyDatabase) {
    // No data inserted
    std::string request = "GET /data/recent HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 0") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": []") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestInvalidCount) {
    insert_test_data(3);
    
    std::string request = "GET /data/recent?count=-5 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid count parameter\"") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestInvalidCountString) {
    insert_test_data(3);
    
    std::string request = "GET /data/recent?count=invalid HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 3") != std::string::npos);
    // Should use default count when parsing fails
}

TEST_F(DataRecentEndpointTest, RecentRequestLargeCount) {
    insert_test_data(5);
    
    std::string request = "GET /data/recent?count=20000 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid count parameter\"") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestZeroCount) {
    insert_test_data(3);
    
    std::string request = "GET /data/recent?count=0 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid count parameter\"") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestWithoutStorage) {
    // Create server without storage
    auto server_no_storage = std::make_unique<HealthMonitorServer>(health_monitor_.get(), nullptr);
    
    std::string request = "GET /data/recent HTTP/1.1\r\n\r\n";
    std::string response = server_no_storage->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Storage not available\"") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestDataContent) {
    insert_test_data(2);
    
    std::string request = "GET /data/recent?count=2 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Check for sensor data fields
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"temperature_c\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"humidity_percent\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"quality_flags\":") != std::string::npos);
    
    // Check for expected values (most recent should be higher)
    EXPECT_TRUE(response.find("410") != std::string::npos || response.find("420") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestMultipleParameters) {
    insert_test_data(5);
    
    // Test with multiple parameters (count should be parsed correctly)
    std::string request = "GET /data/recent?count=3&other=value HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 3") != std::string::npos);
}

TEST_F(DataRecentEndpointTest, RecentRequestResponseFormat) {
    insert_test_data(1);
    
    std::string request = "GET /data/recent?count=1 HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_recent_data_request(request);
    
    // Check HTTP headers
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(response.find("Access-Control-Allow-Origin: *") != std::string::npos);
    
    // Check JSON structure
    EXPECT_TRUE(response.find("{\n") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\":") != std::string::npos);
}