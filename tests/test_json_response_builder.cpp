#include <gtest/gtest.h>
#include "json_response_builder.hpp"
#include <chrono>
#include <vector>
#include <regex>

using namespace sensor_daemon;

class JsonResponseBuilderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test sensor data
        auto now = std::chrono::system_clock::now();
        
        test_reading1_.timestamp = now;
        test_reading1_.co2_ppm = 410.5f;
        test_reading1_.temperature_c = 22.1f;
        test_reading1_.humidity_percent = 45.2f;
        test_reading1_.quality_flags = 7;
        
        test_reading2_.timestamp = now + std::chrono::minutes(1);
        test_reading2_.co2_ppm = 415.0f;
        test_reading2_.temperature_c = 22.3f;
        test_reading2_.humidity_percent = 44.8f;
        test_reading2_.quality_flags = 7;
        
        // Create test reading with missing values
        test_reading_partial_.timestamp = now + std::chrono::minutes(2);
        test_reading_partial_.co2_ppm = 420.0f;
        // temperature_c and humidity_percent are nullopt
        test_reading_partial_.quality_flags = 1; // Only CO2 valid
        
        // Create test aggregate data
        test_aggregate_.timestamp = now;
        test_aggregate_.co2_ppm = AggregateData::ValueStats(412.5, 410.0, 415.0, 100);
        test_aggregate_.temperature_c = AggregateData::ValueStats(22.2, 21.8, 22.6, 100);
        test_aggregate_.humidity_percent = AggregateData::ValueStats(45.0, 44.0, 46.0, 100);
        
        // Create database info
        test_db_info_.total_records = 1000;
        test_db_info_.database_path = "/var/lib/sensor-daemon/data";
        test_db_info_.earliest_timestamp = now - std::chrono::hours(24);
        test_db_info_.latest_timestamp = now;
        test_db_info_.database_size_bytes = 52428800;
        test_db_info_.is_healthy = true;
        test_db_info_.implementation = "RocksDB via HTTP API";
    }
    
    void TearDown() override {}
    
    SensorData test_reading1_;
    SensorData test_reading2_;
    SensorData test_reading_partial_;
    AggregateData test_aggregate_;
    TimeSeriesStorage::DatabaseInfo test_db_info_;
};

// Test sensor data to JSON conversion
TEST_F(JsonResponseBuilderTest, SensorDataToJsonComplete) {
    std::string json = JsonResponseBuilder::sensor_data_to_json(test_reading1_);
    
    EXPECT_TRUE(json.find("\"co2_ppm\": 410.5") != std::string::npos);
    EXPECT_TRUE(json.find("\"temperature_c\": 22.1") != std::string::npos);
    EXPECT_TRUE(json.find("\"humidity_percent\": 45.2") != std::string::npos);
    EXPECT_TRUE(json.find("\"quality_flags\": 7") != std::string::npos);
    EXPECT_TRUE(json.find("\"timestamp\":") != std::string::npos);
}

TEST_F(JsonResponseBuilderTest, SensorDataToJsonPartial) {
    std::string json = JsonResponseBuilder::sensor_data_to_json(test_reading_partial_);
    
    EXPECT_TRUE(json.find("\"co2_ppm\": 420") != std::string::npos);
    EXPECT_TRUE(json.find("\"temperature_c\": null") != std::string::npos);
    EXPECT_TRUE(json.find("\"humidity_percent\": null") != std::string::npos);
    EXPECT_TRUE(json.find("\"quality_flags\": 1") != std::string::npos);
}

// Test aggregate data to JSON conversion
TEST_F(JsonResponseBuilderTest, AggregateDataToJson) {
    std::string json = JsonResponseBuilder::aggregate_data_to_json(test_aggregate_);
    
    EXPECT_TRUE(json.find("\"co2_ppm_mean\": 412.5") != std::string::npos);
    EXPECT_TRUE(json.find("\"co2_ppm_min\": 410") != std::string::npos);
    EXPECT_TRUE(json.find("\"co2_ppm_max\": 415") != std::string::npos);
    EXPECT_TRUE(json.find("\"co2_ppm_count\": 100") != std::string::npos);
    
    EXPECT_TRUE(json.find("\"temperature_c_mean\": 22.2") != std::string::npos);
    EXPECT_TRUE(json.find("\"humidity_percent_mean\": 45") != std::string::npos);
}

TEST_F(JsonResponseBuilderTest, AggregateDataToJsonNoData) {
    AggregateData empty_aggregate;
    empty_aggregate.timestamp = std::chrono::system_clock::now();
    // All ValueStats have has_data = false by default
    
    std::string json = JsonResponseBuilder::aggregate_data_to_json(empty_aggregate);
    
    EXPECT_TRUE(json.find("\"co2_ppm_mean\": null") != std::string::npos);
    EXPECT_TRUE(json.find("\"co2_ppm_count\": 0") != std::string::npos);
    EXPECT_TRUE(json.find("\"temperature_c_mean\": null") != std::string::npos);
    EXPECT_TRUE(json.find("\"humidity_percent_mean\": null") != std::string::npos);
}

