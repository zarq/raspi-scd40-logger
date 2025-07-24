#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include "http_error_handler.hpp"

namespace sensor_daemon {

/**
 * Request validation result
 */
struct ValidationResult {
    bool is_valid = true;
    std::string error_message;
    std::string error_details;
    HttpErrorCategory error_category = HttpErrorCategory::PARAMETER_VALIDATION;
    
    ValidationResult() = default;
    ValidationResult(bool valid) : is_valid(valid) {}
    
    static ValidationResult success() {
        return ValidationResult(true);
    }
    
    static ValidationResult failure(const std::string& message, 
                                  const std::string& details = "",
                                  HttpErrorCategory category = HttpErrorCategory::PARAMETER_VALIDATION) {
        ValidationResult result(false);
        result.error_message = message;
        result.error_details = details;
        result.error_category = category;
        return result;
    }
};

/**
 * Rate limiting configuration
 */
struct RateLimitConfig {
    int requests_per_minute = 60;
    int requests_per_hour = 1000;
    int burst_limit = 10;
    std::chrono::minutes window_size{1};
    bool enabled = true;
};

/**
 * Security configuration
 */
struct SecurityConfig {
    RateLimitConfig rate_limit;
    int max_query_results = 10000;
    std::chrono::seconds query_timeout{30};
    size_t max_request_size = 8192;  // 8KB
    bool validate_user_agent = false;
    bool require_content_type = false;
    std::vector<std::string> allowed_origins;
    bool enable_request_logging = true;
};

/**
 * Input validator for HTTP requests
 */
class InputValidator {
public:
    /**
     * Validate request parameters for security issues
     * @param request Full HTTP request string
     * @return Validation result
     */
    static ValidationResult validate_request(const std::string& request);
    
    /**
     * Validate parameter values for injection attacks
     * @param parameter_name Name of the parameter
     * @param parameter_value Value to validate
     * @return Validation result
     */
    static ValidationResult validate_parameter(const std::string& parameter_name, 
                                             const std::string& parameter_value);
    
    /**
     * Validate count parameter
     * @param count Count value to validate
     * @param max_allowed Maximum allowed count
     * @return Validation result
     */
    static ValidationResult validate_count(int count, int max_allowed = 10000);
    
    /**
     * Validate time range for reasonable limits
     * @param start Start timestamp
     * @param end End timestamp
     * @param max_range_hours Maximum allowed range in hours
     * @return Validation result
     */
    static ValidationResult validate_time_range(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end,
        int max_range_hours = 24 * 7);
    
    /**
     * Validate request size
     * @param request_size Size of the request in bytes
     * @param max_size Maximum allowed size
     * @return Validation result
     */
    static ValidationResult validate_request_size(size_t request_size, size_t max_size = 8192);

private:
    /**
     * Check for SQL injection patterns
     * @param value Value to check
     * @return true if suspicious patterns found
     */
    static bool contains_sql_injection(const std::string& value);
    
    /**
     * Check for XSS patterns
     * @param value Value to check
     * @return true if suspicious patterns found
     */
    static bool contains_xss_patterns(const std::string& value);
    
    /**
     * Check for path traversal patterns
     * @param value Value to check
     * @return true if suspicious patterns found
     */
    static bool contains_path_traversal(const std::string& value);
    
    /**
     * Check for command injection patterns
     * @param value Value to check
     * @return true if suspicious patterns found
     */
    static bool contains_command_injection(const std::string& value);
};

/**
 * Rate limiter for preventing abuse
 */
class RateLimiter {
public:
    /**
     * Constructor
     * @param config Rate limiting configuration
     */
    explicit RateLimiter(const RateLimitConfig& config);
    
    /**
     * Check if request is allowed for client
     * @param client_ip Client IP address
     * @return true if request is allowed
     */
    bool is_request_allowed(const std::string& client_ip);
    
    /**
     * Get remaining requests for client
     * @param client_ip Client IP address
     * @return Number of remaining requests in current window
     */
    int get_remaining_requests(const std::string& client_ip);
    
    /**
     * Get time until rate limit resets
     * @param client_ip Client IP address
     * @return Seconds until reset
     */
    std::chrono::seconds get_reset_time(const std::string& client_ip);
    
    /**
     * Reset rate limits (for testing)
     */
    void reset();

private:
    struct ClientInfo {
        int request_count = 0;
        std::chrono::steady_clock::time_point window_start;
        std::chrono::steady_clock::time_point last_request;
    };
    
    RateLimitConfig config_;
    std::unordered_map<std::string, ClientInfo> clients_;
    mutable std::mutex mutex_;
    
    /**
     * Clean up old client entries
     */
    void cleanup_old_entries();
    
    /**
     * Check if window has expired for client
     * @param client_info Client information
     * @return true if window has expired
     */
    bool is_window_expired(const ClientInfo& client_info) const;
};

/**
 * Performance monitor for tracking query performance
 */
class PerformanceMonitor {
public:
    /**
     * Record query execution time
     * @param endpoint Endpoint name
     * @param duration Execution duration
     * @param result_count Number of results returned
     */
    static void record_query(const std::string& endpoint, 
                           std::chrono::milliseconds duration,
                           size_t result_count = 0);
    
    /**
     * Get average response time for endpoint
     * @param endpoint Endpoint name
     * @return Average response time in milliseconds
     */
    static std::chrono::milliseconds get_average_response_time(const std::string& endpoint);
    
    /**
     * Get query statistics
     * @return JSON string with performance statistics
     */
    static std::string get_statistics();
    
    /**
     * Check if query timeout should be applied
     * @param endpoint Endpoint name
     * @param estimated_duration Estimated query duration
     * @return true if query should be allowed
     */
    static bool should_allow_query(const std::string& endpoint, 
                                 std::chrono::milliseconds estimated_duration);

private:
    struct EndpointStats {
        std::atomic<uint64_t> total_requests{0};
        std::atomic<uint64_t> total_duration_ms{0};
        std::atomic<uint64_t> total_results{0};
        std::chrono::steady_clock::time_point last_request;
    };
    
    static std::unordered_map<std::string, EndpointStats> stats_;
    static std::mutex stats_mutex_;
};

/**
 * Security manager for coordinating security measures
 */
class SecurityManager {
public:
    /**
     * Initialize security manager
     * @param config Security configuration
     */
    explicit SecurityManager(const SecurityConfig& config);
    
    /**
     * Validate incoming request
     * @param request Full HTTP request
     * @param client_ip Client IP address
     * @return Validation result
     */
    ValidationResult validate_request(const std::string& request, const std::string& client_ip);
    
    /**
     * Check if request should be rate limited
     * @param client_ip Client IP address
     * @return Validation result (failure if rate limited)
     */
    ValidationResult check_rate_limit(const std::string& client_ip);
    
    /**
     * Start performance monitoring for request
     * @param endpoint Endpoint name
     * @return Start time for duration calculation
     */
    std::chrono::steady_clock::time_point start_request_monitoring(const std::string& endpoint);
    
    /**
     * End performance monitoring for request
     * @param endpoint Endpoint name
     * @param start_time Start time from start_request_monitoring
     * @param result_count Number of results returned
     */
    void end_request_monitoring(const std::string& endpoint,
                              std::chrono::steady_clock::time_point start_time,
                              size_t result_count = 0);
    
    /**
     * Get security statistics
     * @return JSON string with security and performance statistics
     */
    std::string get_statistics() const;

private:
    SecurityConfig config_;
    std::unique_ptr<RateLimiter> rate_limiter_;
};

} // namespace sensor_daemon