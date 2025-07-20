#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>

namespace sensor_daemon {

/**
 * Performance metrics for monitoring system health
 */
struct PerformanceMetrics {
    std::chrono::steady_clock::time_point start_time;
    uint64_t sensor_readings_success{0};
    uint64_t sensor_readings_failed{0};
    uint64_t storage_writes_success{0};
    uint64_t storage_writes_failed{0};
    uint64_t i2c_connection_failures{0};
    uint64_t memory_usage_bytes{0};
    double cpu_usage_percent{0.0};
    
    // Calculate uptime
    std::chrono::seconds get_uptime() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
    }
    
    // Calculate success rates
    double get_sensor_success_rate() const {
        uint64_t total = sensor_readings_success + sensor_readings_failed;
        return total > 0 ? static_cast<double>(sensor_readings_success) / total : 0.0;
    }
    
    double get_storage_success_rate() const {
        uint64_t total = storage_writes_success + storage_writes_failed;
        return total > 0 ? static_cast<double>(storage_writes_success) / total : 0.0;
    }
};

/**
 * Log levels supported by the logging system
 */
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

/**
 * Context information for error logging
 */
struct ErrorContext {
    std::string component;
    std::string operation;
    std::string error_code;
    std::unordered_map<std::string, std::string> additional_data;
    
    ErrorContext(const std::string& comp, const std::string& op, const std::string& code = "")
        : component(comp), operation(op), error_code(code) {}
    
    ErrorContext& add_data(const std::string& key, const std::string& value) {
        additional_data[key] = value;
        return *this;
    }
};

/**
 * Centralized logging system with structured output and rotation
 */
class LoggingSystem {
public:
    /**
     * Initialize the logging system
     * @param log_level Minimum log level to output
     * @param log_file_path Path to log file (empty for console only)
     * @param max_file_size Maximum size of each log file in bytes
     * @param max_files Maximum number of rotated log files to keep
     * @param enable_console Whether to also log to console
     * @return true if initialization successful
     */
    static bool initialize(LogLevel log_level,
                          const std::string& log_file_path = "",
                          size_t max_file_size = 10 * 1024 * 1024, // 10MB
                          size_t max_files = 5,
                          bool enable_console = true);
    
    /**
     * Shutdown the logging system
     */
    static void shutdown();
    
    /**
     * Set the minimum log level
     */
    static void set_log_level(LogLevel level);
    
    /**
     * Log daemon lifecycle events
     */
    static void log_daemon_startup(const std::string& version, const std::string& config_path);
    static void log_daemon_shutdown(const std::string& reason);
    static void log_daemon_error(const std::string& error_message, const ErrorContext& context);
    
    /**
     * Log sensor-related events
     */
    static void log_sensor_reading(bool success, const std::string& details = "");
    static void log_sensor_connection(bool connected, const std::string& device_path);
    static void log_sensor_error(const std::string& error_message, const ErrorContext& context);
    
    /**
     * Log storage-related events
     */
    static void log_storage_write(bool success, const std::string& details = "");
    static void log_storage_error(const std::string& error_message, const ErrorContext& context);
    
    /**
     * Log performance metrics
     */
    static void log_performance_metrics(const PerformanceMetrics& metrics);
    
    /**
     * Generic structured logging methods
     */
    static void trace(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void debug(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void info(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void warn(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void error(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void critical(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    
    /**
     * Log with error context
     */
    static void log_with_context(LogLevel level, const std::string& message, const ErrorContext& context);
    
    /**
     * Check if logging system is initialized
     */
    static bool is_initialized();
    
    /**
     * Get current log level
     */
    static LogLevel get_log_level();
    
    /**
     * Convert string to LogLevel
     */
    static LogLevel string_to_log_level(const std::string& level_str);
    
    /**
     * Convert LogLevel to string
     */
    static std::string log_level_to_string(LogLevel level);

private:
    static std::shared_ptr<spdlog::logger> logger_;
    static LogLevel current_level_;
    static bool initialized_;
    
    /**
     * Format context data as structured string
     */
    static std::string format_context(const std::unordered_map<std::string, std::string>& context);
    
    /**
     * Format error context as structured string
     */
    static std::string format_error_context(const ErrorContext& context);
    
    /**
     * Convert LogLevel to spdlog level
     */
    static spdlog::level::level_enum to_spdlog_level(LogLevel level);
};

/**
 * RAII helper for performance timing
 */
class PerformanceTimer {
public:
    explicit PerformanceTimer(const std::string& operation_name);
    ~PerformanceTimer();
    
    // Disable copy and move
    PerformanceTimer(const PerformanceTimer&) = delete;
    PerformanceTimer& operator=(const PerformanceTimer&) = delete;
    PerformanceTimer(PerformanceTimer&&) = delete;
    PerformanceTimer& operator=(PerformanceTimer&&) = delete;

private:
    std::string operation_name_;
    std::chrono::steady_clock::time_point start_time_;
};

// Convenience macros for structured logging
#define LOG_TRACE(msg, ...) sensor_daemon::LoggingSystem::trace(msg, ##__VA_ARGS__)
#define LOG_DEBUG(msg, ...) sensor_daemon::LoggingSystem::debug(msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) sensor_daemon::LoggingSystem::info(msg, ##__VA_ARGS__)
#define LOG_WARN(msg, ...) sensor_daemon::LoggingSystem::warn(msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) sensor_daemon::LoggingSystem::error(msg, ##__VA_ARGS__)
#define LOG_CRITICAL(msg, ...) sensor_daemon::LoggingSystem::critical(msg, ##__VA_ARGS__)

// Macro for performance timing
#define PERF_TIMER(name) sensor_daemon::PerformanceTimer _timer(name)

} // namespace sensor_daemon