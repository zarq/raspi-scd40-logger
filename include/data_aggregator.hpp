#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <optional>
#include "sensor_data.hpp"
#include "json_response_builder.hpp"

namespace sensor_daemon {

/**
 * Data aggregator for computing statistical summaries over time intervals
 */
class DataAggregator {
public:
    /**
     * Aggregate sensor readings by time interval
     * @param readings Vector of sensor readings (should be sorted by timestamp)
     * @param interval_str Interval string (e.g., "1H", "30T", "1D")
     * @return Vector of aggregated data for each interval
     */
    static std::vector<AggregateData> aggregate_by_interval(
        const std::vector<SensorData>& readings,
        const std::string& interval_str);
    
    /**
     * Aggregate sensor readings by fixed time interval
     * @param readings Vector of sensor readings (should be sorted by timestamp)
     * @param interval_minutes Interval duration in minutes
     * @return Vector of aggregated data for each interval
     */
    static std::vector<AggregateData> aggregate_by_interval(
        const std::vector<SensorData>& readings,
        std::chrono::minutes interval_minutes);
    
    /**
     * Parse interval string to duration in minutes
     * @param interval_str Interval string (e.g., "1H", "30T", "1D", "2M")
     * @return Duration in minutes, or nullopt if parsing fails
     */
    static std::optional<std::chrono::minutes> parse_interval(const std::string& interval_str);

private:
    /**
     * Aggregate a group of readings into statistical summary
     * @param readings Vector of readings for a single time interval
     * @param interval_start Start time of the interval
     * @return Aggregated data for the interval
     */
    static AggregateData aggregate_readings(
        const std::vector<SensorData>& readings,
        std::chrono::system_clock::time_point interval_start);
    
    /**
     * Compute statistics for a specific sensor value
     * @param values Vector of optional sensor values
     * @return Value statistics (mean, min, max, count)
     */
    static AggregateData::ValueStats compute_value_stats(
        const std::vector<std::optional<float>>& values);
    
    /**
     * Get the start of the time interval containing the given timestamp
     * @param timestamp Timestamp to align
     * @param interval_minutes Interval duration in minutes
     * @return Start of the interval containing the timestamp
     */
    static std::chrono::system_clock::time_point align_to_interval(
        std::chrono::system_clock::time_point timestamp,
        std::chrono::minutes interval_minutes);
    
    /**
     * Generate all interval boundaries between start and end times
     * @param start_time Start of the time range
     * @param end_time End of the time range
     * @param interval_minutes Interval duration in minutes
     * @return Vector of interval start times
     */
    static std::vector<std::chrono::system_clock::time_point> generate_intervals(
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time,
        std::chrono::minutes interval_minutes);
    
    /**
     * Group readings by time intervals
     * @param readings Vector of sensor readings (should be sorted by timestamp)
     * @param intervals Vector of interval start times
     * @param interval_minutes Interval duration in minutes
     * @return Map of interval start time to readings in that interval
     */
    static std::unordered_map<std::chrono::system_clock::time_point, std::vector<SensorData>, TimePointHash> 
    group_readings_by_interval(
        const std::vector<SensorData>& readings,
        const std::vector<std::chrono::system_clock::time_point>& intervals,
        std::chrono::minutes interval_minutes);
    
    /**
     * Hash function for time_point to use in unordered_map
     */
    struct TimePointHash {
        std::size_t operator()(const std::chrono::system_clock::time_point& tp) const {
            return std::hash<std::chrono::system_clock::rep>{}(tp.time_since_epoch().count());
        }
    };
};

/**
 * Interval parsing utilities
 */
class IntervalParser {
public:
    /**
     * Parse interval string with validation
     * @param interval_str Interval string to parse
     * @return Duration in minutes, or nullopt if invalid
     */
    static std::optional<std::chrono::minutes> parse(const std::string& interval_str);
    
    /**
     * Validate interval string format
     * @param interval_str Interval string to validate
     * @return true if format is valid
     */
    static bool is_valid_format(const std::string& interval_str);
    
    /**
     * Get supported interval formats
     * @return Vector of example interval formats
     */
    static std::vector<std::string> get_supported_formats();

private:
    /**
     * Parse numeric value from interval string
     * @param interval_str Interval string
     * @param unit_char Output unit character
     * @return Numeric value, or -1 if parsing fails
     */
    static int parse_numeric_value(const std::string& interval_str, char& unit_char);
    
    /**
     * Convert unit character to minutes multiplier
     * @param unit_char Unit character ('T', 'H', 'D', 'M')
     * @return Minutes multiplier, or -1 if invalid unit
     */
    static int get_unit_multiplier(char unit_char);
};

} // namespace sensor_daemon