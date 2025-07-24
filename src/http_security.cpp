#include "http_security.hpp"
#include <regex>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace sensor_daemon {

// InputValidator implementation
ValidationResult InputValidator::validate_request(const std::string& request) {
    // Check request size
    auto size_result = validate_request_size(request.size());
    if (!size_result.is_valid) {
        return size_result;
    }
    
    // Extract and validate parameters
    size_t query_start = request.find('?');
    if (query_start == std::string::npos) {
        return ValidationResult::success(); // No parameters to validate
    }
    
    size_t query_end = request.find(' ', query_start);
    if (query_end == std::string::npos) {
        query_end = request.find('\r', query_start);
    }
    if (query_end == std::string::npos) {
        query_end = request.length();
    }
    
    std::string query_string = request.substr(query_start + 1, query_end - query_start - 1);
    
    // Parse and validate each parameter
    std::istringstream stream(query_string);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t equals_pos = pair.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = pair.substr(0, equals_pos);
            std::string value = pair.substr(equals_pos + 1);
            
            auto param_result = validate_parameter(key, value);
            if (!param_result.is_valid) {
                return param_result;
            }
        }
    }
    
    return ValidationResult::success();
}

ValidationResult InputValidator::validate_parameter(const std::string& parameter_name, 
                                                  const std::string& parameter_value) {
    // Check for injection attacks
    if (contains_sql_injection(parameter_value)) {
        return ValidationResult::failure(
            "Invalid parameter value",
            "Parameter '" + parameter_name + "' contains suspicious SQL patterns",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    if (contains_xss_patterns(parameter_value)) {
        return ValidationResult::failure(
            "Invalid parameter value",
            "Parameter '" + parameter_name + "' contains suspicious script patterns",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    if (contains_path_traversal(parameter_value)) {
        return ValidationResult::failure(
            "Invalid parameter value",
            "Parameter '" + parameter_name + "' contains path traversal patterns",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    if (contains_command_injection(parameter_value)) {
        return ValidationResult::failure(
            "Invalid parameter value",
            "Parameter '" + parameter_name + "' contains command injection patterns",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    // Check parameter length
    if (parameter_value.length() > 1000) {
        return ValidationResult::failure(
            "Parameter too long",
            "Parameter '" + parameter_name + "' exceeds maximum length of 1000 characters",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    return ValidationResult::success();
}

ValidationResult InputValidator::validate_count(int count, int max_allowed) {
    if (count < 1) {
        return ValidationResult::failure(
            "Invalid count parameter",
            "Count must be a positive integer (minimum: 1)",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    if (count > max_allowed) {
        return ValidationResult::failure(
            "Count parameter too large",
            "Count exceeds maximum allowed value of " + std::to_string(max_allowed),
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    return ValidationResult::success();
}

ValidationResult InputValidator::validate_time_range(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    int max_range_hours) {
    
    if (start > end) {
        return ValidationResult::failure(
            "Invalid time range",
            "Start time must be before end time",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    auto duration = end - start;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    
    if (hours.count() > max_range_hours) {
        return ValidationResult::failure(
            "Time range too large",
            "Time range exceeds maximum allowed duration of " + std::to_string(max_range_hours) + " hours",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    // Check if times are reasonable (not too far in the future)
    auto now = std::chrono::system_clock::now();
    auto future_limit = now + std::chrono::hours(24);
    
    if (start > future_limit || end > future_limit) {
        return ValidationResult::failure(
            "Invalid time range",
            "Time range cannot be more than 24 hours in the future",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    return ValidationResult::success();
}

ValidationResult InputValidator::validate_request_size(size_t request_size, size_t max_size) {
    if (request_size > max_size) {
        return ValidationResult::failure(
            "Request too large",
            "Request size (" + std::to_string(request_size) + " bytes) exceeds maximum allowed size of " + std::to_string(max_size) + " bytes",
            HttpErrorCategory::PARAMETER_VALIDATION
        );
    }
    
    return ValidationResult::success();
}

bool InputValidator::contains_sql_injection(const std::string& value) {
    // Convert to lowercase for case-insensitive matching
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    // Common SQL injection patterns
    std::vector<std::string> sql_patterns = {
        "union select", "drop table", "delete from", "insert into",
        "update set", "alter table", "create table", "exec(",
        "execute(", "sp_", "xp_", "/*", "*/", "--", "';",
        "or 1=1", "and 1=1", "' or '", "\" or \"",
        "union all", "information_schema", "sysobjects"
    };
    
    for (const auto& pattern : sql_patterns) {
        if (lower_value.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

bool InputValidator::contains_xss_patterns(const std::string& value) {
    // Convert to lowercase for case-insensitive matching
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    // Common XSS patterns
    std::vector<std::string> xss_patterns = {
        "<script", "</script>", "javascript:", "vbscript:",
        "onload=", "onerror=", "onclick=", "onmouseover=",
        "alert(", "confirm(", "prompt(", "document.cookie",
        "window.location", "eval(", "expression(",
        "<iframe", "<object", "<embed", "<applet"
    };
    
    for (const auto& pattern : xss_patterns) {
        if (lower_value.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

bool InputValidator::contains_path_traversal(const std::string& value) {
    // Path traversal patterns
    std::vector<std::string> traversal_patterns = {
        "../", "..\\", "..%2f", "..%5c",
        "%2e%2e%2f", "%2e%2e%5c", "....//",
        "....\\\\", "/etc/passwd", "/etc/shadow",
        "c:\\windows", "c:/windows"
    };
    
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
    
    for (const auto& pattern : traversal_patterns) {
        if (lower_value.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

bool InputValidator::contains_command_injection(const std::string& value) {
    // Command injection patterns
    std::vector<std::string> command_patterns = {
        ";", "|", "&", "`", "$(",
        "$(", "&&", "||", ">>", "<<",
        "cat ", "ls ", "pwd", "whoami",
        "rm ", "del ", "format ", "shutdown"
    };
    
    for (const auto& pattern : command_patterns) {
        if (value.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// RateLimiter implementation
RateLimiter::RateLimiter(const RateLimitConfig& config) : config_(config) {}

bool RateLimiter::is_request_allowed(const std::string& client_ip) {
    if (!config_.enabled) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto& client_info = clients_[client_ip];
    
    // Check if window has expired
    if (is_window_expired(client_info)) {
        client_info.request_count = 0;
        client_info.window_start = now;
    }
    
    // Check rate limits
    if (client_info.request_count >= config_.requests_per_minute) {
        return false;
    }
    
    // Allow request
    client_info.request_count++;
    client_info.last_request = now;
    
    // Cleanup old entries periodically
    if (clients_.size() > 1000) {
        cleanup_old_entries();
    }
    
    return true;
}

int RateLimiter::get_remaining_requests(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = clients_.find(client_ip);
    if (it == clients_.end()) {
        return config_.requests_per_minute;
    }
    
    if (is_window_expired(it->second)) {
        return config_.requests_per_minute;
    }
    
    return std::max(0, config_.requests_per_minute - it->second.request_count);
}

std::chrono::seconds RateLimiter::get_reset_time(const std::string& client_ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = clients_.find(client_ip);
    if (it == clients_.end()) {
        return std::chrono::seconds(0);
    }
    
    auto window_end = it->second.window_start + config_.window_size;
    auto now = std::chrono::steady_clock::now();
    
    if (now >= window_end) {
        return std::chrono::seconds(0);
    }
    
    return std::chrono::duration_cast<std::chrono::seconds>(window_end - now);
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.clear();
}

void RateLimiter::cleanup_old_entries() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::hours(1);
    
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (it->second.last_request < cutoff) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

bool RateLimiter::is_window_expired(const ClientInfo& client_info) const {
    auto now = std::chrono::steady_clock::now();
    return (now - client_info.window_start) >= config_.window_size;
}

// PerformanceMonitor implementation
std::unordered_map<std::string, PerformanceMonitor::EndpointStats> PerformanceMonitor::stats_;
std::mutex PerformanceMonitor::stats_mutex_;

void PerformanceMonitor::record_query(const std::string& endpoint, 
                                     std::chrono::milliseconds duration,
                                     size_t result_count) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto& stats = stats_[endpoint];
    stats.total_requests++;
    stats.total_duration_ms += duration.count();
    stats.total_results += result_count;
    stats.last_request = std::chrono::steady_clock::now();
}

std::chrono::milliseconds PerformanceMonitor::get_average_response_time(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto it = stats_.find(endpoint);
    if (it == stats_.end() || it->second.total_requests == 0) {
        return std::chrono::milliseconds(0);
    }
    
    uint64_t avg_ms = it->second.total_duration_ms / it->second.total_requests;
    return std::chrono::milliseconds(avg_ms);
}

std::string PerformanceMonitor::get_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    std::ostringstream json;
    json << "{\n";
    json << "  \"endpoints\": {\n";
    
    size_t i = 0;
    for (const auto& [endpoint, stats] : stats_) {
        if (i > 0) json << ",\n";
        
        uint64_t avg_ms = stats.total_requests > 0 ? 
            stats.total_duration_ms / stats.total_requests : 0;
        
        json << "    \"" << endpoint << "\": {\n";
        json << "      \"total_requests\": " << stats.total_requests << ",\n";
        json << "      \"average_response_time_ms\": " << avg_ms << ",\n";
        json << "      \"total_results\": " << stats.total_results << "\n";
        json << "    }";
        
        ++i;
    }
    
    json << "\n  }\n";
    json << "}\n";
    
    return json.str();
}

bool PerformanceMonitor::should_allow_query(const std::string& endpoint, 
                                           std::chrono::milliseconds estimated_duration) {
    // Allow queries under 30 seconds
    return estimated_duration < std::chrono::seconds(30);
}

// SecurityManager implementation
SecurityManager::SecurityManager(const SecurityConfig& config) 
    : config_(config), rate_limiter_(std::make_unique<RateLimiter>(config.rate_limit)) {}

ValidationResult SecurityManager::validate_request(const std::string& request, const std::string& client_ip) {
    // Check rate limiting first
    auto rate_result = check_rate_limit(client_ip);
    if (!rate_result.is_valid) {
        return rate_result;
    }
    
    // Validate request content
    return InputValidator::validate_request(request);
}

ValidationResult SecurityManager::check_rate_limit(const std::string& client_ip) {
    if (!rate_limiter_->is_request_allowed(client_ip)) {
        auto reset_time = rate_limiter_->get_reset_time(client_ip);
        
        return ValidationResult::failure(
            "Rate limit exceeded",
            "Too many requests from client IP: " + client_ip,
            HttpErrorCategory::RATE_LIMITING
        );
    }
    
    return ValidationResult::success();
}

std::chrono::steady_clock::time_point SecurityManager::start_request_monitoring(const std::string& endpoint) {
    return std::chrono::steady_clock::now();
}

void SecurityManager::end_request_monitoring(const std::string& endpoint,
                                           std::chrono::steady_clock::time_point start_time,
                                           size_t result_count) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PerformanceMonitor::record_query(endpoint, duration, result_count);
}

std::string SecurityManager::get_statistics() const {
    return PerformanceMonitor::get_statistics();
}

} // namespace sensor_daemon