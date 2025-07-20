#include "logging_system.hpp"
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <unistd.h> // for getpid()

namespace sensor_daemon {

// Static member definitions
std::shared_ptr<spdlog::logger> LoggingSystem::logger_;
LogLevel LoggingSystem::current_level_ = LogLevel::INFO;
bool LoggingSystem::initialized_ = false;

bool LoggingSystem::initialize(LogLevel log_level,
                              const std::string& log_file_path,
                              size_t max_file_size,
                              size_t max_files,
                              bool enable_console) {
    try {
        // Create sinks vector
        std::vector<spdlog::sink_ptr> sinks;
        
        // Add console sink if enabled
        if (enable_console) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(to_spdlog_level(log_level));
            sinks.push_back(console_sink);
        }
        
        // Add file sink if path provided
        if (!log_file_path.empty()) {
            // Create directory if it doesn't exist
            std::filesystem::path log_path(log_file_path);
            std::filesystem::create_directories(log_path.parent_path());
            
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file_path, max_file_size, max_files);
            file_sink->set_level(to_spdlog_level(log_level));
            sinks.push_back(file_sink);
        }
        
        // Create logger with combined sinks
        logger_ = std::make_shared<spdlog::logger>("sensor_daemon", sinks.begin(), sinks.end());
        
        // Set pattern for structured logging
        // Format: [timestamp] [level] [thread] component:operation - message {context}
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] [%t] %v");
        logger_->set_level(to_spdlog_level(log_level));
        
        // Register as default logger
        spdlog::set_default_logger(logger_);
        
        current_level_ = log_level;
        initialized_ = true;
        
        // Log initialization success
        info("Logging system initialized", {
            {"log_level", log_level_to_string(log_level)},
            {"file_path", log_file_path.empty() ? "console_only" : log_file_path},
            {"max_file_size", std::to_string(max_file_size)},
            {"max_files", std::to_string(max_files)}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logging system: " << e.what() << std::endl;
        return false;
    }
}

void LoggingSystem::shutdown() {
    if (initialized_) {
        info("Shutting down logging system");
        logger_->flush();
        spdlog::shutdown();
        initialized_ = false;
    }
}

void LoggingSystem::set_log_level(LogLevel level) {
    current_level_ = level;
    if (logger_) {
        logger_->set_level(to_spdlog_level(level));
    }
}

void LoggingSystem::log_daemon_startup(const std::string& version, const std::string& config_path) {
    info("daemon:startup - Sensor daemon starting", {
        {"version", version},
        {"config_path", config_path},
        {"pid", std::to_string(getpid())}
    });
}

void LoggingSystem::log_daemon_shutdown(const std::string& reason) {
    info("daemon:shutdown - Sensor daemon shutting down", {
        {"reason", reason},
        {"pid", std::to_string(getpid())}
    });
}

void LoggingSystem::log_daemon_error(const std::string& error_message, const ErrorContext& context) {
    error("daemon:error - " + error_message, {
        {"component", context.component},
        {"operation", context.operation},
        {"error_code", context.error_code}
    });
}

void LoggingSystem::log_sensor_reading(bool success, const std::string& details) {
    if (success) {
        debug("sensor:reading - Sensor reading successful", {
            {"status", "success"},
            {"details", details}
        });
    } else {
        warn("sensor:reading - Sensor reading failed", {
            {"status", "failed"},
            {"details", details}
        });
    }
}

void LoggingSystem::log_sensor_connection(bool connected, const std::string& device_path) {
    if (connected) {
        info("sensor:connection - Sensor connected", {
            {"status", "connected"},
            {"device_path", device_path}
        });
    } else {
        error("sensor:connection - Sensor connection failed", {
            {"status", "disconnected"},
            {"device_path", device_path}
        });
    }
}

void LoggingSystem::log_sensor_error(const std::string& error_message, const ErrorContext& context) {
    log_with_context(LogLevel::ERROR, "sensor:error - " + error_message, context);
}

void LoggingSystem::log_storage_write(bool success, const std::string& details) {
    if (success) {
        debug("storage:write - Data write successful", {
            {"status", "success"},
            {"details", details}
        });
    } else {
        error("storage:write - Data write failed", {
            {"status", "failed"},
            {"details", details}
        });
    }
}

void LoggingSystem::log_storage_error(const std::string& error_message, const ErrorContext& context) {
    log_with_context(LogLevel::ERROR, "storage:error - " + error_message, context);
}

