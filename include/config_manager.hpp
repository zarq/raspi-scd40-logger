#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <toml.hpp>

namespace sensor_daemon {

/**
 * Configuration structure containing all daemon settings
 */
struct DaemonConfig {
    struct DaemonSettings {
        std::chrono::seconds sampling_interval{30};
        std::chrono::hours data_retention{24 * 365};
        std::string log_level{"info"};
    } daemon;
    
    struct SensorSettings {
        std::string i2c_device{"/dev/i2c-1"};
        uint8_t i2c_address{0x62};
        std::chrono::milliseconds connection_timeout{1000};
        int max_retries{3};
    } sensor;
    
    struct StorageSettings {
        std::string data_directory{"/var/lib/sensor-daemon"};
        std::chrono::hours file_rotation{24};
        bool compression_enabled{true};
        size_t max_memory_cache_mb{5};
    } storage;
    
    struct AlertSettings {
        bool enabled{true};
        int check_interval_minutes{5};
        int alert_cooldown_minutes{15};
        double memory_usage_threshold_mb{15.0};
        double cpu_usage_threshold_percent{75.0};
        double min_sensor_success_rate{0.8};
        double min_storage_success_rate{0.95};
        int sensor_failure_threshold{10};
        int disk_usage_threshold_percent{90};
        int write_failure_threshold_per_hour{5};
    } alerts;
    
    struct MonitoringSettings {
        bool health_endpoint_enabled{true};
        std::string health_status_file{"/var/run/sensor-daemon/health.json"};
        int health_update_interval_seconds{60};
        bool include_detailed_metrics{true};
        bool http_server_enabled{false};
        int http_server_port{8080};
        std::string http_server_bind_address{"127.0.0.1"};
    } monitoring;
};

/**
 * Exception thrown when configuration parsing or validation fails
 */
class ConfigurationError : public std::runtime_error {
public:
    explicit ConfigurationError(const std::string& message) 
        : std::runtime_error(message) {}
};

/**
 * Configuration manager for parsing TOML configuration files
 */
class ConfigManager {
public:
    /**
     * Load configuration from file
     * @param config_path Path to TOML configuration file
     * @return Parsed and validated configuration
     * @throws ConfigurationError if file cannot be parsed or validation fails
     */
    static DaemonConfig load_config(const std::string& config_path);
    
    /**
     * Get default configuration
     * @return Configuration with default values
     */
    static DaemonConfig get_default_config();
    
    /**
     * Validate configuration values
     * @param config Configuration to validate
     * @throws ConfigurationError if validation fails
     */
    static void validate_config(const DaemonConfig& config);
    
private:
    /**
     * Parse daemon section from TOML
     */
    static void parse_daemon_section(const toml::value& toml_data, DaemonConfig& config);
    
    /**
     * Parse sensor section from TOML
     */
    static void parse_sensor_section(const toml::value& toml_data, DaemonConfig& config);
    
    /**
     * Parse storage section from TOML
     */
    static void parse_storage_section(const toml::value& toml_data, DaemonConfig& config);
    
    /**
     * Parse alerts section from TOML
     */
    static void parse_alerts_section(const toml::value& toml_data, DaemonConfig& config);
    
    /**
     * Parse monitoring section from TOML
     */
    static void parse_monitoring_section(const toml::value& toml_data, DaemonConfig& config);
    
    /**
     * Validate log level string
     */
    static bool is_valid_log_level(const std::string& level);
    
    /**
     * Validate I2C address
     */
    static bool is_valid_i2c_address(uint8_t address);
    
    /**
     * Validate file path accessibility
     */
    static bool is_valid_path(const std::string& path, bool must_exist = false);
};

} // namespace sensor_daemon