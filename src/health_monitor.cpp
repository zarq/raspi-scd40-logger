#include "health_monitor.hpp"
#include "scd40_interface.hpp"
#include "time_series_storage.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <sys/statvfs.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

namespace sensor_daemon {

// SystemHealthStatus implementation
std::string SystemHealthStatus::get_status_string() const {
    switch (overall_status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::WARNING: return "WARNING";
        case HealthStatus::CRITICAL: return "CRITICAL";
        case HealthStatus::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

std::string SystemHealthStatus::get_detailed_report() const {
    std::ostringstream report;
    
    report << "=== System Health Report ===\n";
    report << "Overall Status: " << get_status_string() << "\n";
    report << "Last Check: " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - last_check).count() << " seconds ago\n";
    report << "Uptime: " << performance_metrics.get_uptime().count() << " seconds\n\n";
    
    report << "=== Performance Metrics ===\n";
    report << "Memory Usage: " << (performance_metrics.memory_usage_bytes / 1024.0 / 1024.0) << " MB\n";
    report << "CPU Usage: " << performance_metrics.cpu_usage_percent << "%\n";
    report << "Sensor Success Rate: " << (performance_metrics.get_sensor_success_rate() * 100.0) << "%\n";
    report << "Storage Success Rate: " << (performance_metrics.get_storage_success_rate() * 100.0) << "%\n";
    report << "I2C Connection Failures: " << performance_metrics.i2c_connection_failures << "\n\n";
    
    report << "=== Component Health ===\n";
    for (const auto& result : component_results) {
        report << result.component_name << ": " << HealthMonitor::health_status_to_string(result.status);
        if (!result.message.empty()) {
            report << " - " << result.message;
        }
        report << "\n";
        
        for (const auto& [key, value] : result.details) {
            report << "  " << key << ": " << value << "\n";
        }
    }
    
    return report.str();
}

// HealthMonitor implementation
HealthMonitor::HealthMonitor() : initialized_(false) {
    last_health_status_.overall_status = HealthStatus::HEALTHY;
    last_health_status_.last_check = std::chrono::system_clock::now();
}

HealthMonitor::~HealthMonitor() {
    shutdown();
}

bool HealthMonitor::initialize(const AlertConfig& config) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    config_ = config;
    initialized_ = true;
    
    LOG_INFO("Health monitor initialized", {
        {"check_interval_minutes", std::to_string(config_.check_interval.count())},
        {"alert_cooldown_minutes", std::to_string(config_.alert_cooldown.count())},
        {"max_memory_mb", std::to_string(config_.max_memory_mb)},
        {"max_cpu_percent", std::to_string(config_.max_cpu_percent)}
    });
    
    return true;
}

void HealthMonitor::shutdown() {
    if (initialized_) {
        std::lock_guard<std::mutex> lock(health_mutex_);
        initialized_ = false;
        health_checks_.clear();
        LOG_INFO("Health monitor shutdown completed");
    }
}

void HealthMonitor::register_health_check(const std::string& component_name,
                                        std::function<HealthCheckResult()> check_function) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    health_checks_[component_name] = check_function;
    
    LOG_DEBUG("Health check registered", {
        {"component", component_name}
    });
}

SystemHealthStatus HealthMonitor::check_system_health() {
    if (!initialized_) {
        SystemHealthStatus status;
        status.overall_status = HealthStatus::FAILED;
        status.last_check = std::chrono::system_clock::now();
        return status;
    }
    
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    SystemHealthStatus status;
    status.last_check = std::chrono::system_clock::now();
    status.performance_metrics = last_health_status_.performance_metrics;
    
    // Run all registered health checks
    for (const auto& [component_name, check_function] : health_checks_) {
        try {
            HealthCheckResult result = check_function();
            status.component_results.push_back(result);
            
            LOG_DEBUG("Health check completed", {
                {"component", component_name},
                {"status", health_status_to_string(result.status)},
                {"message", result.message}
            });
            
        } catch (const std::exception& e) {
            HealthCheckResult error_result(component_name, HealthStatus::FAILED, 
                                         "Health check exception: " + std::string(e.what()));
            status.component_results.push_back(error_result);
            
            LOG_ERROR("Health check failed with exception", {
                {"component", component_name},
                {"error", e.what()}
            });
        }
    }
    
    // Determine overall status
    status.overall_status = determine_overall_status(status.component_results);
    
    // Update last status
    last_health_status_ = status;
    
    // Check for alerts
    if (config_.enabled) {
        check_and_trigger_alerts(status);
    }
    
    LOG_DEBUG("System health check completed", {
        {"overall_status", health_status_to_string(status.overall_status)},
        {"component_count", std::to_string(status.component_results.size())}
    });
    
    return status;
}

