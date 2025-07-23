#include "json_response_builder.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace sensor_daemon {

std::string JsonResponseBuilder::create_readings_response(const std::vector<SensorData>& readings) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"readings\": [\n";
    
    for (size_t i = 0; i < readings.size(); ++i) {
        if (i > 0) json << ",\n";
        json << "    " << sensor_data_to_json(readings[i]);
    }
    
    json << "\n  ],\n";
    json << "  \"total_count\": " << readings.size() << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return create_http_header(HttpStatus::OK, json_body.length()) + json_body;
}

std::string JsonResponseBuilder::create_range_response(const std::vector<SensorData>& readings,
                                                     const std::string& start_time,
                                                     const std::string& end_time) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"readings\": [\n";
    
    for (size_t i = 0; i < readings.size(); ++i) {
        if (i > 0) json << ",\n";
        json << "    " << sensor_data_to_json(readings[i]);
    }
    
    json << "\n  ],\n";
    json << "  \"start_time\": \"" << escape_json_string(start_time) << "\",\n";
    json << "  \"end_time\": \"" << escape_json_string(end_time) << "\",\n";
    json << "  \"total_count\": " << readings.size() << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return create_http_header(HttpStatus::OK, json_body.length()) + json_body;
}

std::string JsonResponseBuilder::create_aggregates_response(const std::vector<AggregateData>& aggregates,
                                                          const std::string& start_time,
                                                          const std::string& end_time,
                                                          const std::string& interval) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"aggregates\": [\n";
    
    for (size_t i = 0; i < aggregates.size(); ++i) {
        if (i > 0) json << ",\n";
        json << "    " << aggregate_data_to_json(aggregates[i]);
    }
    
    json << "\n  ],\n";
    json << "  \"start_time\": \"" << escape_json_string(start_time) << "\",\n";
    json << "  \"end_time\": \"" << escape_json_string(end_time) << "\",\n";
    json << "  \"interval\": \"" << escape_json_string(interval) << "\",\n";
    json << "  \"total_intervals\": " << aggregates.size() << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return create_http_header(HttpStatus::OK, json_body.length()) + json_body;
}

std::string JsonResponseBuilder::create_info_response(const TimeSeriesStorage::DatabaseInfo& info) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"total_records\": " << info.total_records << ",\n";
    json << "  \"database_path\": \"" << escape_json_string(info.database_path) << "\",\n";
    json << "  \"earliest_timestamp\": \"" << timestamp_to_iso8601(info.earliest_timestamp) << "\",\n";
    json << "  \"latest_timestamp\": \"" << timestamp_to_iso8601(info.latest_timestamp) << "\",\n";
    json << "  \"database_size_bytes\": " << info.database_size_bytes << ",\n";
    json << "  \"implementation\": \"" << escape_json_string(info.implementation) << "\",\n";
    json << "  \"healthy\": " << (info.is_healthy ? "true" : "false") << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return create_http_header(HttpStatus::OK, json_body.length()) + json_body;
}

std::string JsonResponseBuilder::create_error_response(int status_code, 
                                                     const std::string& error_msg,
                                                     const std::string& details) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"error\": \"" << escape_json_string(error_msg) << "\",\n";
    
    if (!details.empty()) {
        json << "  \"details\": \"" << escape_json_string(details) << "\",\n";
    }
    
    json << "  \"timestamp\": \"" << get_current_timestamp() << "\",\n";
    json << "  \"status_code\": " << status_code << "\n";
    json << "}\n";
    
    std::string json_body = json.str();
    return create_http_header(status_code, json_body.length()) + json_body;
}

std::string JsonResponseBuilder::sensor_data_to_json(const SensorData& reading) {
    std::ostringstream json;
    
    json << "{\n";
    json << "      \"timestamp\": \"" << timestamp_to_iso8601(reading.timestamp) << "\",\n";
    
    // Handle optional CO2 value
    if (reading.co2_ppm.has_value()) {
        json << "      \"co2_ppm\": " << format_json_number(reading.co2_ppm.value(), 1) << ",\n";
    } else {
        json << "      \"co2_ppm\": null,\n";
    }
    
    // Handle optional temperature value
    if (reading.temperature_c.has_value()) {
        json << "      \"temperature_c\": " << format_json_number(reading.temperature_c.value(), 1) << ",\n";
    } else {
        json << "      \"temperature_c\": null,\n";
    }
    
    // Handle optional humidity value
    if (reading.humidity_percent.has_value()) {
        json << "      \"humidity_percent\": " << format_json_number(reading.humidity_percent.value(), 1) << ",\n";
    } else {
        json << "      \"humidity_percent\": null,\n";
    }
    
    json << "      \"quality_flags\": " << reading.quality_flags << "\n";
    json << "    }";
    
    return json.str();
}

