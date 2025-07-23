#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class DataInfoEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_data_info_endpoint";
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

TEST_F(DataInfoEndpointTest, BasicInfoRequest) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_records\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"database_path\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"database_size_bytes\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"implementation\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"healthy\":") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestEmptyDatabase) {
    // No data inserted
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_records\": 0") != std::string::npos);
    EXPECT_TRUE(response.find("\"healthy\": true") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestWithData) {
    insert_test_data(10);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Should have some records now
    EXPECT_FALSE(response.find("\"total_records\": 0") != std::string::npos);
    
    // Should have database size > 0
    EXPECT_TRUE(response.find("\"database_size_bytes\":") != std::string::npos);
    
    // Should have timestamps
    EXPECT_TRUE(response.find("\"earliest_timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"latest_timestamp\":") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestWithoutStorage) {
    // Create server without storage
    auto server_no_storage = std::make_unique<HealthMonitorServer>(health_monitor_.get(), nullptr);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_no_storage->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Storage not available\"") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestDatabasePath) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"database_path\": \"" + test_dir_ + "\"") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestImplementationField) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"implementation\": \"RocksDB via HTTP API\"") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestHealthyField) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"healthy\": true") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestTimestampFormat) {
    insert_test_data(1);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Check for ISO 8601 timestamp format
    EXPECT_TRUE(response.find("\"earliest_timestamp\": \"") != std::string::npos);
    EXPECT_TRUE(response.find("\"latest_timestamp\": \"") != std::string::npos);
    
    // Should contain 'T' and 'Z' for ISO 8601 format
    EXPECT_TRUE(response.find("T") != std::string::npos);
    EXPECT_TRUE(response.find("Z") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestResponseFormat) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    // Check HTTP headers
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(response.find("Access-Control-Allow-Origin: *") != std::string::npos);
    
    // Check JSON structure
    EXPECT_TRUE(response.find("{\n") != std::string::npos);
    EXPECT_TRUE(response.find("}\n") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestNoParameters) {
    // Test that info endpoint doesn't require any parameters
    std::string request = "GET /data/info?extra=param HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_records\":") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestAllRequiredFields) {
    insert_test_data(3);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Check all required fields are present
    std::vector<std::string> required_fields = {
        "\"total_records\":",
        "\"database_path\":",
        "\"earliest_timestamp\":",
        "\"latest_timestamp\":",
        "\"database_size_bytes\":",
        "\"implementation\":",
        "\"healthy\":"
    };
    
    for (const auto& field : required_fields) {
        EXPECT_TRUE(response.find(field) != std::string::npos) 
            << "Missing required field: " << field;
    }
}

TEST_F(DataInfoEndpointTest, InfoRequestDatabaseSizePositive) {
    insert_test_data(5);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Database size should be greater than 0 when there's data
    EXPECT_FALSE(response.find("\"database_size_bytes\": 0") != std::string::npos);
}

TEST_F(DataInfoEndpointTest, InfoRequestConsistentTimestamps) {
    insert_test_data(3);
    
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_data_info_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Both timestamps should be present and valid
    EXPECT_TRUE(response.find("\"earliest_timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"latest_timestamp\":") != std::string::npos);
    
    // Should not contain null timestamps when data exists
    EXPECT_FALSE(response.find("\"earliest_timestamp\": null") != std::string::npos);
    EXPECT_FALSE(response.find("\"latest_timestamp\": null") != std::string::npos);
}