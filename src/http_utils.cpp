#include "http_utils.hpp"
#include <sstream>
#include <regex>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace sensor_daemon {

// QueryParameters implementation
QueryParameters QueryParameters::parse_url_parameters(const std::string& request) {
    QueryParameters params;
    
    std::string query_string = HttpParameterParser::extract_query_string(request);
    if (query_string.empty()) {
        return params;
    }
    
    auto param_map = HttpParameterParser::parse_query_string(query_string);
    
    // Parse count parameter
    auto count_it = param_map.find("count");
    if (count_it != param_map.end()) {
        try {
            int count_value = std::stoi(count_it->second);
            if (ParameterValidator::validate_count(count_value)) {
                params.count = count_value;
            }
        } catch (const std::exception&) {
            // Invalid count value, leave as nullopt
        }
    }
    
    // Parse start time parameter
    auto start_it = param_map.find("start");
    if (start_it != param_map.end()) {
        params.start_time = HttpParameterParser::url_decode(start_it->second);
    }
    
    // Parse end time parameter
    auto end_it = param_map.find("end");
    if (end_it != param_map.end()) {
        params.end_time = HttpParameterParser::url_decode(end_it->second);
    }
    
    // Parse interval parameter
    auto interval_it = param_map.find("interval");
    if (interval_it != param_map.end()) {
        std::string interval_value = HttpParameterParser::url_decode(interval_it->second);
        if (ParameterValidator::validate_interval(interval_value)) {
            params.interval = interval_value;
        }
    }
    
    return params;
}

std::optional<std::chrono::system_clock::time_point> QueryParameters::parse_iso8601(const std::string& time_str) const {
    return Iso8601Parser::parse(time_str);
}

bool QueryParameters::is_count_valid() const {
    return count.has_value() && ParameterValidator::validate_count(count.value());
}

bool QueryParameters::is_time_range_valid() const {
    if (!start_time.has_value() || !end_time.has_value()) {
        return false;
    }
    
    auto start_tp = parse_iso8601(start_time.value());
    auto end_tp = parse_iso8601(end_time.value());
    
    if (!start_tp.has_value() || !end_tp.has_value()) {
        return false;
    }
    
    return ParameterValidator::validate_time_range(start_tp.value(), end_tp.value());
}

bool QueryParameters::is_interval_valid() const {
    return interval.has_value() && ParameterValidator::validate_interval(interval.value());
}

// HttpParameterParser implementation
std::string HttpParameterParser::extract_query_string(const std::string& request) {
    // Find the first line of the HTTP request
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        line_end = request.find("\n");
        if (line_end == std::string::npos) {
            line_end = request.length();
        }
    }
    
    std::string first_line = request.substr(0, line_end);
    
    // Find the query string part (after '?')
    size_t query_start = first_line.find('?');
    if (query_start == std::string::npos) {
        return "";
    }
    
    // Find the end of the query string (before ' HTTP/')
    size_t query_end = first_line.find(" HTTP/", query_start);
    if (query_end == std::string::npos) {
        query_end = first_line.length();
    }
    
    return first_line.substr(query_start + 1, query_end - query_start - 1);
}

std::unordered_map<std::string, std::string> HttpParameterParser::parse_query_string(const std::string& query_string) {
    std::unordered_map<std::string, std::string> params;
    
    if (query_string.empty()) {
        return params;
    }
    
    std::istringstream stream(query_string);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t equals_pos = pair.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = pair.substr(0, equals_pos);
            std::string value = pair.substr(equals_pos + 1);
            params[key] = value;
        } else {
            // Parameter without value
            params[pair] = "";
        }
    }
    
    return params;
}

std::string HttpParameterParser::url_decode(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            int high = hex_to_int(encoded[i + 1]);
            int low = hex_to_int(encoded[i + 2]);
            
            if (high >= 0 && low >= 0) {
                decoded += static_cast<char>((high << 4) | low);
                i += 2;
            } else {
                decoded += encoded[i];
            }
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    
    return decoded;
}

std::pair<std::string, std::string> HttpParameterParser::extract_method_and_path(const std::string& request) {
    // Find the first line
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        line_end = request.find("\n");
        if (line_end == std::string::npos) {
            line_end = request.length();
        }
    }
    
    std::string first_line = request.substr(0, line_end);
    
    // Parse "METHOD /path HTTP/1.1"
    std::istringstream stream(first_line);
    std::string method, path, version;
    
    if (stream >> method >> path >> version) {
        // Remove query string from path
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path = path.substr(0, query_pos);
        }
        return {method, path};
    }
    
    return {"", ""};
}

int HttpParameterParser::hex_to_int(char hex) {
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    } else if (hex >= 'A' && hex <= 'F') {
        return hex - 'A' + 10;
    } else if (hex >= 'a' && hex <= 'f') {
        return hex - 'a' + 10;
    }
    return -1;
}