SystemHealthStatus HealthMonitor::get_last_health_status() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return last_health_status_;
}

void HealthMonitor::update_performance_metrics(const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    last_health_status_.performance_metrics = metrics;
}

void HealthMonitor::check_and_trigger_alerts(const SystemHealthStatus& status) {
    // Check memory usage
    double memory_mb = status.performance_metrics.memory_usage_bytes / 1024.0 / 1024.0;
    if (memory_mb > config_.max_memory_mb && should_send_alert("memory_usage")) {
        send_alert("memory_usage", 
                  "Memory usage exceeds threshold",
                  {{"current_mb", std::to_string(memory_mb)},
                   {"threshold_mb", std::to_string(config_.max_memory_mb)}});
    }
    
    // Check CPU usage
    if (status.performance_metrics.cpu_usage_percent > config_.max_cpu_percent && 
        should_send_alert("cpu_usage")) {
        send_alert("cpu_usage",
                  "CPU usage exceeds threshold",
                  {{"current_percent", std::to_string(status.performance_metrics.cpu_usage_percent)},
                   {"threshold_percent", std::to_string(config_.max_cpu_percent)}});
    }
    
    // Check sensor success rate
    double sensor_rate = status.performance_metrics.get_sensor_success_rate();
    if (sensor_rate < config_.min_sensor_success_rate && should_send_alert("sensor_success_rate")) {
        send_alert("sensor_success_rate",
                  "Sensor success rate below threshold",
                  {{"current_rate", std::to_string(sensor_rate * 100.0)},
                   {"threshold_rate", std::to_string(config_.min_sensor_success_rate * 100.0)}});
    }
    
    // Check storage success rate
    double storage_rate = status.performance_metrics.get_storage_success_rate();
    if (storage_rate < config_.min_storage_success_rate && should_send_alert("storage_success_rate")) {
        send_alert("storage_success_rate",
                  "Storage success rate below threshold",
                  {{"current_rate", std::to_string(storage_rate * 100.0)},
                   {"threshold_rate", std::to_string(config_.min_storage_success_rate * 100.0)}});
    }
    
    // Check overall system status
    if (status.overall_status == HealthStatus::CRITICAL && should_send_alert("system_critical")) {
        send_alert("system_critical",
                  "System health is critical",
                  {{"component_count", std::to_string(status.component_results.size())}});
    }
    
    if (status.overall_status == HealthStatus::FAILED && should_send_alert("system_failed")) {
        send_alert("system_failed",
                  "System health check failed",
                  {{"component_count", std::to_string(status.component_results.size())}});
    }
}

std::string HealthMonitor::get_health_status_json() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    std::ostringstream json;
    json << "{\n";
    json << "  \"overall_status\": \"" << last_health_status_.get_status_string() << "\",\n";
    json << "  \"last_check\": " << std::chrono::duration_cast<std::chrono::seconds>(
        last_health_status_.last_check.time_since_epoch()).count() << ",\n";
    json << "  \"uptime_seconds\": " << last_health_status_.performance_metrics.get_uptime().count() << ",\n";
    json << "  \"performance\": {\n";
    json << "    \"memory_usage_mb\": " << (last_health_status_.performance_metrics.memory_usage_bytes / 1024.0 / 1024.0) << ",\n";
    json << "    \"cpu_usage_percent\": " << last_health_status_.performance_metrics.cpu_usage_percent << ",\n";
    json << "    \"sensor_success_rate\": " << last_health_status_.performance_metrics.get_sensor_success_rate() << ",\n";
    json << "    \"storage_success_rate\": " << last_health_status_.performance_metrics.get_storage_success_rate() << ",\n";
    json << "    \"i2c_connection_failures\": " << last_health_status_.performance_metrics.i2c_connection_failures << "\n";
    json << "  },\n";
    json << "  \"components\": [\n";
    
    for (size_t i = 0; i < last_health_status_.component_results.size(); ++i) {
        const auto& result = last_health_status_.component_results[i];
        json << "    {\n";
        json << "      \"name\": \"" << result.component_name << "\",\n";
        json << "      \"status\": \"" << health_status_to_string(result.status) << "\",\n";
        json << "      \"message\": \"" << result.message << "\",\n";
        json << "      \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
            result.timestamp.time_since_epoch()).count() << "\n";
        json << "    }";
        if (i < last_health_status_.component_results.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    return json.str();
}

