#include "data_aggregator.hpp"
#include <algorithm>
#include <unordered_map>
#include <regex>
#include <cmath>

namespace sensor_daemon {

std::vector<AggregateData> DataAggregator::aggregate_by_interval(
    const std::vector<SensorData>& readings,
    const std::string& interval_str) {
    
    auto interval_minutes = parse_interval(interval_str);
    if (!interval_minutes.has_value()) {
        return {}; // Return empty vector for invalid interval
    }
    
    return aggregate_by_interval(readings, interval_minutes.value());
}

std::vector<AggregateData> DataAggregator::aggregate_by_interval(
    const std::vector<SensorData>& readings,
    std::chrono::minutes interval_minutes) {
    
    if (readings.empty() || interval_minutes.count() <= 0) {
        return {};
    }
    
    // Find the time range of the data
    auto start_time = readings.front().timestamp;
    auto end_time = readings.back().timestamp;
    
    // Align start time to interval boundary
    start_time = align_to_interval(start_time, interval_minutes);
    
    // Generate all intervals in the range
    auto intervals = generate_intervals(start_time, end_time, interval_minutes);
    
    // Group readings by intervals
    auto grouped_readings = group_readings_by_interval(readings, intervals, interval_minutes);
    
    // Aggregate each interval
    std::vector<AggregateData> aggregates;
    aggregates.reserve(intervals.size());
    
    for (const auto& interval_start : intervals) {
        auto it = grouped_readings.find(interval_start);
        if (it != grouped_readings.end()) {
            aggregates.push_back(aggregate_readings(it->second, interval_start));
        } else {
            // Empty interval
            AggregateData empty_aggregate(interval_start);
            aggregates.push_back(empty_aggregate);
        }
    }
    
    return aggregates;
}

std::optional<std::chrono::minutes> DataAggregator::parse_interval(const std::string& interval_str) {
    return IntervalParser::parse(interval_str);
}

AggregateData DataAggregator::aggregate_readings(
    const std::vector<SensorData>& readings,
    std::chrono::system_clock::time_point interval_start) {
    
    AggregateData aggregate(interval_start);
    
    if (readings.empty()) {
        return aggregate;
    }
    
    // Extract values for each sensor type
    std::vector<std::optional<float>> co2_values;
    std::vector<std::optional<float>> temp_values;
    std::vector<std::optional<float>> humidity_values;
    
    co2_values.reserve(readings.size());
    temp_values.reserve(readings.size());
    humidity_values.reserve(readings.size());
    
    for (const auto& reading : readings) {
        co2_values.push_back(reading.co2_ppm);
        temp_values.push_back(reading.temperature_c);
        humidity_values.push_back(reading.humidity_percent);
    }
    
    // Compute statistics for each sensor type
    aggregate.co2_ppm = compute_value_stats(co2_values);
    aggregate.temperature_c = compute_value_stats(temp_values);
    aggregate.humidity_percent = compute_value_stats(humidity_values);
    
    return aggregate;
}

AggregateData::ValueStats DataAggregator::compute_value_stats(
    const std::vector<std::optional<float>>& values) {
    
    AggregateData::ValueStats stats;
    
    // Extract valid values
    std::vector<float> valid_values;
    valid_values.reserve(values.size());
    
    for (const auto& value : values) {
        if (value.has_value() && std::isfinite(value.value())) {
            valid_values.push_back(value.value());
        }
    }
    
    if (valid_values.empty()) {
        return stats; // has_data remains false
    }
    
    // Compute statistics
    stats.has_data = true;
    stats.count = valid_values.size();
    
    // Min and max
    auto [min_it, max_it] = std::minmax_element(valid_values.begin(), valid_values.end());
    stats.min = *min_it;
    stats.max = *max_it;
    
    // Mean
    double sum = 0.0;
    for (float value : valid_values) {
        sum += value;
    }
    stats.mean = sum / valid_values.size();
    
    return stats;
}

std::chrono::system_clock::time_point DataAggregator::align_to_interval(
    std::chrono::system_clock::time_point timestamp,
    std::chrono::minutes interval_minutes) {
    
    // Convert to time_t for easier manipulation
    auto time_t_value = std::chrono::system_clock::to_time_t(timestamp);
    auto tm_value = *std::gmtime(&time_t_value);
    
    // Align based on interval size
    if (interval_minutes >= std::chrono::hours(24)) {
        // Daily or larger intervals - align to start of day
        tm_value.tm_hour = 0;
        tm_value.tm_min = 0;
        tm_value.tm_sec = 0;
    } else if (interval_minutes >= std::chrono::hours(1)) {
        // Hourly intervals - align to start of hour
        int interval_hours = static_cast<int>(interval_minutes.count() / 60);
        tm_value.tm_hour = (tm_value.tm_hour / interval_hours) * interval_hours;
        tm_value.tm_min = 0;
        tm_value.tm_sec = 0;
    } else {
        // Minute intervals - align to interval boundary
        int interval_mins = static_cast<int>(interval_minutes.count());
        tm_value.tm_min = (tm_value.tm_min / interval_mins) * interval_mins;
        tm_value.tm_sec = 0;
    }
    
    // Convert back to time_point
    auto aligned_time_t = std::mktime(&tm_value);
    return std::chrono::system_clock::from_time_t(aligned_time_t);
}

std::vector<std::chrono::system_clock::time_point> DataAggregator::generate_intervals(
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time,
    std::chrono::minutes interval_minutes) {
    
    std::vector<std::chrono::system_clock::time_point> intervals;
    
    auto current_time = start_time;
    while (current_time <= end_time) {
        intervals.push_back(current_time);
        current_time += interval_minutes;
    }
    
    return intervals;
}

std::unordered_map<std::chrono::system_clock::time_point, std::vector<SensorData>, DataAggregator::TimePointHash>
DataAggregator::group_readings_by_interval(
    const std::vector<SensorData>& readings,
    const std::vector<std::chrono::system_clock::time_point>& intervals,
    std::chrono::minutes interval_minutes) {
    
    std::unordered_map<std::chrono::system_clock::time_point, std::vector<SensorData>, TimePointHash> grouped;
    
    // Initialize empty groups for all intervals
    for (const auto& interval : intervals) {
        grouped[interval] = std::vector<SensorData>();
    }
    
    // Assign readings to intervals
    for (const auto& reading : readings) {
        auto interval_start = align_to_interval(reading.timestamp, interval_minutes);
        
        auto it = grouped.find(interval_start);
        if (it != grouped.end()) {
            it->second.push_back(reading);
        }
    }
    
    return grouped;
}

// IntervalParser implementation
std::optional<std::chrono::minutes> IntervalParser::parse(const std::string& interval_str) {
    if (!is_valid_format(interval_str)) {
        return std::nullopt;
    }
    
    char unit_char;
    int value = parse_numeric_value(interval_str, unit_char);
    
    if (value <= 0) {
        return std::nullopt;
    }
    
    int multiplier = get_unit_multiplier(unit_char);
    if (multiplier < 0) {
        return std::nullopt;
    }
    
    return std::chrono::minutes(value * multiplier);
}

bool IntervalParser::is_valid_format(const std::string& interval_str) {
    if (interval_str.empty()) {
        return false;
    }
    
    // Check format: number followed by unit character
    std::regex interval_regex(R"(^(\d+)([HTDM])$)");
    return std::regex_match(interval_str, interval_regex);
}

std::vector<std::string> IntervalParser::get_supported_formats() {
    return {
        "1T - 1 minute",
        "5T - 5 minutes", 
        "15T - 15 minutes",
        "30T - 30 minutes",
        "1H - 1 hour",
        "2H - 2 hours",
        "6H - 6 hours",
        "12H - 12 hours",
        "1D - 1 day",
        "7D - 7 days",
        "1M - 1 month (30 days)"
    };
}

int IntervalParser::parse_numeric_value(const std::string& interval_str, char& unit_char) {
    if (interval_str.empty()) {
        return -1;
    }
    
    // Find the unit character (last character)
    unit_char = interval_str.back();
    
    // Parse the numeric part
    std::string numeric_part = interval_str.substr(0, interval_str.length() - 1);
    
    try {
        return std::stoi(numeric_part);
    } catch (const std::exception&) {
        return -1;
    }
}

int IntervalParser::get_unit_multiplier(char unit_char) {
    switch (unit_char) {
        case 'T': // Minutes
            return 1;
        case 'H': // Hours
            return 60;
        case 'D': // Days
            return 60 * 24;
        case 'M': // Months (approximate as 30 days)
            return 60 * 24 * 30;
        default:
            return -1;
    }
}

} // namespace sensor_daemon