// Iso8601Parser implementation
std::optional<std::chrono::system_clock::time_point> Iso8601Parser::parse(const std::string& time_str) {
    if (time_str.empty()) {
        return std::nullopt;
    }
    
    // Expected format: YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS.sssZ
    if (time_str.length() < 19 || time_str.back() != 'Z') {
        return std::nullopt;
    }
    
    // Find the 'T' separator
    size_t t_pos = time_str.find('T');
    if (t_pos == std::string::npos || t_pos != 10) {
        return std::nullopt;
    }
    
    // Parse date part
    std::string date_part = time_str.substr(0, t_pos);
    int year, month, day;
    if (!parse_date(date_part, year, month, day)) {
        return std::nullopt;
    }
    
    // Parse time part (without 'Z')
    std::string time_part = time_str.substr(t_pos + 1, time_str.length() - t_pos - 2);
    int hour, minute, second, millisecond = 0;
    if (!parse_time(time_part, hour, minute, second, millisecond)) {
        return std::nullopt;
    }
    
    // Validate values
    if (!is_valid_date(year, month, day) || !is_valid_time(hour, minute, second, millisecond)) {
        return std::nullopt;
    }
    
    // Convert to time_point
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    
    // Convert to time_t (UTC)
    std::time_t time_t_value = std::mktime(&tm);
    if (time_t_value == -1) {
        return std::nullopt;
    }
    
    // Adjust for UTC (mktime assumes local time)
    time_t_value -= timezone;
    
    auto time_point = std::chrono::system_clock::from_time_t(time_t_value);
    time_point += std::chrono::milliseconds(millisecond);
    
    return time_point;
}

std::string Iso8601Parser::format(const std::chrono::system_clock::time_point& time_point) {
    auto time_t_value = std::chrono::system_clock::to_time_t(time_point);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_point.time_since_epoch()) % 1000;
    
    std::tm* utc_tm = std::gmtime(&time_t_value);
    if (!utc_tm) {
        return "";
    }
    
    std::ostringstream oss;
    oss << std::put_time(utc_tm, "%Y-%m-%dT%H:%M:%S");
    
    if (milliseconds.count() > 0) {
        oss << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    }
    
    oss << "Z";
    return oss.str();
}

bool Iso8601Parser::parse_date(const std::string& date_str, int& year, int& month, int& day) {
    if (date_str.length() != 10 || date_str[4] != '-' || date_str[7] != '-') {
        return false;
    }
    
    try {
        year = std::stoi(date_str.substr(0, 4));
        month = std::stoi(date_str.substr(5, 2));
        day = std::stoi(date_str.substr(8, 2));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Iso8601Parser::parse_time(const std::string& time_str, int& hour, int& minute, int& second, int& millisecond) {
    if (time_str.length() < 8 || time_str[2] != ':' || time_str[5] != ':') {
        return false;
    }
    
    try {
        hour = std::stoi(time_str.substr(0, 2));
        minute = std::stoi(time_str.substr(3, 2));
        second = std::stoi(time_str.substr(6, 2));
        
        // Check for milliseconds
        if (time_str.length() > 8 && time_str[8] == '.') {
            if (time_str.length() >= 12) {
                millisecond = std::stoi(time_str.substr(9, 3));
            }
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Iso8601Parser::is_valid_date(int year, int month, int day) {
    if (year < 1970 || year > 3000 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    
    // Simple day validation (doesn't account for leap years perfectly)
    static const int days_in_month[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day <= days_in_month[month - 1];
}

bool Iso8601Parser::is_valid_time(int hour, int minute, int second, int millisecond) {
    return hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59 &&
           millisecond >= 0 && millisecond <= 999;
}

// ParameterValidator implementation
bool ParameterValidator::validate_count(int count, int min_value, int max_value) {
    return count >= min_value && count <= max_value;
}

bool ParameterValidator::validate_time_range(const std::chrono::system_clock::time_point& start,
                                           const std::chrono::system_clock::time_point& end,
                                           int max_range_hours) {
    if (start > end) {
        return false;
    }
    
    auto duration = end - start;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    
    return hours.count() <= max_range_hours;
}

bool ParameterValidator::validate_interval(const std::string& interval) {
    return parse_interval(interval).has_value();
}

std::optional<std::chrono::minutes> ParameterValidator::parse_interval(const std::string& interval) {
    if (interval.empty()) {
        return std::nullopt;
    }
    
    // Parse format like "1H", "30T", "1D"
    std::regex interval_regex(R"(^(\d+)([HTDM])$)");
    std::smatch match;
    
    if (!std::regex_match(interval, match, interval_regex)) {
        return std::nullopt;
    }
    
    int value = std::stoi(match[1].str());
    char unit = match[2].str()[0];
    
    switch (unit) {
        case 'T': // Minutes
            return std::chrono::minutes(value);
        case 'H': // Hours
            return std::chrono::minutes(value * 60);
        case 'D': // Days
            return std::chrono::minutes(value * 60 * 24);
        case 'M': // Months (approximate as 30 days)
            return std::chrono::minutes(value * 60 * 24 * 30);
        default:
            return std::nullopt;
    }
}

} // namespace sensor_daemon