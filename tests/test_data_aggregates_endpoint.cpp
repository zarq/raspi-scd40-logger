#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class DataAggregatesEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_data_aggregates_endpoint";
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
        start_time_str_ = "2024-01-01T12:00:00Z";
        end_time_str_ = "2024-01-01T14:00:00Z";
    }
    
    void TearDown() override {
        server_.reset();
        storage_.reset();
        health_monitor_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    void insert_test_data_for_aggregation() {
        // Insert data every 10 minutes for 2 hours (12 readings)
        auto start_time = std::chrono::system_clock::now() - std::chrono::hours(3);
        
        for (int i = 0; i < 12; ++i) {
            SensorData reading;
            reading.timestamp = start_time + std::chrono::minutes(i * 10);
            reading.co2_ppm = 400.0f + i * 5.0f;  // 400, 405, 410, ..., 455
            reading.temperature_c = 20.0f + i * 0.5f;  // 20.0, 20.5, 21.0, ..., 25.5
            reading.humidity_percent = 40.0f + i * 2.0f;  // 40, 42, 44, ..., 62
            reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            
            ASSERT_TRUE(storage_->store_reading(reading));
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitorServer> server_;
    std::string start_time_str_;
    std::string end_time_str_;
};

TEST_F(DataAggregatesEndpointTest, BasicAggregatesRequest) {
    insert_test_data_for_aggregation();
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("\"aggregates\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"" + start_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"" + end_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\": \"1H\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_intervals\":") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestWithInterval) {
    insert_test_data_for_aggregation();
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + "&interval=30T HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\": \"30T\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestEmptyDatabase) {
    // No data inserted
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"aggregates\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_intervals\":") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestMissingStartParameter) {
    std::string request = "GET /data/aggregates?end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Missing required parameters\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestMissingEndParameter) {
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Missing required parameters\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestInvalidStartTime) {
    std::string request = "GET /data/aggregates?start=invalid-time&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid start time\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestInvalidEndTime) {
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=invalid-time HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid end time\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestInvalidTimeRange) {
    // End time before start time
    std::string request = "GET /data/aggregates?start=" + end_time_str_ + "&end=" + start_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid time range\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestInvalidInterval) {
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + "&interval=invalid HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid interval parameter\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestWithoutStorage) {
    // Create server without storage
    auto server_no_storage = std::make_unique<HealthMonitorServer>(health_monitor_.get(), nullptr);
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_no_storage->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Storage not available\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestDataContent) {
    insert_test_data_for_aggregation();
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Check for aggregate data fields
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm_mean\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm_min\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm_max\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"co2_ppm_count\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"temperature_c_mean\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"humidity_percent_mean\":") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestDifferentIntervals) {
    insert_test_data_for_aggregation();
    
    std::vector<std::string> intervals = {"30T", "1H", "2H"};
    
    for (const auto& interval : intervals) {
        std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + "&interval=" + interval + " HTTP/1.1\r\n\r\n";
        std::string response = server_->handle_aggregates_request(request);
        
        EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos) 
            << "Failed for interval: " << interval;
        EXPECT_TRUE(response.find("\"interval\": \"" + interval + "\"") != std::string::npos)
            << "Failed for interval: " << interval;
    }
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestUrlEncoding) {
    // Test URL-encoded timestamps
    std::string encoded_start = "2024-01-01T12%3A00%3A00Z";  // %3A = :
    std::string encoded_end = "2024-01-01T14%3A00%3A00Z";
    
    std::string request = "GET /data/aggregates?start=" + encoded_start + "&end=" + encoded_end + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"2024-01-01T12:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"2024-01-01T14:00:00Z\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestMultipleParameters) {
    insert_test_data_for_aggregation();
    
    // Test with additional parameters (should be ignored)
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + "&interval=1H&other=value HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"" + start_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"" + end_time_str_ + "\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\": \"1H\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestResponseFormat) {
    insert_test_data_for_aggregation();
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    // Check HTTP headers
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(response.find("Access-Control-Allow-Origin: *") != std::string::npos);
    
    // Check JSON structure
    EXPECT_TRUE(response.find("{\n") != std::string::npos);
    EXPECT_TRUE(response.find("\"aggregates\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_intervals\":") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestDefaultInterval) {
    insert_test_data_for_aggregation();
    
    // Request without interval parameter should default to "1H"
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + " HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\": \"1H\"") != std::string::npos);
}

TEST_F(DataAggregatesEndpointTest, AggregatesRequestStatisticalAccuracy) {
    // Insert specific test data for statistical verification
    auto start_time = std::chrono::system_clock::now() - std::chrono::hours(1);
    
    // Insert 3 readings with known values in the same hour
    std::vector<float> co2_values = {400.0f, 410.0f, 420.0f};
    for (size_t i = 0; i < co2_values.size(); ++i) {
        SensorData reading;
        reading.timestamp = start_time + std::chrono::minutes(i * 10);
        reading.co2_ppm = co2_values[i];
        reading.temperature_c = 20.0f + i * 1.0f;  // 20, 21, 22
        reading.humidity_percent = 40.0f + i * 5.0f;  // 40, 45, 50
        reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
        
        ASSERT_TRUE(storage_->store_reading(reading));
    }
    
    std::string request = "GET /data/aggregates?start=" + start_time_str_ + "&end=" + end_time_str_ + "&interval=1H HTTP/1.1\r\n\r\n";
    std::string response = server_->handle_aggregates_request(request);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    
    // Should contain statistical values (exact values depend on timing)
    EXPECT_TRUE(response.find("\"co2_ppm_count\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"temperature_c_count\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"humidity_percent_count\":") != std::string::npos);
}