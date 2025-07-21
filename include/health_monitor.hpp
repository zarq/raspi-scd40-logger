#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
#include "logging_system.hpp"

namespace sensor_daemon {

/**
 * Health status levels
 */
enum class HealthStatus {
    HEALTHY,    // All systems operating normally
    WARNING,    // Some issues detected but system still functional
    CRITICAL,   // Serious issues that may affect functionality
    FAILED      // System is not functioning properly
};

/**
 * Health check result for individual components
 */
struct HealthCheckResult {
    std::string component_name;
    HealthStatus status;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> details;
    
    HealthCheckResult(const std::string& name, HealthStatus stat, const std::string& msg)
        : component_name(name), status(stat), message(msg), 
          timestamp(std::chrono::system_clock::now()) {}
    
    HealthCheckResult& add_detail(const std::string& key, const std::string& value) {
        details[key] = value;
        return *this;
    }
};

/**
 * System-wide health status
 */
struct SystemHealthStatus {
    HealthStatus overall_status;
    std::chrono::system_clock::time_point last_check;
    std::vector<HealthCheckResult> component_results;
    PerformanceMetrics performance_metrics;
    
    // Get status summary string
    std::string get_status_string() const;
    
    // Get detailed health report
    std::string get_detailed_report() const;
    
    // Check if system is operational
    bool is_operational() const {
        return overall_status == HealthStatus::HEALTHY || 
               overall_status == HealthStatus::WARNING;
    }
};

/**
 * Alert configuration
 */
struct AlertConfig {
    bool enabled = true;
    std::chrono::minutes check_interval{5};
    std::chrono::minutes alert_cooldown{15};
    
    // Thresholds
    double max_memory_mb = 15.0;
    double max_cpu_percent = 75.0;
    double min_sensor_success_rate = 0.8;  // 80%
    double min_storage_success_rate = 0.95; // 95%
    uint64_t max_i2c_failures_per_hour = 10;
    
    // Alert destinations
    std::string log_level = "error";
    std::string systemd_status = "STATUS=Health check failed";
};

/**
 * Health monitoring and alerting system
 */
class HealthMonitor {
public:
    /**
     * Constructor
     */
    HealthMonitor();
    
    /**
     * Destructor
     */
    ~HealthMonitor();
    
    /**
     * Initialize health monitoring
     * @param config Alert configuration
     * @return true if initialization successful
     */
    bool initialize(const AlertConfig& config = AlertConfig{});
    
    /**
     * Shutdown health monitoring
     */
    void shutdown();
    
    /**
     * Register a health check function for a component
     * @param component_name Name of the component
     * @param check_function Function that returns health check result
     */
    void register_health_check(const std::string& component_name,
                              std::function<HealthCheckResult()> check_function);
    
    /**
     * Perform health checks on all registered components
     * @return System-wide health status
     */
    SystemHealthStatus check_system_health();
    
    /**
     * Get the last health check results
     * @return Last system health status
     */
    SystemHealthStatus get_last_health_status() const;
    
    /**
     * Update performance metrics for health monitoring
     * @param metrics Current performance metrics
     */
    void update_performance_metrics(const PerformanceMetrics& metrics);
    
    /**
     * Check if alerts should be triggered based on current status
     * @param status Current system health status
     */
    void check_and_trigger_alerts(const SystemHealthStatus& status);
    
    /**
     * Get health status as JSON string for external monitoring
     * @return JSON formatted health status
     */
    std::string get_health_status_json() const;
    
    /**
     * Write health status to a status file for external monitoring
     * @param file_path Path to status file
     * @return true if successful
     */
    bool write_status_file(const std::string& file_path) const;
    
    /**
     * Get diagnostic information for troubleshooting
     * @return Detailed diagnostic report
     */
    std::string get_diagnostic_report() const;
    
    /**
     * Check if system is healthy enough to continue operation
     * @return true if system should continue running
     */
    bool should_continue_operation() const;
    
    /**
     * Record a health event for tracking
     * @param component Component name
     * @param event_type Type of event
     * @param message Event message
     */
    void record_health_event(const std::string& component, 
                           const std::string& event_type,
                           const std::string& message);

private:
    AlertConfig config_;
    mutable std::mutex health_mutex_;
    std::atomic<bool> initialized_;
    
    // Health check functions
    std::unordered_map<std::string, std::function<HealthCheckResult()>> health_checks_;
    
    // Last health status
    SystemHealthStatus last_health_status_;
    
    // Alert tracking
    std::unordered_map<std::string, std::chrono::system_clock::time_point> last_alert_times_;
    
    // Health event history (for diagnostics)
    struct HealthEvent {
        std::chrono::system_clock::time_point timestamp;
        std::string component;
        std::string event_type;
        std::string message;
    };
    std::vector<HealthEvent> health_events_;
    static constexpr size_t MAX_HEALTH_EVENTS = 100;
    
    /**
     * Determine overall system health from component results
     * @param results Individual component health results
     * @return Overall system health status
     */
    HealthStatus determine_overall_status(const std::vector<HealthCheckResult>& results) const;
    
    /**
     * Check if alert should be sent (considering cooldown)
     * @param alert_type Type of alert
     * @return true if alert should be sent
     */
    bool should_send_alert(const std::string& alert_type) const;
    
    /**
     * Send alert notification
     * @param alert_type Type of alert
     * @param message Alert message
     * @param details Additional details
     */
    void send_alert(const std::string& alert_type, 
                   const std::string& message,
                   const std::unordered_map<std::string, std::string>& details = {});
    