std::string JsonResponseBuilder::aggregate_data_to_json(const AggregateData& aggregate) {
    std::ostringstream json;
    
    json << "{\n";
    json << "      \"timestamp\": \"" << timestamp_to_iso8601(aggregate.timestamp) << "\",\n";
    
    // CO2 statistics
    if (aggregate.co2_ppm.has_data) {
        json << "      \"co2_ppm_mean\": " << format_json_number(aggregate.co2_ppm.mean, 1) << ",\n";
        json << "      \"co2_ppm_min\": " << format_json_number(aggregate.co2_ppm.min, 1) << ",\n";
        json << "      \"co2_ppm_max\": " << format_json_number(aggregate.co2_ppm.max, 1) << ",\n";
        json << "      \"co2_ppm_count\": " << aggregate.co2_ppm.count << ",\n";
    } else {
        json << "      \"co2_ppm_mean\": null,\n";
        json << "      \"co2_ppm_min\": null,\n";
        json << "      \"co2_ppm_max\": null,\n";
        json << "      \"co2_ppm_count\": 0,\n";
    }
    
    // Temperature statistics
    if (aggregate.temperature_c.has_data) {
        json << "      \"temperature_c_mean\": " << format_json_number(aggregate.temperature_c.mean, 1) << ",\n";
        json << "      \"temperature_c_min\": " << format_json_number(aggregate.temperature_c.min, 1) << ",\n";
        json << "      \"temperature_c_max\": " << format_json_number(aggregate.temperature_c.max, 1) << ",\n";
        json << "      \"temperature_c_count\": " << aggregate.temperature_c.count << ",\n";
    } else {
        json << "      \"temperature_c_mean\": null,\n";
        json << "      \"temperature_c_min\": null,\n";
        json << "      \"temperature_c_max\": null,\n";
        json << "      \"temperature_c_count\": 0,\n";
    }
    
    // Humidity statistics
    if (aggregate.humidity_percent.has_data) {
        json << "      \"humidity_percent_mean\": " << format_json_number(aggregate.humidity_percent.mean, 1) << ",\n";
        json << "      \"humidity_percent_min\": " << format_json_number(aggregate.humidity_percent.min, 1) << ",\n";
        json << "      \"humidity_percent_max\": " << format_json_number(aggregate.humidity_percent.max, 1) << ",\n";
        json << "      \"humidity_percent_count\": " << aggregate.humidity_percent.count << "\n";
    } else {
        json << "      \"humidity_percent_mean\": null,\n";
        json << "      \"humidity_percent_min\": null,\n";
        json << "      \"humidity_percent_max\": null,\n";
        json << "      \"humidity_percent_count\": 0\n";
    }
    
    json << "    }";
    
    return json.str();
}

std::string JsonResponseBuilder::timestamp_to_iso8601(const std::chrono::system_clock::time_point& timestamp) {
    return Iso8601Parser::format(timestamp);
}

std::string JsonResponseBuilder::get_current_timestamp() {
    return timestamp_to_iso8601(std::chrono::system_clock::now());
}

std::string JsonResponseBuilder::create_http_header(int status_code, size_t content_length) {
    std::ostringstream header;
    
    header << "HTTP/1.1 " << status_code << " " << get_status_text(status_code) << "\r\n";
    header << "Content-Type: application/json\r\n";
    header << "Connection: close\r\n";
    header << "Access-Control-Allow-Origin: *\r\n";  // Enable CORS for web clients
    header << "Cache-Control: no-cache\r\n";
    
    if (content_length > 0) {
        header << "Content-Length: " << content_length << "\r\n";
    }
    
    header << "\r\n";
    
    return header.str();
}

std::string JsonResponseBuilder::get_status_text(int status_code) {
    switch (status_code) {
        case HttpStatus::OK: return "OK";
        case HttpStatus::BAD_REQUEST: return "Bad Request";
        case HttpStatus::NOT_FOUND: return "Not Found";
        case HttpStatus::TOO_MANY_REQUESTS: return "Too Many Requests";
        case HttpStatus::INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HttpStatus::SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "Unknown";
    }
}

std::string JsonResponseBuilder::escape_json_string(const std::string& str) {
    std::ostringstream escaped;
    
    for (char c : str) {
        switch (c) {
            case '"':
                escaped << "\\\"";
                break;
            case '\\':
                escaped << "\\\\";
                break;
            case '\b':
                escaped << "\\b";
                break;
            case '\f':
                escaped << "\\f";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                if (c >= 0 && c < 32) {
                    // Control characters
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    escaped << c;
                }
                break;
        }
    }
    
    return escaped.str();
}

std::string JsonResponseBuilder::format_json_number(double value, int precision) {
    // Handle special values
    if (std::isnan(value)) {
        return "null";
    }
    if (std::isinf(value)) {
        return value > 0 ? "null" : "null";  // JSON doesn't support infinity
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    
    std::string result = oss.str();
    
    // Remove trailing zeros after decimal point
    if (result.find('.') != std::string::npos) {
        result = result.substr(0, result.find_last_not_of('0') + 1);
        if (result.back() == '.') {
            result.pop_back();
        }
    }
    
    return result;
}

std::string JsonResponseBuilder::create_json_array(const std::vector<std::string>& json_objects) {
    std::ostringstream array;
    
    array << "[\n";
    
    for (size_t i = 0; i < json_objects.size(); ++i) {
        if (i > 0) array << ",\n";
        array << "  " << json_objects[i];
    }
    
    array << "\n]";
    
    return array.str();
}

} // namespace sensor_daemon