bool HealthMonitor::write_status_file(const std::string& file_path) const {
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        file << get_health_status_json();
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write status file", {
            {"file_path", file_path},
            {"error", e.what()}
        });
        return false;
    }
}

std::string HealthMonitor::get_diagnostic_report() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    std::ostringstream report;
    
    report << "=== Diagnostic Report ===\n";
    report << "Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";
    
    // System health summary
    report << last_health_status_.get_detailed_report() << "\n";
    
    // Recent health events
    report << "=== Recent Health Events ===\n";
    auto now = std::chrono::system_clock::now();
    for (const auto& event : health_events_) {
        auto age = std::chrono::duration_cast<std::chrono::minutes>(now - event.timestamp);
        report << "[" << age.count() << "m ago] " << event.component 
               << ":" << event.event_type << " - " << event.message << "\n";
    }
    
    // Alert configuration
    report << "\n=== Alert Configuration ===\n";
    report << "Enabled: " << (config_.enabled ? "true" : "false") << "\n";
    report << "Check Interval: " << config_.check_interval.count() << " minutes\n";
    report << "Alert Cooldown: " << config_.alert_cooldown.count() << " minutes\n";
    report << "Memory Threshold: " << config_.max_memory_mb << " MB\n";
    report << "CPU Threshold: " << config_.max_cpu_percent << "%\n";
    report << "Min Sensor Success Rate: " << (config_.min_sensor_success_rate * 100.0) << "%\n";
    report << "Min Storage Success Rate: " << (config_.min_storage_success_rate * 100.0) << "%\n";
    
    return report.str();
}

bool HealthMonitor::should_continue_operation() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    return last_health_status_.is_operational();
}

void HealthMonitor::record_health_event(const std::string& component,
                                      const std::string& event_type,
                                      const std::string& message) {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    HealthEvent event;
    event.timestamp = std::chrono::system_clock::now();
    event.component = component;
    event.event_type = event_type;
    event.message = message;
    
    health_events_.push_back(event);
    
    // Keep only recent events
    if (health_events_.size() > MAX_HEALTH_EVENTS) {
        health_events_.erase(health_events_.begin());
    }
    
    LOG_DEBUG("Health event recorded", {
        {"component", component},
        {"event_type", event_type},
        {"message", message}
    });
}

HealthStatus HealthMonitor::determine_overall_status(const std::vector<HealthCheckResult>& results) const {
    if (results.empty()) {
        return HealthStatus::WARNING;
    }
    
    bool has_failed = false;
    bool has_critical = false;
    bool has_warning = false;
    
    for (const auto& result : results) {
        switch (result.status) {
            case HealthStatus::FAILED:
                has_failed = true;
                break;
            case HealthStatus::CRITICAL:
                has_critical = true;
                break;
            case HealthStatus::WARNING:
                has_warning = true;
                break;
            case HealthStatus::HEALTHY:
                break;
        }
    }
    
    if (has_failed) return HealthStatus::FAILED;
    if (has_critical) return HealthStatus::CRITICAL;
    if (has_warning) return HealthStatus::WARNING;
    return HealthStatus::HEALTHY;
}

bool HealthMonitor::should_send_alert(const std::string& alert_type) const {
    auto it = last_alert_times_.find(alert_type);
    if (it == last_alert_times_.end()) {
        return true; // First alert of this type
    }
    
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - it->second);
    
    return elapsed >= config_.alert_cooldown;
}

