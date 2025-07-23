#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class DataRangeEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_data_range_endpoint";
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
        
        // Set up test time points
        base_time_ = std::chrono::system_clock::now() - std::chrono::hours(2);
        start_time_str_ = "2024-01-01T12:00:00Z";
        end_time_str_ = "2024-01-01T13:00:00Z";
    }
    
    void TearDown() override {
        server_.reset();
        storage_.reset();
        health_monitor_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    void insert_test_data_with_times() {
        // Insert data at specific times for predictable testing
        auto start_time = std::chrono::system_clock::now() - std::chrono::hours(2);
        
        for (int i = 0; i < 10; ++i) {
            SensorData reading;
            reading.timestamp = start_time + std::chrono::minutes(i * 10);
            reading.co2_ppm = 400.0f + i * 5.0f;
            reading.temperature_c = 20.0f + i * 0.2f;
            reading.humidity_percent = 40.0f + i * 1.0f;
            reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            
            ASSERT_TRUE(storage_->store_reading(reading));
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitorServer> server_;
    std::chrono::system_clock::time_point base_time_;
    std::string start_time_str_;
    std::string end_time_str_;
};

TEST_F(DataRangeEndpointTest, BasicRangeRequest) {
    insert_test_data_with_times();
    
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"" + start_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"" + end_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\":") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestEmptyDatabase) {
    // No data inserted
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 0") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": []") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestMissingStartParameter) {
    std::string request = "GET /data/range?end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Missing required parameters\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestMissingEndParameter) {
    std::string request = "GET /data/range?start=" + start_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Missing required parameters\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestMissingBothParameters) {
    std::string request = "GET /data/range HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Missing required parameters\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestInvalidStartTime) {
    std::string request = "GET /data/range?start=invalid-time&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid start time\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestInvalidEndTime) {
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=invalid-time HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid end time\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestInvalidTimeRange) {
    // End time before start time
    std::string request = "GET /data/range?start=" + end_time_str_ + "&end=" + start_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid time range\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestTooLargeRange) {
    // Range longer than 7 days
    std::string start_time = "2024-01-01T00:00:00Z";
    std::string end_time = "2024-01-10T00:00:00Z";  // 9 days
    
    std::string request = "GET /data/range?start=" + start_time + "&end=" + end_time + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid time range\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestWithoutStorage) {
    // Create server without storage
    auto server_no_storage = std::make_unique<HealthMonitorServer>(health_monitor_.get(), nullptr);
    
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_no_storage->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Storage not available\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestDataContent) {
    insert_test_data_with_times();
    
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Check for sensor data fields
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"temperature_c\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"humidity_percent\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"quality_flags\":") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestUrlEncoding) {
    // Test URL-encoded timestamps
    std::string encoded_start = "2024-01-01T12%3A00%3A00Z";  // %3A = :
    std::string encoded_end = "2024-01-01T13%3A00%3A00Z";
    
    std::string request = "GET /data/range?start=" + encoded_start + "&end=" + encoded_end + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"2024-01-01T12:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"2024-01-01T13:00:00Z\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestMultipleParameters) {
    insert_test_data_with_times();
    
    // Test with additional parameters (should be ignored)
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + "&other=value HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"" + start_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"" + end_time_str_ + "\"") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestResponseFormat) {
    insert_test_data_with_times();
    
    std::string request = "GET /data/range?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_range_data_request(request);
    
    // Check HTTP headers
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(response.find("Access-Control-Allow-Origin: *") != std::string::npos);
    
    // Check JSON structure
    EXPECT_TRUE(response.find("{\n") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\":") != std::string::npos);
}

TEST_F(DataRangeEndpointTest, RangeRequestValidTimeFormats) {
    // Test different valid ISO 8601 formats
    std::vector<std::pair<std::string, std::string>> time_formats = {
        {"2024-01-01T12:00:00Z", "2024-01-01T13:00:00Z"},
        {"2024-01-01T12:00:00.000Z", "2024-01-01T13:00:00.000Z"}
    };
    
    for (const auto& [start, end] : time_formats) {
        std::string request = "GET /data/range?start=" + start + "&end=" + end + " HTTP/1.1\r\n\r\n";
        std::string response = server_->handle_range_data_request(request);
        
        EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos) 
            << "Failed for format: " << start << " to " << end;
    }
}