void LoggingSystem::log_performance_metrics(const PerformanceMetrics& metrics) {
    info("performance:metrics - System performance update", {
        {"uptime_seconds", std::to_string(metrics.get_uptime().count())},
        {"sensor_success_rate", fmt::format("{:.2f}", metrics.get_sensor_success_rate() * 100)},
        {"storage_success_rate", fmt::format("{:.2f}", metrics.get_storage_success_rate() * 100)},
        {"sensor_readings_success", std::to_string(metrics.sensor_readings_success)},
        {"sensor_readings_failed", std::to_string(metrics.sensor_readings_failed)},
        {"storage_writes_success", std::to_string(metrics.storage_writes_success)},
        {"storage_writes_failed", std::to_string(metrics.storage_writes_failed)},
        {"i2c_connection_failures", std::to_string(metrics.i2c_connection_failures)},
        {"memory_usage_mb", fmt::format("{:.2f}", metrics.memory_usage_bytes / (1024.0 * 1024.0))},
        {"cpu_usage_percent", fmt::format("{:.2f}", metrics.cpu_usage_percent)}
    });
}

void LoggingSystem::trace(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->trace(formatted_message);
}

void LoggingSystem::debug(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->debug(formatted_message);
}

void LoggingSystem::info(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->info(formatted_message);
}

void LoggingSystem::warn(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->warn(formatted_message);
}

void LoggingSystem::error(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->error(formatted_message);
}

void LoggingSystem::critical(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message;
    if (!context.empty()) {
        formatted_message += " " + format_context(context);
    }
    
    logger_->critical(formatted_message);
}

void LoggingSystem::log_with_context(LogLevel level, const std::string& message, const ErrorContext& context) {
    if (!initialized_ || !logger_) return;
    
    std::string formatted_message = message + " " + format_error_context(context);
    
    switch (level) {
        case LogLevel::TRACE:
            logger_->trace(formatted_message);
            break;
        case LogLevel::DEBUG:
            logger_->debug(formatted_message);
            break;
        case LogLevel::INFO:
            logger_->info(formatted_message);
            break;
        case LogLevel::WARN:
            logger_->warn(formatted_message);
            break;
        case LogLevel::ERROR:
            logger_->error(formatted_message);
            break;
        case LogLevel::CRITICAL:
            logger_->critical(formatted_message);
            break;
    }
}

bool LoggingSystem::is_initialized() {
    return initialized_;
}

LogLevel LoggingSystem::get_log_level() {
    return current_level_;
}

LogLevel LoggingSystem::string_to_log_level(const std::string& level_str) {
    std::string lower_level = level_str;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);
    
    if (lower_level == "trace") return LogLevel::TRACE;
    if (lower_level == "debug") return LogLevel::DEBUG;
    if (lower_level == "info") return LogLevel::INFO;
    if (lower_level == "warn" || lower_level == "warning") return LogLevel::WARN;
    if (lower_level == "error") return LogLevel::ERROR;
    if (lower_level == "critical") return LogLevel::CRITICAL;
    
    return LogLevel::INFO; // Default fallback
}

std::string LoggingSystem::log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "trace";
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO: return "info";
        case LogLevel::WARN: return "warn";
        case LogLevel::ERROR: return "error";
        case LogLevel::CRITICAL: return "critical";
        default: return "info";
    }
}

std::string LoggingSystem::format_context(const std::unordered_map<std::string, std::string>& context) {
    if (context.empty()) return "";
    
    std::string result = "{";
    bool first = true;
    for (const auto& [key, value] : context) {
        if (!first) result += ", ";
        result += key + "=" + value;
        first = false;
    }
    result += "}";
    return result;
}

std::string LoggingSystem::format_error_context(const ErrorContext& context) {
    std::unordered_map<std::string, std::string> context_map = {
        {"component", context.component},
        {"operation", context.operation}
    };
    
    if (!context.error_code.empty()) {
        context_map["error_code"] = context.error_code;
    }
    
    // Add additional data
    for (const auto& [key, value] : context.additional_data) {
        context_map[key] = value;
    }
    
    return format_context(context_map);
}

spdlog::level::level_enum LoggingSystem::to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO: return spdlog::level::info;
        case LogLevel::WARN: return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        default: return spdlog::level::info;
    }
}

// PerformanceTimer implementation
PerformanceTimer::PerformanceTimer(const std::string& operation_name)
    : operation_name_(operation_name), start_time_(std::chrono::steady_clock::now()) {
}

PerformanceTimer::~PerformanceTimer() {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    
    LoggingSystem::debug("performance:timer - Operation completed", {
        {"operation", operation_name_},
        {"duration_us", std::to_string(duration.count())},
        {"duration_ms", fmt::format("{:.3f}", duration.count() / 1000.0)}
    });
}

} // namespace sensor_daemon