void HealthMonitor::send_alert(const std::string& alert_type,
                             const std::string& message,
                             const std::unordered_map<std::string, std::string>& details) {
    // Update alert time
    last_alert_times_[alert_type] = std::chrono::system_clock::now();
    
    // Enhanced alert context with troubleshooting suggestions
    std::unordered_map<std::string, std::string> log_context = {
        {"alert_type", alert_type},
        {"alert_timestamp", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())}
    };
    log_context.insert(details.begin(), details.end());
    
    // Add troubleshooting suggestions based on alert type
    if (alert_type == "memory_usage") {
        log_context["troubleshooting"] = "Check for memory leaks, restart daemon if necessary, monitor memory growth patterns";
        log_context["command_suggestion"] = "sudo systemctl restart sensor-daemon";
    } else if (alert_type == "cpu_usage") {
        log_context["troubleshooting"] = "Check system load, verify no runaway processes, consider reducing sampling frequency";
        log_context["command_suggestion"] = "htop; check sensor daemon configuration";
    } else if (alert_type == "sensor_success_rate") {
        log_context["troubleshooting"] = "Check I2C connections, verify sensor power, run I2C bus scan";
        log_context["command_suggestion"] = "sudo i2cdetect -y 1; sudo sensor-daemon-diagnostic i2c";
    } else if (alert_type == "storage_success_rate") {
        log_context["troubleshooting"] = "Check disk space, verify database integrity, check file permissions";
        log_context["command_suggestion"] = "df -h; sudo sensor-daemon-diagnostic storage";
    } else if (alert_type == "system_critical") {
        log_context["troubleshooting"] = "Run comprehensive diagnostics, check system resources, review recent logs";
        log_context["command_suggestion"] = "sudo sensor-daemon-diagnostic; journalctl -u sensor-daemon --since '1 hour ago'";
    }
    
    // Log alert with enhanced context
    if (config_.log_level == "critical") {
        LOG_CRITICAL("ALERT: " + message, log_context);
    } else {
        LOG_ERROR("ALERT: " + message, log_context);
    }
    
    // Update systemd status with more detailed information
    std::string systemd_message = config_.systemd_status + ": " + alert_type;
    if (details.find("current_value") != details.end()) {
        systemd_message += " (current: " + details.at("current_value") + ")";
    }
    update_systemd_status(systemd_message);
    
    // Record health event with enhanced details
    std::ostringstream event_message;
    event_message << alert_type << ": " << message;
    for (const auto& [key, value] : details) {
        event_message << " | " << key << "=" << value;
    }
    record_health_event("health_monitor", "alert", event_message.str());
    
    // For critical alerts, also record recovery suggestions
    if (alert_type == "system_critical" || alert_type == "system_failed") {
        record_health_event("health_monitor", "recovery_suggestion", 
                           "Run: sudo sensor-daemon-diagnostic for detailed analysis");
    }
}

void HealthMonitor::update_systemd_status(const std::string& status) const {
#ifdef HAVE_SYSTEMD
    if (sd_notify(0, status.c_str()) < 0) {
        LOG_DEBUG("Failed to notify systemd", {{"status", status}});
    }
#else
    (void)status; // Suppress unused parameter warning
#endif
}

void HealthMonitor::cleanup_old_events() {
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24);
    
    health_events_.erase(
        std::remove_if(health_events_.begin(), health_events_.end(),
                      [cutoff](const HealthEvent& event) {
                          return event.timestamp < cutoff;
                      }),
        health_events_.end()
    );
}

