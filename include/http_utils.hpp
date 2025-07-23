#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <unordered_map>

namespace sensor_daemon {

/**
 * Structure to hold parsed URL query parameters
 */
struct QueryParameters {
    std::optional<int> count;
    std::optional<std::string> start_time;
    std::optional<std::string> end_time;
    std::optional<std::string> interval;
    
    /**
     * Parse URL parameters from HTTP request string
     * @param request Full HTTP request string
     * @return Parsed query parameters
     */
    static QueryParameters parse_url_parameters(const std::string& request);
    
    /**
     * Parse ISO 8601 timestamp string to time_point
     * @param time_str ISO 8601 formatted time string
     * @return Parsed time point, or nullopt if parsing fails
     */
    std::optional<std::chrono::system_clock::time_point> parse_iso8601(const std::string& time_str) const;
    
    /**
     * Validate count parameter
     * @return true if count is valid (positive integer)
     */
    bool is_count_valid() const;
    
    /**
     * Validate time range parameters
     * @return true if start and end times are valid and start <= end
     */
    bool is_time_range_valid() const;
    
    /**
     * Validate interval parameter
     * @return true if interval format is supported
     */
    bool is_interval_valid() const;
};

/**
 * HTTP parameter parsing utilities
 */
class HttpParameterParser {
public:
    /**
     * Extract query string from HTTP request
     * @param request Full HTTP request string
     * @return Query string portion, or empty string if not found
     */
    static std::string extract_query_string(const std::string& request);
    
    /**
     * Parse query string into key-value pairs
     * @param query_string URL query string (without '?')
     * @return Map of parameter names to values
     */
    static std::unordered_map<std::string, std::string> parse_query_string(const std::string& query_string);
    
    /**
     * URL decode a string
     * @param encoded URL-encoded string
     * @return Decoded string
     */
    static std::string url_decode(const std::string& encoded);
    
    /**
     * Extract HTTP method and path from request
     * @param request Full HTTP request string
     * @return Pair of (method, path) or empty strings if parsing fails
     */
    static std::pair<std::string, std::string> extract_method_and_path(const std::string& request);
    
private:
    /**
     * Convert hex character to integer
     * @param hex Hex character ('0'-'9', 'A'-'F', 'a'-'f')
     * @return Integer value, or -1 if invalid
     */
    static int hex_to_int(char hex);
};

/**
 * ISO 8601 timestamp parsing utilities
 */
class Iso8601Parser {
public:
    /**
     * Parse ISO 8601 timestamp string
     * Supports formats: YYYY-MM-DDTHH:MM:SSZ, YYYY-MM-DDTHH:MM:SS.sssZ
     * @param time_str ISO 8601 formatted string
     * @return Parsed time point, or nullopt if parsing fails
     */
    static std::optional<std::chrono::system_clock::time_point> parse(const std::string& time_str);
    
    /**
     * Format time point as ISO 8601 string
     * @param time_point Time point to format
     * @return ISO 8601 formatted string
     */
    static std::string format(const std::chrono::system_clock::time_point& time_point);
    
private:
    /**
     * Parse date component (YYYY-MM-DD)
     * @param date_str Date string
     * @param year Output year
     * @param month Output month (1-12)
     * @param day Output day (1-31)
     * @return true if parsing successful
     */
    static bool parse_date(const std::string& date_str, int& year, int& month, int& day);
    
    /**
     * Parse time component (HH:MM:SS or HH:MM:SS.sss)
     * @param time_str Time string
     * @param hour Output hour (0-23)
     * @param minute Output minute (0-59)
     * @param second Output second (0-59)
     * @param millisecond Output millisecond (0-999)
     * @return true if parsing successful
     */
    static bool parse_time(const std::string& time_str, int& hour, int& minute, int& second, int& millisecond);
    
    /**
     * Validate date values
     * @param year Year value
     * @param month Month value (1-12)
     * @param day Day value (1-31)
     * @return true if date is valid
     */
    static bool is_valid_date(int year, int month, int day);
    
    /**
     * Validate time values
     * @param hour Hour value (0-23)
     * @param minute Minute value (0-59)
     * @param second Second value (0-59)
     * @param millisecond Millisecond value (0-999)
     * @return true if time is valid
     */
    static bool is_valid_time(int hour, int minute, int second, int millisecond);
};

/**
 * Parameter validation utilities
 */
class ParameterValidator {
public:
    /**
     * Validate count parameter
     * @param count Count value to validate
     * @param min_value Minimum allowed value (default: 1)
     * @param max_value Maximum allowed value (default: 10000)
     * @return true if count is within valid range
     */
    static bool validate_count(int count, int min_value = 1, int max_value = 10000);
    
    /**
     * Validate time range
     * @param start Start time
     * @param end End time
     * @param max_range_hours Maximum allowed range in hours (default: 24*7 = 1 week)
     * @return true if time range is valid
     */
    static bool validate_time_range(const std::chrono::system_clock::time_point& start,
                                  const std::chrono::system_clock::time_point& end,
                                  int max_range_hours = 24 * 7);
    
    /**
     * Validate interval string
     * @param interval Interval string (e.g., "1H", "30T", "1D")
     * @return true if interval format is supported
     */
    static bool validate_interval(const std::string& interval);
    
    /**
     * Parse interval string to duration
     * @param interval Interval string
     * @return Duration in minutes, or nullopt if parsing fails
     */
    static std::optional<std::chrono::minutes> parse_interval(const std::string& interval);
};

} // namespace sensor_daemon