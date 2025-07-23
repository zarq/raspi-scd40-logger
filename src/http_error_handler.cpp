#include "http_error_handler.hpp"
#include <sstream>
#include <chrono>
#include <random>
#include <iomanip>
#include <iostream>

namespace sensor_daemon {

std::string HttpErrorHandler::create_parameter_error(
    const std::string& parameter_name,
    const std::string& provided_value,
    const std::string& expected_format) {
    
    ErrorDetails details(ErrorCodes::INVALID_PARAMETER, "Invalid parameter value");
    details.with_details("Parameter '" + parameter_name + "' has invalid value")
           .with_context("parameter", parameter_name);
    
    if (!provided_value.empty()) {
        details.with_context("provided_value", provided_value);
    }
    
    if (!expected_format.empty()) {
        details.with_suggestion("Use format: " + expected_format);
        details.with_context("expected_format", expected_format);
    }
    
    return create_detailed_error(HttpErrorCategory::PARAMETER_VALIDATION, details);
}

std::string HttpErrorHandler::create_missing_parameters_error(
    const std::vector<std::string>& missing_parameters) {
    
    ErrorDetails details(ErrorCodes::MISSING_PARAMETER, "Required parameters are missing");
    
    std::ostringstream missing_list;
    for (size_t i = 0; i < missing_parameters.size(); ++i) {
        if (i > 0) missing_list << ", ";
        missing_list << "'" << missing_parameters[i] << "'";
    }
    
    details.with_details("Missing required parameters: " + missing_list.str())
           .with_suggestion("Include all required parameters in the request")
           .with_context("missing_count", std::to_string(missing_parameters.size()));
    
    return create_detailed_error(HttpErrorCategory::PARAMETER_VALIDATION, details);
}

std::string HttpErrorHandler::create_storage_unavailable_error(const std::string& reason) {
    ErrorDetails details(ErrorCodes::STORAGE_UNAVAILABLE, "Data storage is currently unavailable");
    
    if (!reason.empty()) {
        details.with_details(reason);
    }
    
    details.with_suggestion("Try again later or contact system administrator")
           .with_context("service", "time_series_storage");
    
    return create_detailed_error(HttpErrorCategory::SERVICE_UNAVAILABLE, details);
}

std::string HttpErrorHandler::create_rate_limit_error(int retry_after_seconds) {
    ErrorDetails details(ErrorCodes::RATE_LIMITED, "Request rate limit exceeded");
    details.with_details("Too many requests in a short time period")
           .with_suggestion("Wait " + std::to_string(retry_after_seconds) + " seconds before retrying")
           .with_context("retry_after", std::to_string(retry_after_seconds));
    
    return create_detailed_error(HttpErrorCategory::RATE_LIMITING, details);
}

std::string HttpErrorHandler::create_internal_error(
    const std::string& operation,
    const std::string& error_id) {
    
    std::string id = error_id.empty() ? generate_error_id() : error_id;
    
    ErrorDetails details(ErrorCodes::INTERNAL_ERROR, "An internal server error occurred");
    details.with_details("Unexpected error during request processing")
           .with_suggestion("Try again later or contact support with error ID: " + id)
           .with_context("error_id", id);
    
    if (!operation.empty()) {
        details.with_context("operation", operation);
    }
    
    return create_detailed_error(HttpErrorCategory::INTERNAL_ERROR, details);
}

std::string HttpErrorHandler::create_not_found_error(
    const std::string& resource_type,
    const std::string& resource_id) {
    
    ErrorDetails details(ErrorCodes::ENDPOINT_NOT_FOUND, "Requested resource not found");
    details.with_details("The requested " + resource_type + " was not found")
           .with_suggestion("Check the URL path and try again")
           .with_context("resource_type", resource_type);
    
    if (!resource_id.empty()) {
        details.with_context("resource_id", resource_id);
    }
    
    return create_detailed_error(HttpErrorCategory::RESOURCE_NOT_FOUND, details);
}

std::string HttpErrorHandler::create_method_not_allowed_error(
    const std::string& method,
    const std::vector<std::string>& allowed_methods) {
    
    ErrorDetails details(ErrorCodes::METHOD_NOT_ALLOWED, "HTTP method not allowed");
    
    std::ostringstream allowed_list;
    for (size_t i = 0; i < allowed_methods.size(); ++i) {
        if (i > 0) allowed_list << ", ";
        allowed_list << allowed_methods[i];
    }
    
    details.with_details("Method '" + method + "' is not allowed for this endpoint")
           .with_suggestion("Use one of: " + allowed_list.str())
           .with_context("provided_method", method)
           .with_context("allowed_methods", allowed_list.str());
    
    return create_detailed_error(HttpErrorCategory::METHOD_NOT_ALLOWED, details);
}

std::string HttpErrorHandler::create_detailed_error(
    HttpErrorCategory category,
    const ErrorDetails& details) {
    
    int status_code = get_status_code(category);
    
    return create_enhanced_error_response(
        status_code,
        details.error_code,
        details.user_message,
        details.technical_details,
        details.suggested_action,
        details.context
    );
}

int HttpErrorHandler::get_status_code(HttpErrorCategory category) {
    switch (category) {
        case HttpErrorCategory::PARAMETER_VALIDATION:
            return HttpStatus::BAD_REQUEST;
        case HttpErrorCategory::AUTHENTICATION:
            return 401; // Unauthorized
        case HttpErrorCategory::AUTHORIZATION:
            return 403; // Forbidden
        case HttpErrorCategory::RESOURCE_NOT_FOUND:
            return HttpStatus::NOT_FOUND;
        case HttpErrorCategory::METHOD_NOT_ALLOWED:
            return 405; // Method Not Allowed
        case HttpErrorCategory::RATE_LIMITING:
            return HttpStatus::TOO_MANY_REQUESTS;
        case HttpErrorCategory::INTERNAL_ERROR:
            return HttpStatus::INTERNAL_SERVER_ERROR;
        case HttpErrorCategory::SERVICE_UNAVAILABLE:
            return HttpStatus::SERVICE_UNAVAILABLE;
        case HttpErrorCategory::GATEWAY_TIMEOUT:
            return 504; // Gateway Timeout
        default:
            return HttpStatus::INTERNAL_SERVER_ERROR;
    }
}

std::vector<std::string> HttpErrorHandler::get_troubleshooting_suggestions(const std::string& error_type) {
    if (error_type == "INVALID_TIME_FORMAT") {
        return {
            "Use ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ",
            "Example: 2024-01-01T12:00:00Z",
            "Ensure the timestamp ends with 'Z' for UTC"
        };
    } else if (error_type == "INVALID_INTERVAL") {
        return {
            "Use format: number + unit (T=minutes, H=hours, D=days)",
            "Examples: 1T, 30T, 1H, 2H, 1D",
            "Supported units: T (minutes), H (hours), D (days), M (months)"
        };
    } else if (error_type == "STORAGE_UNAVAILABLE") {
        return {
            "Check if the sensor daemon is running",
            "Verify database permissions and disk space",
            "Contact system administrator if problem persists"
        };
    } else if (error_type == "RATE_LIMITED") {
        return {
            "Reduce request frequency",
            "Implement exponential backoff in your client",
            "Consider caching responses to reduce API calls"
        };
    }
    
    return {"Contact support for assistance"};
}

void HttpErrorHandler::log_error(
    HttpErrorCategory category,
    const ErrorDetails& details,
    const std::string& request_info) {
    
    std::ostringstream log_message;
    log_message << "HTTP Error [" << get_category_name(category) << "] "
                << details.error_code << ": " << details.user_message;
    
    if (!details.technical_details.empty()) {
        log_message << " | Details: " << details.technical_details;
    }
    
    if (!request_info.empty()) {
        log_message << " | Request: " << request_info;
    }
    
    // Log context information
    for (const auto& [key, value] : details.context) {
        log_message << " | " << key << "=" << value;
    }
    
    // Use appropriate log level based on category
    if (category == HttpErrorCategory::INTERNAL_ERROR) {
        std::cerr << "ERROR: " << log_message.str() << std::endl;
    } else if (category == HttpErrorCategory::SERVICE_UNAVAILABLE) {
        std::cerr << "WARN: " << log_message.str() << std::endl;
    } else {
        std::cerr << "INFO: " << log_message.str() << std::endl;
    }
}

std::string HttpErrorHandler::generate_error_id() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::ostringstream id;
    id << "ERR-" << std::put_time(std::gmtime(&time_t), "%Y%m%d%H%M%S") 
       << "-" << dis(gen);
    
