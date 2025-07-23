#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "json_response_builder.hpp"

namespace sensor_daemon {

/**
 * HTTP error categories for consistent error handling
 */
enum class HttpErrorCategory {
    PARAMETER_VALIDATION,    // 400 - Invalid parameters
    AUTHENTICATION,          // 401 - Authentication required
    AUTHORIZATION,          // 403 - Access forbidden
    RESOURCE_NOT_FOUND,     // 404 - Resource not found
    METHOD_NOT_ALLOWED,     // 405 - HTTP method not allowed
    RATE_LIMITING,          // 429 - Too many requests
    INTERNAL_ERROR,         // 500 - Internal server error
    SERVICE_UNAVAILABLE,    // 503 - Service unavailable
    GATEWAY_TIMEOUT         // 504 - Gateway timeout
};

/**
 * Detailed error information for troubleshooting
 */
struct ErrorDetails {
    std::string error_code;
    std::string user_message;
    std::string technical_details;
    std::string suggested_action;
    std::unordered_map<std::string, std::string> context;
    
    ErrorDetails() = default;
    ErrorDetails(const std::string& code, const std::string& message)
        : error_code(code), user_message(message) {}
    
    ErrorDetails& with_details(const std::string& details) {
        technical_details = details;
        return *this;
    }
    
    ErrorDetails& with_suggestion(const std::string& suggestion) {
        suggested_action = suggestion;
        return *this;
    }
    
    ErrorDetails& with_context(const std::string& key, const std::string& value) {
        context[key] = value;
        return *this;
    }
};

/**
 * HTTP error handler for consistent error responses
 */
class HttpErrorHandler {
public:
    /**
     * Create error response for parameter validation errors
     * @param parameter_name Name of the invalid parameter
     * @param provided_value Value that was provided (optional)
     * @param expected_format Expected format description
     * @return HTTP error response
     */
    static std::string create_parameter_error(
        const std::string& parameter_name,
        const std::string& provided_value = "",
        const std::string& expected_format = "");
    
    /**
     * Create error response for missing required parameters
     * @param missing_parameters List of missing parameter names
     * @return HTTP error response
     */
    static std::string create_missing_parameters_error(
        const std::vector<std::string>& missing_parameters);
    
    /**
     * Create error response for storage unavailable
     * @param reason Reason why storage is unavailable
     * @return HTTP error response
     */
    static std::string create_storage_unavailable_error(const std::string& reason = "");
    
    /**
     * Create error response for rate limiting
     * @param retry_after_seconds Seconds to wait before retrying
     * @return HTTP error response
     */
    static std::string create_rate_limit_error(int retry_after_seconds = 60);
    
    /**
     * Create error response for internal server errors
     * @param operation Operation that failed
     * @param error_id Unique error identifier for tracking
     * @return HTTP error response
     */
    static std::string create_internal_error(
        const std::string& operation = "",
        const std::string& error_id = "");
    
    /**
     * Create error response for resource not found
     * @param resource_type Type of resource that wasn't found
     * @param resource_id Identifier of the resource
     * @return HTTP error response
     */
    static std::string create_not_found_error(
        const std::string& resource_type = "endpoint",
        const std::string& resource_id = "");
    
    /**
     * Create error response for method not allowed
     * @param method HTTP method that was used
     * @param allowed_methods List of allowed methods
     * @return HTTP error response
     */
    static std::string create_method_not_allowed_error(
        const std::string& method,
        const std::vector<std::string>& allowed_methods = {"GET"});
    
    /**
     * Create error response with detailed troubleshooting information
     * @param category Error category
     * @param details Detailed error information
     * @return HTTP error response
     */
    static std::string create_detailed_error(
        HttpErrorCategory category,
        const ErrorDetails& details);
    
    /**
     * Get HTTP status code for error category
     * @param category Error category
     * @return HTTP status code
     */
    static int get_status_code(HttpErrorCategory category);
    
    /**
     * Get troubleshooting suggestions for common errors
     * @param error_type Type of error
     * @return Vector of troubleshooting suggestions
     */
    static std::vector<std::string> get_troubleshooting_suggestions(const std::string& error_type);
    
    /**
     * Log error with appropriate level and context
     * @param category Error category
     * @param details Error details
     * @param request_info Request information for context
     */
    static void log_error(
        HttpErrorCategory category,
        const ErrorDetails& details,
        const std::string& request_info = "");

private:
    /**
     * Generate unique error ID for tracking
     * @return Unique error identifier
     */
    static std::string generate_error_id();
    
    /**
     * Get error category name as string
     * @param category Error category
     * @return Category name
     */
    static std::string get_category_name(HttpErrorCategory category);
    
    /**
     * Create enhanced error response with troubleshooting info
     * @param status_code HTTP status code
     * @param error_code Error code
     * @param user_message User-friendly error message
     * @param technical_details Technical details for debugging
     * @param suggested_action Suggested action to resolve the error
     * @param context Additional context information
     * @return Complete HTTP error response
     */
    static std::string create_enhanced_error_response(
        int status_code,
        const std::string& error_code,
        const std::string& user_message,
        const std::string& technical_details = "",
        const std::string& suggested_action = "",
        const std::unordered_map<std::string, std::string>& context = {});
};

/**
 * Common error codes for consistent error identification
 */
namespace ErrorCodes {
    constexpr const char* INVALID_PARAMETER = "INVALID_PARAMETER";
    constexpr const char* MISSING_PARAMETER = "MISSING_PARAMETER";
    constexpr const char* INVALID_TIME_FORMAT = "INVALID_TIME_FORMAT";
    constexpr const char* INVALID_TIME_RANGE = "INVALID_TIME_RANGE";
    constexpr const char* INVALID_INTERVAL = "INVALID_INTERVAL";
    constexpr const char* STORAGE_UNAVAILABLE = "STORAGE_UNAVAILABLE";
    constexpr const char* STORAGE_UNHEALTHY = "STORAGE_UNHEALTHY";
    constexpr const char* RATE_LIMITED = "RATE_LIMITED";
    constexpr const char* INTERNAL_ERROR = "INTERNAL_ERROR";
    constexpr const char* ENDPOINT_NOT_FOUND = "ENDPOINT_NOT_FOUND";
    constexpr const char* METHOD_NOT_ALLOWED = "METHOD_NOT_ALLOWED";
    constexpr const char* QUERY_TIMEOUT = "QUERY_TIMEOUT";
    constexpr const char* RESULT_TOO_LARGE = "RESULT_TOO_LARGE";
}

} // namespace sensor_daemon