std::string HealthMonitor::health_status_to_string(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::WARNING: return "WARNING";
        case HealthStatus::CRITICAL: return "CRITICAL";
        case HealthStatus::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

HealthStatus HealthMonitor::string_to_health_status(const std::string& status_str) {
    if (status_str == "HEALTHY") return HealthStatus::HEALTHY;
    if (status_str == "WARNING") return HealthStatus::WARNING;
    if (status_str == "CRITICAL") return HealthStatus::CRITICAL;
    if (status_str == "FAILED") return HealthStatus::FAILED;
    return HealthStatus::WARNING; // Default fallback
}

// Built-in health check functions
namespace health_checks {

HealthCheckResult check_memory_usage(double max_memory_mb) {
    try {
        std::ifstream status_file("/proc/self/status");
        std::string line;
        
        while (std::getline(status_file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                
                double memory_mb = std::stod(value) / 1024.0; // Convert KB to MB
                
                HealthCheckResult result("memory", HealthStatus::HEALTHY, "Memory usage normal");
                result.add_detail("current_mb", std::to_string(memory_mb))
                      .add_detail("max_mb", std::to_string(max_memory_mb))
                      .add_detail("usage_percent", std::to_string((memory_mb / max_memory_mb) * 100.0));
                
                if (memory_mb > max_memory_mb * 1.2) {
                    result.status = HealthStatus::CRITICAL;
                    result.message = "Memory usage critically high";
                } else if (memory_mb > max_memory_mb) {
                    result.status = HealthStatus::WARNING;
                    result.message = "Memory usage above threshold";
                }
                
                return result;
            }
        }
        
        return HealthCheckResult("memory", HealthStatus::WARNING, "Unable to read memory usage");
        
    } catch (const std::exception& e) {
        return HealthCheckResult("memory", HealthStatus::FAILED, 
                               "Memory check failed: " + std::string(e.what()));
    }
}

HealthCheckResult check_cpu_usage(double max_cpu_percent) {
    try {
        static auto last_time = std::chrono::steady_clock::now();
        static uint64_t last_cpu_time = 0;
        
        std::ifstream stat_file("/proc/self/stat");
        std::string line;
        std::getline(stat_file, line);
        
        std::istringstream iss(line);
        std::string token;
        uint64_t utime = 0, stime = 0;
        
        // Skip to the 14th and 15th fields (utime and stime)
        for (int i = 0; i < 13; ++i) {
            iss >> token;
        }
        iss >> utime >> stime;
        
        uint64_t total_cpu_time = utime + stime;
        auto current_time = std::chrono::steady_clock::now();
        
        auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();
        uint64_t cpu_diff = total_cpu_time - last_cpu_time;
        
        double cpu_usage = 0.0;
        if (time_diff > 0 && last_cpu_time > 0) {
            long clock_ticks_per_sec = sysconf(_SC_CLK_TCK);
            cpu_usage = (static_cast<double>(cpu_diff) / clock_ticks_per_sec) / (time_diff / 1000.0) * 100.0;
        }
        
        last_time = current_time;
        last_cpu_time = total_cpu_time;
        
        HealthCheckResult result("cpu", HealthStatus::HEALTHY, "CPU usage normal");
        result.add_detail("current_percent", std::to_string(cpu_usage))
              .add_detail("max_percent", std::to_string(max_cpu_percent));
        
        if (cpu_usage > max_cpu_percent * 1.2) {
            result.status = HealthStatus::CRITICAL;
            result.message = "CPU usage critically high";
        } else if (cpu_usage > max_cpu_percent) {
            result.status = HealthStatus::WARNING;
            result.message = "CPU usage above threshold";
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return HealthCheckResult("cpu", HealthStatus::FAILED,
                               "CPU check failed: " + std::string(e.what()));
    }
}

HealthCheckResult check_disk_space(const std::string& data_directory, double min_free_mb) {
    try {
        struct statvfs stat;
        
        if (statvfs(data_directory.c_str(), &stat) != 0) {
            return HealthCheckResult("disk", HealthStatus::WARNING, 
                                   "Unable to check disk space");
        }
        
        uint64_t available_bytes = stat.f_bavail * stat.f_frsize;
        double available_mb = available_bytes / 1024.0 / 1024.0;
        
        HealthCheckResult result("disk", HealthStatus::HEALTHY, "Disk space sufficient");
        result.add_detail("available_mb", std::to_string(available_mb))
              .add_detail("min_free_mb", std::to_string(min_free_mb))
              .add_detail("data_directory", data_directory);
        
        if (available_mb < min_free_mb * 0.5) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Disk space critically low";
        } else if (available_mb < min_free_mb) {
            result.status = HealthStatus::WARNING;
            result.message = "Disk space running low";
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return HealthCheckResult("disk", HealthStatus::FAILED,
                               "Disk check failed: " + std::string(e.what()));
    }
}

HealthCheckResult check_sensor_health(const SCD40Interface* sensor_interface, double min_success_rate) {
    try {
        if (!sensor_interface) {
            return HealthCheckResult("sensor", HealthStatus::FAILED, "Sensor interface not available");
        }
        
        auto stats = sensor_interface->get_stats();
        uint64_t total_reads = stats.successful_reads + stats.failed_reads;
        double success_rate = total_reads > 0 ? 
            static_cast<double>(stats.successful_reads) / total_reads : 0.0;
        
        bool is_connected = sensor_interface->is_connected();
        std::string last_error = sensor_interface->get_last_error();
        
        HealthCheckResult result("sensor", HealthStatus::HEALTHY, "Sensor operating normally");
        result.add_detail("connected", is_connected ? "true" : "false")
              .add_detail("success_rate", std::to_string(success_rate))
              .add_detail("successful_reads", std::to_string(stats.successful_reads))
              .add_detail("failed_reads", std::to_string(stats.failed_reads))
              .add_detail("reconnection_attempts", std::to_string(stats.reconnection_attempts));
        
        if (!last_error.empty()) {
            result.add_detail("last_error", last_error);
        }
        
        if (!is_connected) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Sensor not connected";
        } else if (success_rate < min_success_rate * 0.5) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Sensor success rate critically low";
        } else if (success_rate < min_success_rate) {
            result.status = HealthStatus::WARNING;
            result.message = "Sensor success rate below threshold";
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return HealthCheckResult("sensor", HealthStatus::FAILED,
                               "Sensor check failed: " + std::string(e.what()));
    }
}

HealthCheckResult check_storage_health(const TimeSeriesStorage* storage, double min_success_rate) {
    try {
        if (!storage) {
            return HealthCheckResult("storage", HealthStatus::FAILED, "Storage engine not available");
        }
        
        bool is_healthy = storage->is_healthy();
        uint64_t db_size = storage->get_database_size();
        
        HealthCheckResult result("storage", HealthStatus::HEALTHY, "Storage operating normally");
        result.add_detail("healthy", is_healthy ? "true" : "false")
              .add_detail("database_size_mb", std::to_string(db_size / 1024.0 / 1024.0));
        
        if (!is_healthy) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Storage engine reports unhealthy status";
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return HealthCheckResult("storage", HealthStatus::FAILED,
                               "Storage check failed: " + std::string(e.what()));
    }
}

} // namespace health_checks

// MetricsCollector implementation
MetricsCollector::MetricsCollector() {
    // Reserve space for events to avoid frequent reallocations
    sensor_events_.reserve(MAX_EVENTS);
    storage_events_.reserve(MAX_EVENTS);
    i2c_events_.reserve(MAX_EVENTS);
}

void MetricsCollector::record_sensor_reading(bool success, std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    sensor_events_.emplace_back(success, duration);
    
    // Keep only recent events
    if (sensor_events_.size() > MAX_EVENTS) {
        sensor_events_.erase(sensor_events_.begin());
    }
    
    cleanup_old_events();
}

void MetricsCollector::record_storage_operation(bool success, std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    storage_events_.emplace_back(success, duration);
    
    // Keep only recent events
    if (storage_events_.size() > MAX_EVENTS) {
        storage_events_.erase(storage_events_.begin());
    }
    
    cleanup_old_events();
}

void MetricsCollector::record_i2c_event(bool success, int error_code) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    i2c_events_.emplace_back(success, std::chrono::milliseconds(0), error_code);
    
    // Keep only recent events
    if (i2c_events_.size() > MAX_EVENTS) {
        i2c_events_.erase(i2c_events_.begin());
    }
    
    cleanup_old_events();
}