    return id.str();
}

std::string HttpErrorHandler::get_category_name(HttpErrorCategory category) {
    switch (category) {
        case HttpErrorCategory::PARAMETER_VALIDATION: return "PARAMETER_VALIDATION";
        case HttpErrorCategory::AUTHENTICATION: return "AUTHENTICATION";
        case HttpErrorCategory::AUTHORIZATION: return "AUTHORIZATION";
        case HttpErrorCategory::RESOURCE_NOT_FOUND: return "RESOURCE_NOT_FOUND";
        case HttpErrorCategory::METHOD_NOT_ALLOWED: return "METHOD_NOT_ALLOWED";
        case HttpErrorCategory::RATE_LIMITING: return "RATE_LIMITING";
        case HttpErrorCategory::INTERNAL_ERROR: return "INTERNAL_ERROR";
        case HttpErrorCategory::SERVICE_UNAVAILABLE: return "SERVICE_UNAVAILABLE";
        case HttpErrorCategory::GATEWAY_TIMEOUT: return "GATEWAY_TIMEOUT";
        default: return "UNKNOWN";
    }
}

std::string HttpErrorHandler::create_enhanced_error_response(
    int status_code,
    const std::string& error_code,
    const std::string& user_message,
    const std::string& technical_details,
    const std::string& suggested_action,
    const std::unordered_map<std::string, std::string>& context) {
    
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"error\": \"" << JsonResponseBuilder::escape_json_string(user_message) << "\",\n";
    json << "  \"error_code\": \"" << error_code << "\",\n";
    
    if (!technical_details.empty()) {
        json << "  \"details\": \"" << JsonResponseBuilder::escape_json_string(technical_details) << "\",\n";
    }
    
    if (!suggested_action.empty()) {
        json << "  \"suggestion\": \"" << JsonResponseBuilder::escape_json_string(suggested_action) << "\",\n";
    }
    
    // Add context information
    if (!context.empty()) {
        json << "  \"context\": {\n";
        size_t i = 0;
        for (const auto& [key, value] : context) {
            if (i > 0) json << ",\n";
            json << "    \"" << key << "\": \"" << JsonResponseBuilder::escape_json_string(value) << "\"";
            ++i;
        }
        json << "\n  },\n";
    }
    
    json << "  \"timestamp\": \"" << JsonResponseBuilder::get_current_timestamp() << "\",\n";
    json << "  \"status_code\": " << status_code << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return JsonResponseBuilder::create_http_header(status_code, json_body.length()) + json_body;
}

} // namespace sensor_daemon