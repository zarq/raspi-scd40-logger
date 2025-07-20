#include "config_manager.hpp"
#include <toml.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace sensor_daemon {

DaemonConfig ConfigManager::load_config(const std::string& config_path) {
    DaemonConfig config = get_default_config();
    
    // Check if file exists
    if (!std::filesystem::exists(config_path)) {
        throw ConfigurationError("Configuration file not found: " + config_path);
    }
    
    try {
        // Parse TOML file
        const auto toml_data = toml::parse(config_path);
        
        // Parse each section
        parse_daemon_section(toml_data, config);
        parse_sensor_section(toml_data, config);
        parse_storage_section(toml_data, config);
        
        // Validate the final configuration
        validate_config(config);
        
        return config;
    }
    catch (const toml::syntax_error& e) {
        throw ConfigurationError("TOML syntax error: " + std::string(e.what()));
    }
    catch (const toml::type_error& e) {
        throw ConfigurationError("TOML type error: " + std::string(e.what()));
    }
    catch (const std::exception& e) {
        throw ConfigurationError("Configuration parsing error: " + std::string(e.what()));
    }
}

DaemonConfig ConfigManager::get_default_config() {
    return DaemonConfig{};  // Uses default member initializers
}

void ConfigManager::validate_config(const DaemonConfig& config) {
    std::ostringstream errors;
    
    // Validate daemon section
    if (config.daemon.sampling_interval.count() < 1 || config.daemon.sampling_interval.count() > 3600) {
        errors << "Sampling interval must be between 1 and 3600 seconds. ";
    }
    
    if (config.daemon.data_retention.count() < 24 || config.daemon.data_retention.count() > 24 * 365) {
        errors << "Data retention must be between 1 day and 1 year. ";
    }
    
    if (!is_valid_log_level(config.daemon.log_level)) {
        errors << "Invalid log level: " << config.daemon.log_level << ". ";
    }
    
    // Validate sensor section
    if (!is_valid_path(config.sensor.i2c_device, true)) {
        errors << "I2C device path does not exist or is not accessible: " << config.sensor.i2c_device << ". ";
    }
    
    if (!is_valid_i2c_address(config.sensor.i2c_address)) {
        errors << "Invalid I2C address: 0x" << std::hex << static_cast<int>(config.sensor.i2c_address) << ". ";
    }
    
    if (config.sensor.connection_timeout.count() < 100 || config.sensor.connection_timeout.count() > 10000) {
        errors << "Connection timeout must be between 100ms and 10000ms. ";
    }
    
    if (config.sensor.max_retries < 0 || config.sensor.max_retries > 10) {
        errors << "Max retries must be between 0 and 10. ";
    }
    
    // Validate storage section
    if (config.storage.data_directory.empty()) {
        errors << "Data directory cannot be empty. ";
    }
    
    if (config.storage.file_rotation.count() < 1 || config.storage.file_rotation.count() > 24 * 7) {
        errors << "File rotation must be between 1 hour and 1 week. ";
    }
    
    if (config.storage.max_memory_cache_mb < 1 || config.storage.max_memory_cache_mb > 100) {
        errors << "Memory cache must be between 1MB and 100MB. ";
    }
    
    std::string error_string = errors.str();
    if (!error_string.empty()) {
        throw ConfigurationError("Configuration validation failed: " + error_string);
    }
}

void ConfigManager::parse_daemon_section(const toml::value& toml_data, DaemonConfig& config) {
    if (toml_data.contains("daemon")) {
        const auto& daemon_section = toml::find(toml_data, "daemon");
        
        if (daemon_section.contains("sampling_interval_seconds")) {
            int interval = toml::find<int>(daemon_section, "sampling_interval_seconds");
            config.daemon.sampling_interval = std::chrono::seconds(interval);
        }
        
        if (daemon_section.contains("data_retention_days")) {
            int retention = toml::find<int>(daemon_section, "data_retention_days");
            config.daemon.data_retention = std::chrono::hours(retention * 24);
        }
        
        if (daemon_section.contains("log_level")) {
            config.daemon.log_level = toml::find<std::string>(daemon_section, "log_level");
            // Convert to lowercase for consistency
            std::transform(config.daemon.log_level.begin(), config.daemon.log_level.end(),
                         config.daemon.log_level.begin(), ::tolower);
        }
    }
}

void ConfigManager::parse_sensor_section(const toml::value& toml_data, DaemonConfig& config) {
    if (toml_data.contains("sensor")) {
        const auto& sensor_section = toml::find(toml_data, "sensor");
        
        if (sensor_section.contains("i2c_device")) {
            config.sensor.i2c_device = toml::find<std::string>(sensor_section, "i2c_device");
        }
        
        if (sensor_section.contains("i2c_address")) {
            int address = toml::find<int>(sensor_section, "i2c_address");
            if (address >= 0 && address <= 255) {
                config.sensor.i2c_address = static_cast<uint8_t>(address);
            } else {
                throw ConfigurationError("I2C address must be between 0 and 255");
            }
        }
        
        if (sensor_section.contains("connection_timeout_ms")) {
            int timeout = toml::find<int>(sensor_section, "connection_timeout_ms");
            config.sensor.connection_timeout = std::chrono::milliseconds(timeout);
        }
        
        if (sensor_section.contains("max_retries")) {
            config.sensor.max_retries = toml::find<int>(sensor_section, "max_retries");
        }
    }
}

void ConfigManager::parse_storage_section(const toml::value& toml_data, DaemonConfig& config) {
    if (toml_data.contains("storage")) {
        const auto& storage_section = toml::find(toml_data, "storage");
        
        if (storage_section.contains("data_directory")) {
            config.storage.data_directory = toml::find<std::string>(storage_section, "data_directory");
        }
        
        if (storage_section.contains("file_rotation_hours")) {
            int rotation = toml::find<int>(storage_section, "file_rotation_hours");
            config.storage.file_rotation = std::chrono::hours(rotation);
        }
        
        if (storage_section.contains("compression_enabled")) {
            config.storage.compression_enabled = toml::find<bool>(storage_section, "compression_enabled");
        }
        
        if (storage_section.contains("max_memory_cache_mb")) {
            int cache_mb = toml::find<int>(storage_section, "max_memory_cache_mb");
            config.storage.max_memory_cache_mb = static_cast<size_t>(cache_mb);
        }
    }
}

bool ConfigManager::is_valid_log_level(const std::string& level) {
    static const std::vector<std::string> valid_levels = {
        "trace", "debug", "info", "warn", "error", "critical", "off"
    };
    
    return std::find(valid_levels.begin(), valid_levels.end(), level) != valid_levels.end();
}

bool ConfigManager::is_valid_i2c_address(uint8_t address) {
    // I2C addresses 0x00-0x07 and 0x78-0x7F are reserved
    return address >= 0x08 && address <= 0x77;
}

bool ConfigManager::is_valid_path(const std::string& path, bool must_exist) {
    if (path.empty()) {
        return false;
    }
    
    if (must_exist) {
        return std::filesystem::exists(path);
    }
    
    // For paths that don't need to exist, check if parent directory exists
    // or if it's an absolute path that could be created
    std::filesystem::path fs_path(path);
    if (fs_path.is_absolute()) {
        return true;  // Assume absolute paths can be created
    }
    
    // For relative paths, check if parent exists
    auto parent = fs_path.parent_path();
    return parent.empty() || std::filesystem::exists(parent);
}

} // namespace sensor_daemon