double MetricsCollector::get_sensor_success_rate(std::chrono::minutes period) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return calculate_success_rate(sensor_events_, period);
}

double MetricsCollector::get_storage_success_rate(std::chrono::minutes period) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return calculate_success_rate(storage_events_, period);
}

std::chrono::milliseconds MetricsCollector::get_avg_sensor_duration(std::chrono::minutes period) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return calculate_avg_duration(sensor_events_, period);
}

std::chrono::milliseconds MetricsCollector::get_avg_storage_duration(std::chrono::minutes period) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return calculate_avg_duration(storage_events_, period);
}

std::unordered_map<int, uint64_t> MetricsCollector::get_i2c_error_stats(std::chrono::minutes period) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    std::unordered_map<int, uint64_t> error_stats;
    auto cutoff = std::chrono::system_clock::now() - period;
    
    for (const auto& event : i2c_events_) {
        if (event.timestamp >= cutoff && !event.success) {
            error_stats[event.error_code]++;
        }
    }
    
    return error_stats;
}

std::string MetricsCollector::get_metrics_report() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    std::ostringstream report;
    
    report << "{\n";
    report << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    
    // Sensor metrics
    double sensor_rate_1h = calculate_success_rate(sensor_events_, std::chrono::minutes(60));
    double sensor_rate_24h = calculate_success_rate(sensor_events_, std::chrono::minutes(1440));
    auto sensor_avg_duration = calculate_avg_duration(sensor_events_, std::chrono::minutes(60));
    
    report << "  \"sensor_metrics\": {\n";
    report << "    \"success_rate_1h\": " << sensor_rate_1h << ",\n";
    report << "    \"success_rate_24h\": " << sensor_rate_24h << ",\n";
    report << "    \"avg_duration_ms\": " << sensor_avg_duration.count() << ",\n";
    report << "    \"total_events\": " << sensor_events_.size() << "\n";
    report << "  },\n";
    
    // Storage metrics
    double storage_rate_1h = calculate_success_rate(storage_events_, std::chrono::minutes(60));
    double storage_rate_24h = calculate_success_rate(storage_events_, std::chrono::minutes(1440));
    auto storage_avg_duration = calculate_avg_duration(storage_events_, std::chrono::minutes(60));
    
    report << "  \"storage_metrics\": {\n";
    report << "    \"success_rate_1h\": " << storage_rate_1h << ",\n";
    report << "    \"success_rate_24h\": " << storage_rate_24h << ",\n";
    report << "    \"avg_duration_ms\": " << storage_avg_duration.count() << ",\n";
    report << "    \"total_events\": " << storage_events_.size() << "\n";
    report << "  },\n";
    
    // I2C metrics
    auto i2c_errors_1h = get_i2c_error_stats(std::chrono::minutes(60));
    double i2c_rate_1h = calculate_success_rate(i2c_events_, std::chrono::minutes(60));
    
    report << "  \"i2c_metrics\": {\n";
    report << "    \"success_rate_1h\": " << i2c_rate_1h << ",\n";
    report << "    \"total_events\": " << i2c_events_.size() << ",\n";
    report << "    \"error_stats_1h\": {\n";
    
    bool first_error = true;
    for (const auto& [error_code, count] : i2c_errors_1h) {
        if (!first_error) report << ",\n";
        report << "      \"" << error_code << "\": " << count;
        first_error = false;
    }
    
    report << "\n    }\n";
    report << "  }\n";
    report << "}\n";
    
    return report.str();
}