// Test readings response
TEST_F(JsonResponseBuilderTest, CreateReadingsResponse) {
    std::vector<SensorData> readings = {test_reading1_, test_reading2_};
    std::string response = JsonResponseBuilder::create_readings_response(readings);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 2") != std::string::npos);
    EXPECT_TRUE(response.find("410.5") != std::string::npos);
    EXPECT_TRUE(response.find("415") != std::string::npos);
}

TEST_F(JsonResponseBuilderTest, CreateReadingsResponseEmpty) {
    std::vector<SensorData> readings;
    std::string response = JsonResponseBuilder::create_readings_response(readings);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"readings\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 0") != std::string::npos);
}

// Test range response
TEST_F(JsonResponseBuilderTest, CreateRangeResponse) {
    std::vector<SensorData> readings = {test_reading1_};
    std::string start_time = "2024-01-01T12:00:00Z";
    std::string end_time = "2024-01-01T13:00:00Z";
    
    std::string response = JsonResponseBuilder::create_range_response(readings, start_time, end_time);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"2024-01-01T12:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"2024-01-01T13:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_count\": 1") != std::string::npos);
}

// Test aggregates response
TEST_F(JsonResponseBuilderTest, CreateAggregatesResponse) {
    std::vector<AggregateData> aggregates = {test_aggregate_};
    std::string start_time = "2024-01-01T00:00:00Z";
    std::string end_time = "2024-01-02T00:00:00Z";
    std::string interval = "1H";
    
    std::string response = JsonResponseBuilder::create_aggregates_response(aggregates, start_time, end_time, interval);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"aggregates\": [") != std::string::npos);
    EXPECT_TRUE(response.find("\"start_time\": \"2024-01-01T00:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"end_time\": \"2024-01-02T00:00:00Z\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"interval\": \"1H\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_intervals\": 1") != std::string::npos);
}

// Test info response
TEST_F(JsonResponseBuilderTest, CreateInfoResponse) {
    std::string response = JsonResponseBuilder::create_info_response(test_db_info_);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_records\": 1000") != std::string::npos);
    EXPECT_TRUE(response.find("\"database_path\": \"/var/lib/sensor-daemon/data\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"database_size_bytes\": 52428800") != std::string::npos);
    EXPECT_TRUE(response.find("\"implementation\": \"RocksDB via HTTP API\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"healthy\": true") != std::string::npos);
}

// Test error responses
TEST_F(JsonResponseBuilderTest, CreateErrorResponseBasic) {
    std::string response = JsonResponseBuilder::create_error_response(400, "Invalid parameter");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid parameter\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"status_code\": 400") != std::string::npos);
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
}

TEST_F(JsonResponseBuilderTest, CreateErrorResponseWithDetails) {
    std::string response = JsonResponseBuilder::create_error_response(400, "Invalid parameter", "Count must be positive");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error\": \"Invalid parameter\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"details\": \"Count must be positive\"") != std::string::npos);
}

// Test integration of get_current_timestamp with error responses
TEST_F(JsonResponseBuilderTest, ErrorResponseContainsCurrentTimestamp) {
    auto before = std::chrono::system_clock::now();
    std::string response = JsonResponseBuilder::create_error_response(500, "Test error");
    auto after = std::chrono::system_clock::now();
    
    // Extract timestamp from response
    size_t timestamp_pos = response.find("\"timestamp\": \"");
    EXPECT_NE(timestamp_pos, std::string::npos);
    
    size_t start = timestamp_pos + 14; // Length of "\"timestamp\": \""
    size_t end = response.find("\"", start);
    EXPECT_NE(end, std::string::npos);
    
    std::string timestamp = response.substr(start, end - start);
    
    // Verify timestamp format
    EXPECT_TRUE(std::regex_match(timestamp, std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{3})?Z)")));
    
    // Parse timestamp and verify it's within reasonable bounds
    auto parsed_time = Iso8601Parser::parse(timestamp);
    EXPECT_TRUE(parsed_time.has_value());
    EXPECT_GE(parsed_time.value(), before - std::chrono::seconds(1));
    EXPECT_LE(parsed_time.value(), after + std::chrono::seconds(1));
}

