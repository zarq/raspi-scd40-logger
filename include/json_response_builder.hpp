#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "sensor_data.hpp"
#include "time_series_storage.hpp"
#include "http_utils.hpp"

namespace sensor_daemon {

/**
 * Aggregate data structure for statistical summaries
 */
struct AggregateData {
    std::chrono::system_clock::time_point timestamp;
    
    struct ValueStats {
        double mean = 0.0;
        double min = 0.0;
        double max = 0.0;
        uint64_t count = 0;
        bool has_data = false;
        
        ValueStats() = default;
        ValueStats(double m, double mn, double mx, uint64_t c) 
            : mean(m), min(mn), max(mx), count(c), has_data(true) {}
    };
    
    ValueStats co2_ppm;
    ValueStats temperature_c;
    ValueStats humidity_percent;
    
    AggregateData() = default;
    explicit AggregateData(std::chrono::system_clock::time_point ts) : timestamp(ts) {}
};

/**
 * JSON response builder for HTTP API endpoints
 */
class JsonResponseBuilder {
public:
    /**
     * Create JSON response for recent readings endpoint
     * @param readings Vector of sensor readings
     * @return Complete HTTP response with JSON body
     */
    static std::string create_readings_response(const std::vector<SensorData>& readings);
    
    /**
     * Create JSON response for range query endpoint
     * @param readings Vector of sensor readings
     * @param start_time Start time string (ISO 8601)
     * @param end_time End time string (ISO 8601)
     * @return Complete HTTP response with JSON body
     */
    static std::string create_range_response(const std::vector<SensorData>& readings,
                                           const std::string& start_time,
                                           const std::string& end_time);
    
    /**
     * Create JSON response for aggregates endpoint
     * @param aggregates Vector of aggregate data
     * @param start_time Start time string (ISO 8601)
     * @param end_time End time string (ISO 8601)
     * @param interval Interval string (e.g., "1H")
     * @return Complete HTTP response with JSON body
     */
    static std::string create_aggregates_response(const std::vector<AggregateData>& aggregates,
                                                const std::string& start_time,
                                                const std::string& end_time,
                                                const std::string& interval);
    
    /**
     * Create JSON response for database info endpoint
     * @param info Database information structure
     * @return Complete HTTP response with JSON body
     */
    static std::string create_info_response(const TimeSeriesStorage::DatabaseInfo& info);
    
    /**
     * Create error response with appropriate HTTP status code
     * @param status_code HTTP status code (400, 500, 503, etc.)
     * @param error_msg Main error message
     * @param details Optional detailed error information
     * @return Complete HTTP error response with JSON body
     */
    static std::string create_error_response(int status_code, 
                                           const std::string& error_msg,
                                           const std::string& details = "");
    
    /**
     * Convert sensor data to JSON object string
     * @param reading Sensor reading to convert
     * @return JSON object string
     */
    static std::string sensor_data_to_json(const SensorData& reading);
    
    /**
     * Convert aggregate data to JSON object string
     * @param aggregate Aggregate data to convert
     * @return JSON object string
     */
    static std::string aggregate_data_to_json(const AggregateData& aggregate);
    
    /**
     * Convert timestamp to ISO 8601 string
     * @param timestamp Time point to convert
     * @return ISO 8601 formatted string
     */
    static std::string timestamp_to_iso8601(const std::chrono::system_clock::time_point& timestamp);
    
    /**
     * Get current timestamp as ISO 8601 string
     * @return Current time in ISO 8601 format
     */
    static std::string get_current_timestamp();
    
    /**
     * Create HTTP response header
     * @param status_code HTTP status code
     * @param content_length Length of content body
     * @return HTTP response header string
     */
    static std::string create_http_header(int status_code, size_t content_length = 0);

private:
    
    /**
     * Get HTTP status text for status code
     * @param status_code HTTP status code
     * @return Status text (e.g., "OK", "Bad Request")
     */
    static std::string get_status_text(int status_code);
    
    /**
     * Escape string for JSON
     * @param str String to escape
     * @return JSON-escaped string
     */
    static std::string escape_json_string(const std::string& str);
    
    /**
     * Format double value for JSON (handles NaN, infinity)
     * @param value Double value to format
     * @param precision Number of decimal places
     * @return JSON-formatted number string
     */
    static std::string format_json_number(double value, int precision = 6);
    
    /**
     * Create JSON array from vector of JSON objects
     * @param json_objects Vector of JSON object strings
     * @return JSON array string
     */
    static std::string create_json_array(const std::vector<std::string>& json_objects);
};

/**
 * HTTP status code constants
 */
namespace HttpStatus {
    constexpr int OK = 200;
    constexpr int BAD_REQUEST = 400;
    constexpr int NOT_FOUND = 404;
    constexpr int TOO_MANY_REQUESTS = 429;
    constexpr int INTERNAL_SERVER_ERROR = 500;
    constexpr int SERVICE_UNAVAILABLE = 503;
}

} // namespace sensor_daemon