void MetricsCollector::reset() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    sensor_events_.clear();
    storage_events_.clear();
    i2c_events_.clear();
}

void MetricsCollector::cleanup_old_events() {
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24);
    
    // Remove events older than 24 hours
    sensor_events_.erase(
        std::remove_if(sensor_events_.begin(), sensor_events_.end(),
                      [cutoff](const TimedEvent& event) {
                          return event.timestamp < cutoff;
                      }),
        sensor_events_.end()
    );
    
    storage_events_.erase(
        std::remove_if(storage_events_.begin(), storage_events_.end(),
                      [cutoff](const TimedEvent& event) {
                          return event.timestamp < cutoff;
                      }),
        storage_events_.end()
    );
    
    i2c_events_.erase(
        std::remove_if(i2c_events_.begin(), i2c_events_.end(),
                      [cutoff](const TimedEvent& event) {
                          return event.timestamp < cutoff;
                      }),
        i2c_events_.end()
    );
}

double MetricsCollector::calculate_success_rate(const std::vector<TimedEvent>& events, std::chrono::minutes period) const {
    if (events.empty()) {
        return 0.0;
    }
    
    auto cutoff = std::chrono::system_clock::now() - period;
    uint64_t total_events = 0;
    uint64_t successful_events = 0;
    
    for (const auto& event : events) {
        if (event.timestamp >= cutoff) {
            total_events++;
            if (event.success) {
                successful_events++;
            }
        }
    }
    
    return total_events > 0 ? static_cast<double>(successful_events) / total_events : 0.0;
}

std::chrono::milliseconds MetricsCollector::calculate_avg_duration(const std::vector<TimedEvent>& events, std::chrono::minutes period) const {
    if (events.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    auto cutoff = std::chrono::system_clock::now() - period;
    uint64_t total_duration = 0;
    uint64_t successful_events = 0;
    
    for (const auto& event : events) {
        if (event.timestamp >= cutoff && event.success) {
            total_duration += event.duration.count();
            successful_events++;
        }
    }
    
    return successful_events > 0 ? 
        std::chrono::milliseconds(total_duration / successful_events) : 
        std::chrono::milliseconds(0);
}

} // namespace sensor_daemon