TEST_F(JsonResponseBuilderTest, CreateErrorResponseDifferentCodes) {
    std::string response500 = JsonResponseBuilder::create_error_response(500, "Internal error");
    EXPECT_TRUE(response500.find("HTTP/1.1 500 Internal Server Error") != std::string::npos);
    
    std::string response503 = JsonResponseBuilder::create_error_response(503, "Service unavailable");
    EXPECT_TRUE(response503.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
}

// Test utility functions
TEST_F(JsonResponseBuilderTest, EscapeJsonString) {
    // Test basic escaping
    std::string escaped = JsonResponseBuilder::escape_json_string("Hello \"World\"");
    EXPECT_EQ(escaped, "Hello \\\"World\\\"");
    
    // Test newline escaping
    escaped = JsonResponseBuilder::escape_json_string("Line 1\nLine 2");
    EXPECT_EQ(escaped, "Line 1\\nLine 2");
    
    // Test backslash escaping
    escaped = JsonResponseBuilder::escape_json_string("Path\\to\\file");
    EXPECT_EQ(escaped, "Path\\\\to\\\\file");
}

TEST_F(JsonResponseBuilderTest, FormatJsonNumber) {
    EXPECT_EQ(JsonResponseBuilder::format_json_number(123.456, 2), "123.46");
    EXPECT_EQ(JsonResponseBuilder::format_json_number(123.0, 1), "123");
    EXPECT_EQ(JsonResponseBuilder::format_json_number(123.100, 3), "123.1");
    
    // Test special values
    EXPECT_EQ(JsonResponseBuilder::format_json_number(std::numeric_limits<double>::quiet_NaN(), 2), "null");
    EXPECT_EQ(JsonResponseBuilder::format_json_number(std::numeric_limits<double>::infinity(), 2), "null");
}

TEST_F(JsonResponseBuilderTest, TimestampToIso8601) {
    auto now = std::chrono::system_clock::now();
    std::string iso_string = JsonResponseBuilder::timestamp_to_iso8601(now);
    
    EXPECT_TRUE(iso_string.find('T') != std::string::npos);
    EXPECT_TRUE(iso_string.back() == 'Z');
    EXPECT_GE(iso_string.length(), 19); // At least YYYY-MM-DDTHH:MM:SSZ
}

TEST_F(JsonResponseBuilderTest, GetCurrentTimestamp) {
    std::string timestamp = JsonResponseBuilder::get_current_timestamp();
    
    EXPECT_TRUE(timestamp.find('T') != std::string::npos);
    EXPECT_TRUE(timestamp.back() == 'Z');
    EXPECT_GE(timestamp.length(), 19);
    
    // Test that it returns a valid ISO 8601 format
    // Format should be: YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS.sssZ
    EXPECT_TRUE(std::regex_match(timestamp, std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{3})?Z)")));
    
    // Test that consecutive calls return different timestamps (or at least not fail)
    std::string timestamp2 = JsonResponseBuilder::get_current_timestamp();
    EXPECT_GE(timestamp2.length(), 19);
    EXPECT_TRUE(timestamp2.back() == 'Z');
}

// Test HTTP header generation
TEST_F(JsonResponseBuilderTest, HttpHeaderGeneration) {
    std::string header = JsonResponseBuilder::create_http_header(200, 100);
    
    EXPECT_TRUE(header.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(header.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(header.find("Content-Length: 100") != std::string::npos);
    EXPECT_TRUE(header.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(header.find("Access-Control-Allow-Origin: *") != std::string::npos);
    EXPECT_TRUE(header.find("\r\n\r\n") != std::string::npos); // Header terminator
}

TEST_F(JsonResponseBuilderTest, HttpHeaderGenerationNoContentLength) {
    std::string header = JsonResponseBuilder::create_http_header(404);
    
    EXPECT_TRUE(header.find("HTTP/1.1 404 Not Found") != std::string::npos);
    EXPECT_TRUE(header.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(header.find("Connection: close") != std::string::npos);
    EXPECT_TRUE(header.find("Access-Control-Allow-Origin: *") != std::string::npos);
    EXPECT_TRUE(header.find("Cache-Control: no-cache") != std::string::npos);
    EXPECT_TRUE(header.find("\r\n\r\n") != std::string::npos); // Header terminator
    // Should not contain Content-Length when content_length is 0
    EXPECT_TRUE(header.find("Content-Length:") == std::string::npos);
}

TEST_F(JsonResponseBuilderTest, HttpHeaderGenerationDifferentStatusCodes) {
    // Test various status codes
    std::string header400 = JsonResponseBuilder::create_http_header(400, 50);
    EXPECT_TRUE(header400.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(header400.find("Content-Length: 50") != std::string::npos);
    
    std::string header429 = JsonResponseBuilder::create_http_header(429, 75);
    EXPECT_TRUE(header429.find("HTTP/1.1 429 Too Many Requests") != std::string::npos);
    EXPECT_TRUE(header429.find("Content-Length: 75") != std::string::npos);
    
    std::string header500 = JsonResponseBuilder::create_http_header(500, 25);
    EXPECT_TRUE(header500.find("HTTP/1.1 500 Internal Server Error") != std::string::npos);
    EXPECT_TRUE(header500.find("Content-Length: 25") != std::string::npos);
    
    std::string header503 = JsonResponseBuilder::create_http_header(503, 30);
    EXPECT_TRUE(header503.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(header503.find("Content-Length: 30") != std::string::npos);
}