    /**
     * Update systemd status
     * @param status Status message
     */
    void update_systemd_status(const std::string& status) const;
    
    /**
     * Clean up old health events
     */
    void cleanup_old_events();
    
    /**
     * Convert health status to string
     * @param status Health status enum
     * @return String representation
     */
    static std::string health_status_to_string(HealthStatus status);
    
    /**
     * Convert string to health status
     * @param status_str String representation
     * @return Health status enum
     */
    static HealthStatus string_to_health_status(const std::string& status_str);
};

/**
 * Built-in health check functions
 */
namespace health_checks {
    /**
     * Check system memory usage
     * @param max_memory_mb Maximum allowed memory in MB
     * @return Health check result
     */
    HealthCheckResult check_memory_usage(double max_memory_mb);
    
    /**
     * Check system CPU usage
     * @param max_cpu_percent Maximum allowed CPU percentage
     * @return Health check result
     */
    HealthCheckResult check_cpu_usage(double max_cpu_percent);
    
    /**
     * Check disk space availability
     * @param data_directory Directory to check
     * @param min_free_mb Minimum free space in MB
     * @return Health check result
     */
    HealthCheckResult check_disk_space(const std::string& data_directory, double min_free_mb = 100.0);
    
    /**
     * Check sensor connectivity and performance
     * @param sensor_interface Sensor interface to check
     * @param min_success_rate Minimum success rate (0.0-1.0)
     * @return Health check result
     */
    HealthCheckResult check_sensor_health(const class SCD40Interface* sensor_interface, 
                                        double min_success_rate);
    
    /**
     * Check storage engine health
     * @param storage Storage engine to check
     * @param min_success_rate Minimum success rate (0.0-1.0)
     * @return Health check result
     */
    HealthCheckResult check_storage_health(const class TimeSeriesStorage* storage,
                                         double min_success_rate);
}

/**
 * Enhanced metrics collector for detailed performance monitoring
 */
class MetricsCollector {
public:
    /**
     * Constructor
     */
    MetricsCollector();
    
    /**
     * Record sensor reading attempt
     * @param success Whether the reading was successful
     * @param duration Time taken for the reading
     */
    void record_sensor_reading(bool success, std::chrono::milliseconds duration);
    
    /**
     * Record storage operation
     * @param success Whether the operation was successful
     * @param duration Time taken for the operation
     */
    void record_storage_operation(bool success, std::chrono::milliseconds duration);
    
    /**
     * Record I2C communication event
     * @param success Whether the communication was successful
     * @param error_code Error code if failed
     */
    void record_i2c_event(bool success, int error_code = 0);
    
    /**
     * Get sensor reading success rate over the last period
     * @param period Time period to calculate rate for
     * @return Success rate (0.0 to 1.0)
     */
    double get_sensor_success_rate(std::chrono::minutes period = std::chrono::minutes(60)) const;
    
    /**
     * Get storage operation success rate over the last period
     * @param period Time period to calculate rate for
     * @return Success rate (0.0 to 1.0)
     */
    double get_storage_success_rate(std::chrono::minutes period = std::chrono::minutes(60)) const;
    
    /**
     * Get average sensor reading duration
     * @param period Time period to calculate average for
     * @return Average duration in milliseconds
     */
    std::chrono::milliseconds get_avg_sensor_duration(std::chrono::minutes period = std::chrono::minutes(60)) const;
    
    /**
     * Get average storage operation duration
     * @param period Time period to calculate average for
     * @return Average duration in milliseconds
     */
    std::chrono::milliseconds get_avg_storage_duration(std::chrono::minutes period = std::chrono::minutes(60)) const;
    
    /**
     * Get I2C error statistics
     * @param period Time period to get statistics for
     * @return Map of error codes to occurrence counts
     */
    std::unordered_map<int, uint64_t> get_i2c_error_stats(std::chrono::minutes period = std::chrono::minutes(60)) const;
    
    /**
     * Get comprehensive metrics report
     * @return JSON formatted metrics report
     */
    std::string get_metrics_report() const;
    
    /**
     * Reset all metrics
     */
    void reset();

private:
    struct TimedEvent {
        std::chrono::system_clock::time_point timestamp;
        bool success;
        std::chrono::milliseconds duration;
        int error_code;
        
        TimedEvent(bool s, std::chrono::milliseconds d, int e = 0)
            : timestamp(std::chrono::system_clock::now()), success(s), duration(d), error_code(e) {}
    };
    
    mutable std::mutex metrics_mutex_;
    std::vector<TimedEvent> sensor_events_;
    std::vector<TimedEvent> storage_events_;
    std::vector<TimedEvent> i2c_events_;
    
    static constexpr size_t MAX_EVENTS = 1000;
    
    /**
     * Clean up old events beyond the retention period
     */
    void cleanup_old_events();
    
    /**
     * Calculate success rate for a set of events
     * @param events Events to analyze
     * @param period Time period to consider
     * @return Success rate (0.0 to 1.0)
     */
    double calculate_success_rate(const std::vector<TimedEvent>& events, std::chrono::minutes period) const;
    
    /**
     * Calculate average duration for successful events
     * @param events Events to analyze
     * @param period Time period to consider
     * @return Average duration
     */
    std::chrono::milliseconds calculate_avg_duration(const std::vector<TimedEvent>& events, std::chrono::minutes period) const;
};

} // namespace sensor_daemon