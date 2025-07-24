#include "diagnostic_tools.hpp"
#include "config_manager.hpp"
#include "logging_system.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <algorithm>
#include <random>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

namespace sensor_daemon {

// Global variables for health endpoint update thread
namespace {
    std::atomic<bool> health_endpoint_running{false};
    std::thread health_endpoint_thread;
    std::mutex health_endpoint_mutex;
    std::condition_variable health_endpoint_cv;
    HealthEndpointConfig health_endpoint_config;
    HealthMonitor* health_endpoint_monitor = nullptr;
    
    void health_endpoint_update_loop() {
        auto last_update = std::chrono::steady_clock::now();
        
        while (health_endpoint_running) {
            std::unique_lock<std::mutex> lock(health_endpoint_mutex);
            
            auto now = std::chrono::steady_clock::now();
            auto next_update = last_update + health_endpoint_config.update_interval;
            
            if (health_endpoint_cv.wait_until(lock, next_update, []{ return !health_endpoint_running; })) {
                // Exit signal received
                break;
            }
            
            // Update health endpoint
            if (health_endpoint_monitor) {
                DiagnosticTools::update_health_endpoint(health_endpoint_monitor, health_endpoint_config);
            }
            
            last_update = std::chrono::steady_clock::now();
        }
    }
}

// DiagnosticReport implementation
bool DiagnosticReport::all_tests_passed() const {
    return std::all_of(test_results.begin(), test_results.end(),
                      [](const DiagnosticResult& result) { return result.passed; });
}

std::string DiagnosticReport::get_formatted_report() const {
    std::ostringstream report;
    
    report << "=== SENSOR DAEMON DIAGNOSTIC REPORT ===\n";
    report << "Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
        timestamp.time_since_epoch()).count() << "\n";
    report << "Overall Status: " << (all_tests_passed() ? "PASS" : "FAIL") << "\n\n";
    
    report << "=== TEST RESULTS ===\n";
    for (const auto& result : test_results) {
        report << "[" << (result.passed ? "PASS" : "FAIL") << "] " 
               << result.test_name << " (" << result.duration.count() << "ms)\n";
        if (!result.message.empty()) {
            report << "  Message: " << result.message << "\n";
        }
        for (const auto& detail : result.details) {
            report << "  - " << detail << "\n";
        }
        report << "\n";
    }
    
    if (!summary.empty()) {
        report << "=== SUMMARY ===\n";
        report << summary << "\n\n";
    }
    
    return report.str();
}

std::string DiagnosticReport::get_json_report() const {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        timestamp.time_since_epoch()).count() << ",\n";
    json << "  \"overall_status\": \"" << (all_tests_passed() ? "PASS" : "FAIL") << "\",\n";
    json << "  \"test_results\": [\n";
    
    for (size_t i = 0; i < test_results.size(); ++i) {
        const auto& result = test_results[i];
        json << "    {\n";
        json << "      \"test_name\": \"" << result.test_name << "\",\n";
        json << "      \"passed\": " << (result.passed ? "true" : "false") << ",\n";
        json << "      \"message\": \"" << result.message << "\",\n";
        json << "      \"duration_ms\": " << result.duration.count() << ",\n";
        json << "      \"details\": [";
        for (size_t j = 0; j < result.details.size(); ++j) {
            json << "\"" << result.details[j] << "\"";
            if (j < result.details.size() - 1) json << ", ";
        }
        json << "]\n";
        json << "    }";
        if (i < test_results.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ],\n";
    json << "  \"summary\": \"" << summary << "\"\n";
    json << "}\n";
    
    return json.str();
}

// DiagnosticTools implementation
DiagnosticReport DiagnosticTools::run_comprehensive_diagnostics(
    const SCD40Interface* sensor_interface,
    const TimeSeriesStorage* storage,
    const HealthMonitor* health_monitor) {
    
    DiagnosticReport report;
    report.timestamp = std::chrono::system_clock::now();
    
    std::cout << "Running comprehensive system diagnostics...\n" << std::endl;
    
    // Test system resources
    std::cout << "Testing system resources..." << std::endl;
    report.test_results.push_back(test_system_resources());
    
    // Test system dependencies
    std::cout << "Testing system dependencies..." << std::endl;
    report.test_results.push_back(test_system_dependencies());
    
    // Test configuration
    std::cout << "Testing configuration..." << std::endl;
    report.test_results.push_back(test_configuration());
    
    // Test file permissions
    std::cout << "Testing file permissions..." << std::endl;
    report.test_results.push_back(test_file_permissions());
    
    // Test logging system
    std::cout << "Testing logging system..." << std::endl;
    report.test_results.push_back(test_logging_system());
    
    // Test daemon status
    std::cout << "Testing daemon status..." << std::endl;
    report.test_results.push_back(test_daemon_status());
    
    // Test I2C communication if sensor interface available
    if (sensor_interface) {
        std::cout << "Testing I2C communication..." << std::endl;
        report.test_results.push_back(test_i2c_communication(sensor_interface));
    }
    
    // Test storage functionality if storage available
    if (storage) {
        std::cout << "Testing storage functionality..." << std::endl;
        report.test_results.push_back(test_storage_functionality(storage));
    }
    
    // Get health status if health monitor available
    if (health_monitor) {
        report.health_status = health_monitor->get_last_health_status();
    }
    
    // Generate summary
    int passed_tests = std::count_if(report.test_results.begin(), report.test_results.end(),
                                   [](const DiagnosticResult& r) { return r.passed; });
    int total_tests = report.test_results.size();
    
    std::ostringstream summary;
    summary << "Diagnostic completed: " << passed_tests << "/" << total_tests << " tests passed";
    if (passed_tests < total_tests) {
        summary << ". System may have issues that require attention.";
    } else {
        summary << ". System appears to be functioning normally.";
    }
    report.summary = summary.str();
    
    return report;
}

DiagnosticResult DiagnosticTools::test_i2c_communication(const SCD40Interface* sensor_interface) {
    DiagnosticResult result("I2C Communication");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!sensor_interface) {
            result.message = "Sensor interface not available";
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        bool is_connected = sensor_interface->is_connected();
        auto stats = sensor_interface->get_stats();
        std::string last_error = sensor_interface->get_last_error();
        
        result.add_detail("Connected: " + std::string(is_connected ? "Yes" : "No"));
        result.add_detail("Successful reads: " + std::to_string(stats.successful_reads));
        result.add_detail("Failed reads: " + std::to_string(stats.failed_reads));
        result.add_detail("Reconnection attempts: " + std::to_string(stats.reconnection_attempts));
        
        if (!last_error.empty()) {
            result.add_detail("Last error: " + last_error);
        }
        
        // Check I2C device file
        std::string i2c_device = "/dev/i2c-1"; // Default, should be configurable
        if (access(i2c_device.c_str(), R_OK | W_OK) == 0) {
            result.add_detail("I2C device accessible: " + i2c_device);
        } else {
            result.add_detail("I2C device not accessible: " + i2c_device);
        }
        
        if (is_connected && stats.successful_reads > 0) {
            result.passed = true;
            result.message = "I2C communication working";
        } else if (is_connected) {
            result.passed = false;
            result.message = "Sensor connected but no successful reads";
        } else {
            result.passed = false;
            result.message = "Sensor not connected";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during I2C test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_storage_functionality(const TimeSeriesStorage* storage) {
    DiagnosticResult result("Storage Functionality");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!storage) {
            result.message = "Storage engine not available";
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        bool is_healthy = storage->is_healthy();
        uint64_t db_size = storage->get_database_size();
        
        result.add_detail("Storage healthy: " + std::string(is_healthy ? "Yes" : "No"));
        result.add_detail("Database size: " + std::to_string(db_size / 1024.0 / 1024.0) + " MB");
        
        if (is_healthy) {
            result.passed = true;
            result.message = "Storage engine functioning normally";
        } else {
            result.passed = false;
            result.message = "Storage engine reports unhealthy status";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during storage test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_system_resources(const std::string& data_directory) {
    DiagnosticResult result("System Resources");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Check memory usage
        std::ifstream status_file("/proc/self/status");
        std::string line;
        double memory_mb = 0.0;
        
        while (std::getline(status_file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                memory_mb = std::stod(value) / 1024.0;
                break;
            }
        }
        
        result.add_detail("Memory usage: " + std::to_string(memory_mb) + " MB");
        
        // Check disk space
        struct statvfs stat;
        if (statvfs(data_directory.c_str(), &stat) == 0) {
            uint64_t available_bytes = stat.f_bavail * stat.f_frsize;
            double available_mb = available_bytes / 1024.0 / 1024.0;
            result.add_detail("Available disk space: " + std::to_string(available_mb) + " MB");
            
            if (available_mb < 100.0) {
                result.passed = false;
                result.message = "Low disk space";
            }
        } else {
            result.add_detail("Could not check disk space for: " + data_directory);
        }
        
        // Check if memory usage is reasonable
        if (memory_mb > 20.0) {
            result.passed = false;
            result.message = "High memory usage";
        } else {
            result.passed = true;
            result.message = "System resources normal";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during resource test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_file_permissions(const std::string& data_directory) {
    DiagnosticResult result("File Permissions");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        bool all_good = true;
        
        // Check data directory
        if (check_directory_writable(data_directory)) {
            result.add_detail("Data directory writable: " + data_directory);
        } else {
            result.add_detail("Data directory not writable: " + data_directory);
            all_good = false;
        }
        
        // Check log directory
        std::string log_dir = "/var/log/sensor-daemon";
        if (std::filesystem::exists(log_dir)) {
            if (check_directory_writable(log_dir)) {
                result.add_detail("Log directory writable: " + log_dir);
            } else {
                result.add_detail("Log directory not writable: " + log_dir);
                all_good = false;
            }
        }
        
        // Check run directory
        std::string run_dir = "/var/run/sensor-daemon";
        if (std::filesystem::exists(run_dir)) {
            if (check_directory_writable(run_dir)) {
                result.add_detail("Run directory writable: " + run_dir);
            } else {
                result.add_detail("Run directory not writable: " + run_dir);
                all_good = false;
            }
        }
        
        // Check config file
        std::string config_file = "/etc/sensor-daemon/config.toml";
        if (check_file_readable(config_file)) {
            result.add_detail("Config file readable: " + config_file);
        } else {
            result.add_detail("Config file not readable: " + config_file);
            // This might be OK if using default config
        }
        
        result.passed = all_good;
        result.message = all_good ? "File permissions OK" : "Some permission issues found";
        
    } catch (const std::exception& e) {
        result.message = "Exception during permission test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_configuration(const std::string& config_path) {
    DiagnosticResult result("Configuration");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (check_file_readable(config_path)) {
            result.add_detail("Config file exists: " + config_path);
            
            // Try to load configuration
            try {
                DaemonConfig config = ConfigManager::load_config(config_path);
                result.add_detail("Configuration loaded successfully");
                result.add_detail("Sampling interval: " + std::to_string(config.daemon.sampling_interval.count()) + "s");
                result.add_detail("Data retention: " + std::to_string(config.daemon.data_retention.count()) + "h");
                result.add_detail("I2C device: " + config.sensor.i2c_device);
                result.add_detail("I2C address: 0x" + std::to_string(config.sensor.i2c_address));
                result.add_detail("Data directory: " + config.storage.data_directory);
                
                result.passed = true;
                result.message = "Configuration valid";
                
            } catch (const std::exception& e) {
                result.message = "Configuration parsing failed: " + std::string(e.what());
                result.passed = false;
            }
        } else {
            result.add_detail("Config file not found, will use defaults");
            result.passed = true;
            result.message = "Using default configuration";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during configuration test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_logging_system() {
    DiagnosticResult result("Logging System");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        bool is_initialized = LoggingSystem::is_initialized();
        result.add_detail("Logging initialized: " + std::string(is_initialized ? "Yes" : "No"));
        
        if (is_initialized) {
            LogLevel current_level = LoggingSystem::get_log_level();
            result.add_detail("Log level: " + LoggingSystem::log_level_to_string(current_level));
            result.passed = true;
            result.message = "Logging system functional";
        } else {
            result.passed = false;
            result.message = "Logging system not initialized";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during logging test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_system_dependencies() {
    DiagnosticResult result("System Dependencies");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        bool all_good = true;
        
        // Check for I2C support
        if (std::filesystem::exists("/dev/i2c-1")) {
            result.add_detail("I2C device available: /dev/i2c-1");
        } else {
            result.add_detail("I2C device not found: /dev/i2c-1");
            all_good = false;
        }
        
        // Check for systemd
        if (std::filesystem::exists("/run/systemd/system")) {
            result.add_detail("systemd available");
        } else {
            result.add_detail("systemd not detected");
        }
        
        // Check kernel modules
        std::ifstream modules("/proc/modules");
        std::string line;
        bool i2c_dev_loaded = false;
        
        while (std::getline(modules, line)) {
            if (line.find("i2c_dev") != std::string::npos) {
                i2c_dev_loaded = true;
                break;
            }
        }
        
        if (i2c_dev_loaded) {
            result.add_detail("i2c-dev kernel module loaded");
        } else {
            result.add_detail("i2c-dev kernel module not loaded");
            all_good = false;
        }
        
        result.passed = all_good;
        result.message = all_good ? "All dependencies available" : "Some dependencies missing";
        
    } catch (const std::exception& e) {
        result.message = "Exception during dependency test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_daemon_status() {
    DiagnosticResult result("Daemon Status");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        bool daemon_running = is_process_running("sensor-daemon");
        result.add_detail("Daemon process running: " + std::string(daemon_running ? "Yes" : "No"));
        
        // Check systemd service status
        int systemd_status = system("systemctl is-active --quiet sensor-daemon");
        bool service_active = (systemd_status == 0);
        result.add_detail("systemd service active: " + std::string(service_active ? "Yes" : "No"));
        
        // Check if status file exists
        std::string status_file = "/var/run/sensor-daemon/health.json";
        if (std::filesystem::exists(status_file)) {
            result.add_detail("Health status file exists: " + status_file);
            
            // Try to read and parse status file
            try {
                std::ifstream file(status_file);
                std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                if (!content.empty()) {
                    result.add_detail("Status file readable and contains data");
                    
                    // Check if it contains expected JSON structure
                    if (content.find("overall_status") != std::string::npos) {
                        result.add_detail("Status file contains valid health data");
                    }
                }
            } catch (const std::exception& e) {
                result.add_detail("Status file read error: " + std::string(e.what()));
            }
        } else {
            result.add_detail("Health status file not found: " + status_file);
        }
        
        // Check if HTTP health endpoint is responding
        if (daemon_running) {
            int http_status = system("curl -s -o /dev/null -w '%{http_code}' http://localhost:8080/health 2>/dev/null | grep -q 200");
            if (http_status == 0) {
                result.add_detail("HTTP health endpoint responding");
            } else {
                result.add_detail("HTTP health endpoint not responding");
            }
        }
        
        result.passed = daemon_running || service_active;
        result.message = result.passed ? "Daemon appears to be running" : "Daemon not running";
        
    } catch (const std::exception& e) {
        result.message = "Exception during daemon status test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_i2c_bus(const std::string& i2c_device) {
    DiagnosticResult result("I2C Bus Scan");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Check if I2C device exists
        if (!std::filesystem::exists(i2c_device)) {
            result.message = "I2C device not found: " + i2c_device;
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        result.add_detail("I2C device found: " + i2c_device);
        
        // Try to open I2C device
        int fd = open(i2c_device.c_str(), O_RDWR);
        if (fd < 0) {
            result.message = "Cannot open I2C device: " + std::string(strerror(errno));
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        result.add_detail("I2C device opened successfully");
        
        // Scan for devices on the bus
        std::vector<uint8_t> detected_devices;
        for (uint8_t addr = 0x03; addr < 0x78; ++addr) {
            if (ioctl(fd, I2C_SLAVE, addr) >= 0) {
                // Try to read from the device
                char buf;
                if (read(fd, &buf, 1) >= 0 || errno == EREMOTEIO) {
                    detected_devices.push_back(addr);
                }
            }
        }
        
        close(fd);
        
        if (!detected_devices.empty()) {
            std::ostringstream devices_str;
            devices_str << "Detected devices at addresses: ";
            for (size_t i = 0; i < detected_devices.size(); ++i) {
                if (i > 0) devices_str << ", ";
                devices_str << "0x" << std::hex << static_cast<int>(detected_devices[i]);
            }
            result.add_detail(devices_str.str());
            
            // Check if SCD40 default address (0x62) is present
            bool scd40_found = std::find(detected_devices.begin(), detected_devices.end(), 0x62) != detected_devices.end();
            if (scd40_found) {
                result.add_detail("SCD40 sensor detected at address 0x62");
                result.passed = true;
                result.message = "I2C bus functional, SCD40 sensor detected";
            } else {
                result.add_detail("SCD40 sensor not found at expected address 0x62");
                result.passed = false;
                result.message = "I2C bus functional but SCD40 sensor not detected";
            }
        } else {
            result.add_detail("No I2C devices detected on bus");
            result.passed = false;
            result.message = "I2C bus appears empty or non-functional";
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during I2C bus test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_sensor_data_quality(const SCD40Interface* sensor_interface, int num_samples) {
    DiagnosticResult result("Sensor Data Quality");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!sensor_interface) {
            result.message = "Sensor interface not available";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        if (!sensor_interface->is_connected()) {
            result.message = "Sensor not connected";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        std::vector<SensorReading> readings;
        int successful_readings = 0;
        
        // Collect samples
        for (int i = 0; i < num_samples; ++i) {
            try {
                SensorReading reading = sensor_interface->read_sensor();
                readings.push_back(reading);
                
                if (reading.co2_ppm.has_value() || reading.temperature_c.has_value() || reading.humidity_percent.has_value()) {
                    successful_readings++;
                }
                
                // Small delay between readings
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                result.add_detail("Reading " + std::to_string(i + 1) + " failed: " + e.what());
            }
        }
        
        result.add_detail("Successful readings: " + std::to_string(successful_readings) + "/" + std::to_string(num_samples));
        
        if (successful_readings == 0) {
            result.message = "No successful sensor readings obtained";
            result.passed = false;
        } else {
            // Analyze data quality
            std::vector<float> co2_values, temp_values, humidity_values;
            
            for (const auto& reading : readings) {
                if (reading.co2_ppm.has_value()) {
                    co2_values.push_back(reading.co2_ppm.value());
                }
                if (reading.temperature_c.has_value()) {
                    temp_values.push_back(reading.temperature_c.value());
                }
                if (reading.humidity_percent.has_value()) {
                    humidity_values.push_back(reading.humidity_percent.value());
                }
            }
            
            // Check CO2 values
            if (!co2_values.empty()) {
                auto [min_co2, max_co2] = std::minmax_element(co2_values.begin(), co2_values.end());
                float avg_co2 = std::accumulate(co2_values.begin(), co2_values.end(), 0.0f) / co2_values.size();
                
                result.add_detail("CO2 range: " + std::to_string(*min_co2) + " - " + std::to_string(*max_co2) + " ppm");
                result.add_detail("CO2 average: " + std::to_string(avg_co2) + " ppm");
                
                // Check for reasonable CO2 values (outdoor: ~400ppm, indoor: 400-1000ppm)
                if (*min_co2 < 300 || *max_co2 > 5000) {
                    result.add_detail("Warning: CO2 values outside expected range (300-5000 ppm)");
                }
            }
            
            // Check temperature values
            if (!temp_values.empty()) {
                auto [min_temp, max_temp] = std::minmax_element(temp_values.begin(), temp_values.end());
                float avg_temp = std::accumulate(temp_values.begin(), temp_values.end(), 0.0f) / temp_values.size();
                
                result.add_detail("Temperature range: " + std::to_string(*min_temp) + " - " + std::to_string(*max_temp) + " °C");
                result.add_detail("Temperature average: " + std::to_string(avg_temp) + " °C");
                
                // Check for reasonable temperature values
                if (*min_temp < -40 || *max_temp > 70) {
                    result.add_detail("Warning: Temperature values outside sensor range (-40 to 70°C)");
                }
            }
            
            // Check humidity values
            if (!humidity_values.empty()) {
                auto [min_hum, max_hum] = std::minmax_element(humidity_values.begin(), humidity_values.end());
                float avg_hum = std::accumulate(humidity_values.begin(), humidity_values.end(), 0.0f) / humidity_values.size();
                
                result.add_detail("Humidity range: " + std::to_string(*min_hum) + " - " + std::to_string(*max_hum) + " %");
                result.add_detail("Humidity average: " + std::to_string(avg_hum) + " %");
                
                // Check for reasonable humidity values
                if (*min_hum < 0 || *max_hum > 100) {
                    result.add_detail("Warning: Humidity values outside valid range (0-100%)");
                }
            }
            
            double success_rate = static_cast<double>(successful_readings) / num_samples;
            if (success_rate >= 0.8) {
                result.passed = true;
                result.message = "Sensor data quality good (" + std::to_string(static_cast<int>(success_rate * 100)) + "% success rate)";
            } else {
                result.passed = false;
                result.message = "Poor sensor data quality (" + std::to_string(static_cast<int>(success_rate * 100)) + "% success rate)";
            }
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during sensor data quality test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_storage_query_performance(const TimeSeriesStorage* storage) {
    DiagnosticResult result("Storage Query Performance");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!storage) {
            result.message = "Storage engine not available";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        if (!storage->is_healthy()) {
            result.message = "Storage engine reports unhealthy status";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        // Test basic storage metrics
        uint64_t db_size = storage->get_database_size();
        result.add_detail("Database size: " + std::to_string(db_size / 1024.0 / 1024.0) + " MB");
        
        // Performance requirements from design document:
        // - Query response time < 10ms for recent data
        // - Memory usage < 10MB
        
        if (db_size > 100 * 1024 * 1024) { // 100MB
            result.add_detail("Large database size may affect query performance");
        }
        
        result.passed = true;
        result.message = "Storage engine appears healthy and ready for queries";
        
    } catch (const std::exception& e) {
        result.message = "Exception during storage performance test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

std::vector<std::string> DiagnosticTools::generate_troubleshooting_recommendations(const DiagnosticReport& report) {
    std::vector<std::string> recommendations;
    
    for (const auto& result : report.test_results) {
        if (!result.passed) {
            if (result.test_name == "I2C Communication") {
                recommendations.push_back("Check I2C wiring and sensor connection");
                recommendations.push_back("Verify I2C is enabled: sudo raspi-config (if on Raspberry Pi)");
                recommendations.push_back("Check I2C device permissions: ls -l /dev/i2c-*");
                recommendations.push_back("Test I2C detection: sudo i2cdetect -y 1");
            } else if (result.test_name == "System Resources") {
                recommendations.push_back("Check available disk space: df -h");
                recommendations.push_back("Monitor memory usage: free -h");
                recommendations.push_back("Consider cleaning up old log files");
            } else if (result.test_name == "File Permissions") {
                recommendations.push_back("Check directory ownership: ls -ld /var/lib/sensor-daemon");
                recommendations.push_back("Fix permissions: sudo chown -R sensor-daemon:sensor-daemon /var/lib/sensor-daemon");
                recommendations.push_back("Ensure daemon user exists: id sensor-daemon");
            } else if (result.test_name == "Configuration") {
                recommendations.push_back("Check configuration file syntax");
                recommendations.push_back("Verify configuration file permissions");
                recommendations.push_back("Review configuration values for validity");
            } else if (result.test_name == "System Dependencies") {
                recommendations.push_back("Enable I2C: sudo raspi-config -> Interface Options -> I2C");
                recommendations.push_back("Load I2C module: sudo modprobe i2c-dev");
                recommendations.push_back("Add to boot modules: echo 'i2c-dev' | sudo tee -a /etc/modules");
            } else if (result.test_name == "Storage Functionality") {
                recommendations.push_back("Check database directory permissions");
                recommendations.push_back("Verify sufficient disk space");
                recommendations.push_back("Consider database recovery if corrupted");
            }
        }
    }
    
    if (recommendations.empty()) {
        recommendations.push_back("All diagnostic tests passed - system appears healthy");
    }
    
    return recommendations;
}

// Helper functions
bool DiagnosticTools::check_file_readable(const std::string& file_path) {
    return access(file_path.c_str(), R_OK) == 0;
}

bool DiagnosticTools::check_directory_writable(const std::string& dir_path) {
    return access(dir_path.c_str(), W_OK) == 0;
}

bool DiagnosticTools::is_process_running(const std::string& process_name) {
    std::string command = "pgrep -x " + process_name + " > /dev/null 2>&1";
    return system(command.c_str()) == 0;
}

// DiagnosticCLI implementation
int DiagnosticCLI::run(int argc, char* argv[]) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage();
        return 0;
    }
    
    std::cout << "Sensor Daemon Diagnostic Tool\n";
    std::cout << "==============================\n\n";
    
    DiagnosticReport report = DiagnosticTools::run_comprehensive_diagnostics();
    
    std::cout << "\n" << report.get_formatted_report() << std::endl;
    
    // Generate recommendations
    auto recommendations = DiagnosticTools::generate_troubleshooting_recommendations(report);
    if (!recommendations.empty()) {
        std::cout << "=== TROUBLESHOOTING RECOMMENDATIONS ===\n";
        for (const auto& rec : recommendations) {
            std::cout << "- " << rec << "\n";
        }
        std::cout << std::endl;
    }
    
    return report.all_tests_passed() ? 0 : 1;
}

void DiagnosticCLI::print_usage() {
    std::cout << "Usage: sensor-daemon-diagnostic [options] [test_name] [test_args...]\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help    Show this help message\n";
    std::cout << "\n";
    std::cout << "Available tests:\n";
    std::cout << "  i2c           Test I2C communication\n";
    std::cout << "  storage       Test storage functionality\n";
    std::cout << "  resources     Test system resources\n";
    std::cout << "  permissions   Test file permissions\n";
    std::cout << "  config        Test configuration\n";
    std::cout << "  logging       Test logging system\n";
    std::cout << "  dependencies  Test system dependencies\n";
    std::cout << "  daemon        Test daemon status\n";
    std::cout << "  i2c-bus       Scan I2C bus for devices\n";
    std::cout << "  sensor-quality Test sensor data quality\n";
    std::cout << "  query-perf    Test storage query performance\n";
    std::cout << "\n";
    std::cout << "This tool runs comprehensive diagnostics on the sensor daemon system\n";
    std::cout << "and provides troubleshooting recommendations.\n";
}

bool DiagnosticCLI::parse_arguments(int argc, char* argv[], std::string& test_name, std::vector<std::string>& args) {
    if (argc < 2) {
        return true; // No arguments, run comprehensive diagnostics
    }
    
    std::string arg1 = argv[1];
    if (arg1 == "--help" || arg1 == "-h") {
        print_usage();
        return false;
    }
    
    // Check if first argument is a test name
    auto available_tests = get_available_tests();
    if (std::find(available_tests.begin(), available_tests.end(), arg1) != available_tests.end()) {
        test_name = arg1;
        
        // Collect remaining arguments for the test
        for (int i = 2; i < argc; ++i) {
            args.push_back(argv[i]);
        }
    }
    
    return true;
}

std::vector<std::string> DiagnosticCLI::get_available_tests() {
    return {
        "i2c", "storage", "resources", "permissions", "config", 
        "logging", "dependencies", "daemon", "i2c-bus", 
        "sensor-quality", "query-perf"
    };
}

int DiagnosticCLI::run(int argc, char* argv[]) {
    std::string test_name;
    std::vector<std::string> args;
    
    if (!parse_arguments(argc, argv, test_name, args)) {
        return 0; // Help was displayed
    }
    
    if (!test_name.empty()) {
        return run_specific_test(test_name, args);
    }
    
    std::cout << "Sensor Daemon Diagnostic Tool\n";
    std::cout << "==============================\n\n";
    
    DiagnosticReport report = DiagnosticTools::run_comprehensive_diagnostics();
    
    std::cout << "\n" << report.get_formatted_report() << std::endl;
    
    // Generate recommendations
    auto recommendations = DiagnosticTools::generate_troubleshooting_recommendations(report);
    if (!recommendations.empty()) {
        std::cout << "=== TROUBLESHOOTING RECOMMENDATIONS ===\n";
        for (const auto& rec : recommendations) {
            std::cout << "- " << rec << "\n";
        }
        std::cout << std::endl;
    }
    
    return report.all_tests_passed() ? 0 : 1;
}

int DiagnosticCLI::run_specific_test(const std::string& test_name, const std::vector<std::string>& args) {
    std::cout << "Running test: " << test_name << "\n";
    std::cout << "==============================\n\n";
    
    DiagnosticResult result("Unknown Test");
    
    try {
        if (test_name == "i2c") {
            result = DiagnosticTools::test_i2c_communication(nullptr);
        } else if (test_name == "storage") {
            result = DiagnosticTools::test_storage_functionality(nullptr);
        } else if (test_name == "resources") {
            std::string dir = args.size() > 0 ? args[0] : "/var/lib/sensor-daemon";
            result = DiagnosticTools::test_system_resources(dir);
        } else if (test_name == "permissions") {
            std::string dir = args.size() > 0 ? args[0] : "/var/lib/sensor-daemon";
            result = DiagnosticTools::test_file_permissions(dir);
        } else if (test_name == "config") {
            std::string config_path = args.size() > 0 ? args[0] : "/etc/sensor-daemon/config.toml";
            result = DiagnosticTools::test_configuration(config_path);
        } else if (test_name == "logging") {
            result = DiagnosticTools::test_logging_system();
        } else if (test_name == "dependencies") {
            result = DiagnosticTools::test_system_dependencies();
        } else if (test_name == "daemon") {
            result = DiagnosticTools::test_daemon_status();
        } else if (test_name == "i2c-bus") {
            std::string i2c_device = args.size() > 0 ? args[0] : "/dev/i2c-1";
            result = DiagnosticTools::test_i2c_bus(i2c_device);
        } else if (test_name == "sensor-quality") {
            int num_samples = args.size() > 0 ? std::stoi(args[0]) : 5;
            result = DiagnosticTools::test_sensor_data_quality(nullptr, num_samples);
        } else if (test_name == "query-perf") {
            result = DiagnosticTools::test_storage_query_performance(nullptr);
        } else {
            std::cout << "Unknown test: " << test_name << "\n";
            print_usage();
            return 1;
        }
        
        // Print test result
        std::cout << "[" << (result.passed ? "PASS" : "FAIL") << "] " << result.test_name 
                  << " (" << result.duration.count() << "ms)\n";
        
        if (!result.message.empty()) {
            std::cout << "Message: " << result.message << "\n";
        }
        
        for (const auto& detail : result.details) {
            std::cout << "- " << detail << "\n";
        }
        
        return result.passed ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cout << "Error running test: " << e.what() << "\n";
        return 1;
    }
}

// New diagnostic tool methods

DiagnosticResult DiagnosticTools::test_i2c_bus(const std::string& i2c_device) {
    DiagnosticResult result("I2C Bus Scan");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        int file = open(i2c_device.c_str(), O_RDWR);
        if (file < 0) {
            result.message = "Failed to open I2C device: " + std::string(strerror(errno));
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        result.add_detail("I2C device opened: " + i2c_device);
        
        // Scan for devices
        std::vector<int> detected_addresses;
        
        for (int addr = 0x03; addr < 0x78; ++addr) {
            // Skip reserved addresses
            if (addr < 0x08 || (addr > 0x37 && addr < 0x40)) {
                continue;
            }
            
            if (ioctl(file, I2C_SLAVE, addr) < 0) {
                continue;
            }
            
            // Try to read a byte from the device
            char buf;
            if (read(file, &buf, 1) >= 0) {
                detected_addresses.push_back(addr);
            }
        }
        
        close(file);
        
        if (detected_addresses.empty()) {
            result.message = "No I2C devices detected";
            result.passed = false;
        } else {
            result.message = "Detected " + std::to_string(detected_addresses.size()) + " I2C devices";
            result.passed = true;
            
            for (int addr : detected_addresses) {
                std::stringstream ss;
                ss << "Device at address: 0x" << std::hex << addr;
                
                // Add known device names
                if (addr == 0x62) {
                    ss << " (SCD40 CO2 sensor)";
                }
                
                result.add_detail(ss.str());
            }
        }
        
    } catch (const std::exception& e) {
        result.message = "Exception during I2C bus scan: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_sensor_data_quality(
    const SCD40Interface* sensor_interface, 
    int num_samples) {
    
    DiagnosticResult result("Sensor Data Quality");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!sensor_interface) {
            result.message = "Sensor interface not available";
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        if (!sensor_interface->is_connected()) {
            result.message = "Sensor not connected";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        // Collect multiple samples
        std::vector<SensorData> samples;
        int successful_samples = 0;
        
        for (int i = 0; i < num_samples; ++i) {
            try {
                SensorData reading = sensor_interface->read_sensor();
                samples.push_back(reading);
                successful_samples++;
                
                // Add a small delay between readings
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
            } catch (const std::exception& e) {
                result.add_detail("Sample " + std::to_string(i+1) + " failed: " + e.what());
            }
        }
        
        if (successful_samples == 0) {
            result.message = "Failed to collect any valid samples";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        // Calculate statistics
        double co2_sum = 0.0, co2_min = 0.0, co2_max = 0.0;
        double temp_sum = 0.0, temp_min = 0.0, temp_max = 0.0;
        double humidity_sum = 0.0, humidity_min = 0.0, humidity_max = 0.0;
        int co2_count = 0, temp_count = 0, humidity_count = 0;
        
        for (const auto& sample : samples) {
            if (sample.co2_ppm) {
                double value = *sample.co2_ppm;
                if (co2_count == 0 || value < co2_min) co2_min = value;
                if (co2_count == 0 || value > co2_max) co2_max = value;
                co2_sum += value;
                co2_count++;
            }
            
            if (sample.temperature_c) {
                double value = *sample.temperature_c;
                if (temp_count == 0 || value < temp_min) temp_min = value;
                if (temp_count == 0 || value > temp_max) temp_max = value;
                temp_sum += value;
                temp_count++;
            }
            
            if (sample.humidity_percent) {
                double value = *sample.humidity_percent;
                if (humidity_count == 0 || value < humidity_min) humidity_min = value;
                if (humidity_count == 0 || value > humidity_max) humidity_max = value;
                humidity_sum += value;
                humidity_count++;
            }
        }
        
        // Add statistics to result
        result.add_detail("Successful samples: " + std::to_string(successful_samples) + 
                         " of " + std::to_string(num_samples));
        
        if (co2_count > 0) {
            double co2_avg = co2_sum / co2_count;
            double co2_range = co2_max - co2_min;
            result.add_detail("CO2 (ppm): avg=" + std::to_string(co2_avg) + 
                             ", min=" + std::to_string(co2_min) + 
                             ", max=" + std::to_string(co2_max) + 
                             ", range=" + std::to_string(co2_range));
            
            // Check if CO2 readings are reasonable
            if (co2_avg < 400 || co2_avg > 5000) {
                result.add_detail("WARNING: CO2 average outside expected range (400-5000 ppm)");
            }
        } else {
            result.add_detail("No valid CO2 readings");
        }
        
        if (temp_count > 0) {
            double temp_avg = temp_sum / temp_count;
            double temp_range = temp_max - temp_min;
            result.add_detail("Temperature (°C): avg=" + std::to_string(temp_avg) + 
                             ", min=" + std::to_string(temp_min) + 
                             ", max=" + std::to_string(temp_max) + 
                             ", range=" + std::to_string(temp_range));
            
            // Check if temperature readings are reasonable
            if (temp_avg < -10 || temp_avg > 50) {
                result.add_detail("WARNING: Temperature average outside expected range (-10 to 50 °C)");
            }
        } else {
            result.add_detail("No valid temperature readings");
        }
        
        if (humidity_count > 0) {
            double humidity_avg = humidity_sum / humidity_count;
            double humidity_range = humidity_max - humidity_min;
            result.add_detail("Humidity (%): avg=" + std::to_string(humidity_avg) + 
                             ", min=" + std::to_string(humidity_min) + 
                             ", max=" + std::to_string(humidity_max) + 
                             ", range=" + std::to_string(humidity_range));
            
            // Check if humidity readings are reasonable
            if (humidity_avg < 0 || humidity_avg > 100) {
                result.add_detail("WARNING: Humidity average outside expected range (0-100%)");
            }
        } else {
            result.add_detail("No valid humidity readings");
        }
        
        // Determine overall data quality
        bool data_quality_good = true;
        
        // Check for missing values
        if (co2_count < successful_samples || temp_count < successful_samples || 
            humidity_count < successful_samples) {
            result.add_detail("WARNING: Some samples have missing values");
            data_quality_good = false;
        }
        
        // Check for excessive variation
        if (co2_count > 1 && (co2_max - co2_min) > 200) {
            result.add_detail("WARNING: High CO2 variation between samples");
            data_quality_good = false;
        }
        
        if (temp_count > 1 && (temp_max - temp_min) > 2.0) {
            result.add_detail("WARNING: High temperature variation between samples");
            data_quality_good = false;
        }
        
        if (humidity_count > 1 && (humidity_max - humidity_min) > 5.0) {
            result.add_detail("WARNING: High humidity variation between samples");
            data_quality_good = false;
        }
        
        result.passed = successful_samples > 0 && data_quality_good;
        result.message = result.passed ? 
            "Sensor data quality acceptable" : 
            "Sensor data quality issues detected";
        
    } catch (const std::exception& e) {
        result.message = "Exception during sensor quality test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

DiagnosticResult DiagnosticTools::test_storage_query_performance(const TimeSeriesStorage* storage) {
    DiagnosticResult result("Storage Query Performance");
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (!storage) {
            result.message = "Storage engine not available";
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        if (!storage->is_healthy()) {
            result.message = "Storage engine reports unhealthy status";
            result.passed = false;
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);
            return result;
        }
        
        // Test recent readings query performance
        auto recent_start = std::chrono::steady_clock::now();
        auto recent_readings = storage->get_recent_readings(10);
        auto recent_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recent_start);
        
        result.add_detail("Recent readings query (10 items): " + 
                         std::to_string(recent_duration.count()) + " ms");
        
        // Test time range query performance
        auto now = std::chrono::system_clock::now();
        auto one_hour_ago = now - std::chrono::hours(1);
        
        auto range_start = std::chrono::steady_clock::now();
        auto range_readings = storage->get_readings_range(one_hour_ago, now);
        auto range_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - range_start);
        
        result.add_detail("Time range query (last hour): " + 
                         std::to_string(range_duration.count()) + " ms, " +
                         std::to_string(range_readings.size()) + " readings");
        
        // Test aggregation query performance
        auto agg_start = std::chrono::steady_clock::now();
        auto aggregates = storage->get_aggregated_readings(one_hour_ago, now, std::chrono::minutes(5));
        auto agg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - agg_start);
        
        result.add_detail("Aggregation query (5-minute intervals): " + 
                         std::to_string(agg_duration.count()) + " ms, " +
                         std::to_string(aggregates.size()) + " intervals");
        
        // Check performance against requirements
        bool performance_acceptable = true;
        
        if (recent_duration.count() > 10) {
            result.add_detail("WARNING: Recent readings query exceeds 10ms target");
            performance_acceptable = false;
        }
        
        if (range_duration.count() > 100) {
            result.add_detail("WARNING: Time range query exceeds 100ms target");
            performance_acceptable = false;
        }
        
        if (agg_duration.count() > 200) {
            result.add_detail("WARNING: Aggregation query exceeds 200ms target");
            performance_acceptable = false;
        }
        
        result.passed = performance_acceptable;
        result.message = performance_acceptable ? 
            "Storage query performance acceptable" : 
            "Storage query performance issues detected";
        
    } catch (const std::exception& e) {
        result.message = "Exception during storage performance test: " + std::string(e.what());
        result.passed = false;
    }
    
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    
    return result;
}

bool DiagnosticTools::create_health_endpoint(
    HealthMonitor* health_monitor,
    const HealthEndpointConfig& config) {
    
    try {
        // Stop any existing health endpoint thread
        if (health_endpoint_running) {
            {
                std::lock_guard<std::mutex> lock(health_endpoint_mutex);
                health_endpoint_running = false;
            }
            health_endpoint_cv.notify_all();
            
            if (health_endpoint_thread.joinable()) {
                health_endpoint_thread.join();
            }
        }
        
        if (!config.enabled || !health_monitor) {
            return false;
        }
        
        // Create parent directories for status file
        if (!create_parent_directories(config.status_file_path)) {
            LOG_ERROR("Failed to create parent directories for health status file", {
                {"path", config.status_file_path}
            });
            return false;
        }
        
        // Store configuration and monitor
        {
            std::lock_guard<std::mutex> lock(health_endpoint_mutex);
            health_endpoint_config = config;
            health_endpoint_monitor = health_monitor;
            health_endpoint_running = true;
        }
        
        // Create initial status file
        if (!update_health_endpoint(health_monitor, config)) {
            LOG_ERROR("Failed to create initial health status file", {
                {"path", config.status_file_path}
            });
            return false;
        }
        
        // Start update thread
        health_endpoint_thread = std::thread(health_endpoint_update_loop);
        
        LOG_INFO("Health endpoint created", {
            {"status_file", config.status_file_path},
            {"update_interval_seconds", std::to_string(config.update_interval.count())}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception creating health endpoint", {
            {"error", e.what()}
        });
        return false;
    }
}

bool DiagnosticTools::update_health_endpoint(
    const HealthMonitor* health_monitor,
    const HealthEndpointConfig& config) {
    
    try {
        if (!config.enabled || !health_monitor) {
            return false;
        }
        
        // Get health status
        SystemHealthStatus status = health_monitor->get_last_health_status();
        
        // Create JSON status file
        std::ofstream file(config.status_file_path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open health status file for writing", {
                {"path", config.status_file_path}
            });
            return false;
        }
        
        // Write basic health status
        file << "{\n";
        file << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
        file << "  \"status\": \"" << status.get_status_string() << "\",\n";
        file << "  \"operational\": " << (status.is_operational() ? "true" : "false") << ",\n";
        
        // Add detailed metrics if enabled
        if (config.include_detailed_metrics) {
            file << "  \"metrics\": {\n";
            file << "    \"uptime_seconds\": " << status.performance_metrics.get_uptime().count() << ",\n";
            file << "    \"memory_usage_mb\": " << (status.performance_metrics.memory_usage_bytes / 1024.0 / 1024.0) << ",\n";
            file << "    \"cpu_usage_percent\": " << status.performance_metrics.cpu_usage_percent << ",\n";
            file << "    \"sensor_success_rate\": " << status.performance_metrics.get_sensor_success_rate() << ",\n";
            file << "    \"storage_success_rate\": " << status.performance_metrics.get_storage_success_rate() << ",\n";
            file << "    \"i2c_connection_failures\": " << status.performance_metrics.i2c_connection_failures << "\n";
            file << "  }";
            
            // Add component status if enabled
            if (config.include_component_status && !status.component_results.empty()) {
                file << ",\n  \"components\": [\n";
                
                for (size_t i = 0; i < status.component_results.size(); ++i) {
                    const auto& result = status.component_results[i];
                    file << "    {\n";
                    file << "      \"name\": \"" << result.component_name << "\",\n";
                    file << "      \"status\": \"" << HealthMonitor::health_status_to_string(result.status) << "\",\n";
                    file << "      \"message\": \"" << result.message << "\"\n";
                    file << "    }";
                    if (i < status.component_results.size() - 1) {
                        file << ",";
                    }
                    file << "\n";
                }
                
                file << "  ]\n";
            } else {
                file << "\n";
            }
        } else {
            file << "  \"last_check\": " << std::chrono::duration_cast<std::chrono::seconds>(
                status.last_check.time_since_epoch()).count() << "\n";
        }
        
        file << "}\n";
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating health endpoint", {
            {"error", e.what()}
        });
        return false;
    }
}

bool DiagnosticTools::create_parent_directories(const std::string& file_path) {
    try {
        std::filesystem::path path(file_path);
        std::filesystem::path parent_path = path.parent_path();
        
        if (!std::filesystem::exists(parent_path)) {
            return std::filesystem::create_directories(parent_path);
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create parent directories", {
            {"path", file_path},
            {"error", e.what()}
        });
        return false;
    }
}

// HealthMonitorServer implementation
HealthMonitorServer::HealthMonitorServer(HealthMonitor* health_monitor, TimeSeriesStorage* storage)
    : health_monitor_(health_monitor), storage_(storage), running_(false), port_(8080), bind_address_("127.0.0.1") {
    
    // Initialize security manager with default configuration
    SecurityConfig security_config;
    security_config.rate_limit.requests_per_minute = 60;
    security_config.rate_limit.requests_per_hour = 1000;
    security_config.max_query_results = 10000;
    security_config.query_timeout = std::chrono::seconds(30);
    security_config.max_request_size = 8192;
    security_config.enable_request_logging = true;
    
    security_manager_ = std::make_unique<SecurityManager>(security_config);
}

HealthMonitorServer::~HealthMonitorServer() {
    stop();
}

bool HealthMonitorServer::start(int port, const std::string& bind_address) {
    if (running_) {
        return true; // Already running
    }
    
    if (!health_monitor_) {
        LOG_ERROR("Cannot start health monitor server without a health monitor");
        return false;
    }
    
    port_ = port;
    bind_address_ = bind_address;
    running_ = true;
    
    // Start server thread
    server_thread_ = std::thread(&HealthMonitorServer::server_loop, this);
    
    LOG_INFO("Health monitor server started", {
        {"port", std::to_string(port_)},
        {"bind_address", bind_address_}
    });
    
    return true;
}

void HealthMonitorServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    LOG_INFO("Health monitor server stopped");
}

bool HealthMonitorServer::is_running() const {
    return running_;
}

std::string HealthMonitorServer::get_url() const {
    return "http://" + bind_address_ + ":" + std::to_string(port_) + "/health";
}

void HealthMonitorServer::server_loop() {
    // Simple HTTP server implementation
    // This is a minimal implementation for demonstration purposes
    // In a production environment, consider using a proper HTTP server library
    
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("Failed to create socket", {
            {"error", strerror(errno)}
        });
        running_ = false;
        return;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set socket options", {
            {"error", strerror(errno)}
        });
        close(server_fd);
        running_ = false;
        return;
    }
    
    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    
    if (bind_address_ == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr);
    }
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        LOG_ERROR("Failed to bind socket", {
            {"error", strerror(errno)},
            {"port", std::to_string(port_)}
        });
        close(server_fd);
        running_ = false;
        return;
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        LOG_ERROR("Failed to listen on socket", {
            {"error", strerror(errno)}
        });
        close(server_fd);
        running_ = false;
        return;
    }
    
    // Set socket to non-blocking mode
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    LOG_INFO("Health monitor server listening", {
        {"port", std::to_string(port_)},
        {"bind_address", bind_address_}
    });
    
    // Main server loop
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 second timeout for responsive shutdown
        timeout.tv_usec = 0;
        
        int activity = select(server_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            LOG_ERROR("Select error", {
                {"error", strerror(errno)}
            });
            break;
        }
        
        if (activity == 0 || !running_) {
            // Timeout or shutdown requested
            continue;
        }
        
        if (FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_ERROR("Accept error", {
                        {"error", strerror(errno)}
                    });
                }
                continue;
            }
            
            // Handle client request
            char buffer[1024] = {0};
            read(client_fd, buffer, sizeof(buffer) - 1);
            
            // Extract client IP for security validation
            std::string client_ip = extract_client_ip(client_fd);
            
            // Parse HTTP request
            std::string request(buffer);
            
            // Start request timing for logging
            auto request_start_time = std::chrono::steady_clock::now();
            
            // Extract method and path for logging
            auto [method, path] = HttpParameterParser::extract_method_and_path(request);
            
            // Process request with security validation and enhanced routing
            std::string response = process_request_with_security(request, client_ip);
            
            // Calculate response time
            auto request_end_time = std::chrono::steady_clock::now();
            auto response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                request_end_time - request_start_time).count();
            
            // Log request details with response time
            bool is_data_endpoint = path.find("/data/") == 0;
            if (is_data_endpoint) {
                // Enhanced logging for data endpoints
                LOG_INFO("Data endpoint request processed", {
                    {"method", method},
                    {"path", path},
                    {"client_ip", client_ip},
                    {"response_time_ms", std::to_string(response_time_ms)},
                    {"response_size_bytes", std::to_string(response.length())},
                    {"status_code", response.substr(9, 3)} // Extract status code from "HTTP/1.1 XXX"
                });
            } else {
                // Standard logging for health endpoints
                LOG_DEBUG("Health endpoint request processed", {
                    {"method", method},
                    {"path", path},
                    {"client_ip", client_ip},
                    {"response_time_ms", std::to_string(response_time_ms)}
                });
            }
            
            // Send response
            write(client_fd, response.c_str(), response.length());
            close(client_fd);
        }
    }
    
    close(server_fd);
    running_ = false;
}

std::string HealthMonitorServer::handle_health_request() const {
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Connection: close\r\n\r\n";
    
    if (health_monitor_) {
        SystemHealthStatus status = health_monitor_->get_last_health_status();
        
        // Simple health status JSON
        std::ostringstream json;
        json << "{\n";
        json << "  \"status\": \"" << status.get_status_string() << "\",\n";
        json << "  \"operational\": " << (status.is_operational() ? "true" : "false") << ",\n";
        json << "  \"storage_available\": " << (storage_ ? "true" : "false") << ",\n";
        if (storage_) {
            json << "  \"storage_healthy\": " << (storage_->is_healthy() ? "true" : "false") << ",\n";
        }
        json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
        json << "}\n";
        
        response += json.str();
    } else {
        response += "{\"status\":\"UNKNOWN\",\"operational\":false,\"error\":\"Health monitor not available\"}\n";
    }
    
    return response;
}

std::string HealthMonitorServer::handle_metrics_request() const {
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Connection: close\r\n\r\n";
    
    if (health_monitor_) {
        // Return detailed health status JSON
        response += health_monitor_->get_health_status_json();
    } else {
        response += "{\"error\":\"Health monitor not available\"}\n";
    }
    
    return response;
}

std::string HealthMonitorServer::handle_diagnostic_request() const {
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Connection: close\r\n\r\n";
    
    if (health_monitor_) {
        // Run comprehensive diagnostics and return as JSON
        DiagnosticReport report = DiagnosticTools::run_comprehensive_diagnostics();
        response += report.get_json_report();
    } else {
        response += "{\"error\":\"Health monitor not available\"}\n";
    }
    
    return response;
}

std::string HealthMonitorServer::handle_readiness_request() const {
    if (health_monitor_) {
        SystemHealthStatus status = health_monitor_->get_last_health_status();
        
        if (status.is_operational()) {
            std::string response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Connection: close\r\n\r\n";
            response += "{\"ready\":true,\"status\":\"" + status.get_status_string() + "\"}\n";
            return response;
        } else {
            std::string response = "HTTP/1.1 503 Service Unavailable\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Connection: close\r\n\r\n";
            response += "{\"ready\":false,\"status\":\"" + status.get_status_string() + "\"}\n";
            return response;
        }
    } else {
        std::string response = "HTTP/1.1 503 Service Unavailable\r\n";
        response += "Content-Type: application/json\r\n";
        response += "Connection: close\r\n\r\n";
        response += "{\"ready\":false,\"error\":\"Health monitor not available\"}\n";
        return response;
    }
}

std::string HealthMonitorServer::handle_liveness_request() const {
    // Liveness probe - just check if the server is responding
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Connection: close\r\n\r\n";
    
    std::ostringstream json;
    json << "{\n";
    json << "  \"alive\": true,\n";
    json << "  \"timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    json << "  \"uptime_seconds\": ";
    
    if (health_monitor_) {
        SystemHealthStatus status = health_monitor_->get_last_health_status();
        json << status.performance_metrics.get_uptime().count();
    } else {
        json << "0";
    }
    
    json << "\n}\n";
    response += json.str();
    
    return response;
}

std::string HealthMonitorServer::handle_recent_data_request(const std::string& request) const {
    try {
        // Check if storage is available
        if (!storage_) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage not available",
                "Time series storage is not configured or unavailable"
            );
        }
        
        // Check if storage is healthy
        if (!storage_->is_healthy()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage unhealthy",
                "Time series storage reports unhealthy status"
            );
        }
        
        // Parse query parameters
        QueryParameters params = QueryParameters::parse_url_parameters(request);
        
        // Get count parameter (default to 100)
        int count = 100;
        if (params.count.has_value()) {
            if (!params.is_count_valid()) {
                return JsonResponseBuilder::create_error_response(
                    HttpStatus::BAD_REQUEST,
                    "Invalid count parameter",
                    "Count must be a positive integer between 1 and 10000"
                );
            }
            count = params.count.value();
        }
        
        // Query recent readings from storage
        std::vector<SensorData> readings = storage_->get_recent_readings(count);
        
        // Generate JSON response
        return JsonResponseBuilder::create_readings_response(readings);
        
    } catch (const std::exception& e) {
        // Log error and return internal server error
        std::cerr << "Exception in handle_recent_data_request: " << e.what() << std::endl;
        
        return JsonResponseBuilder::create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error",
            "An unexpected error occurred while processing the request"
        );
    }
}

std::string HealthMonitorServer::handle_range_data_request(const std::string& request) const {
    try {
        // Check if storage is available
        if (!storage_) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage not available",
                "Time series storage is not configured or unavailable"
            );
        }
        
        // Check if storage is healthy
        if (!storage_->is_healthy()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage unhealthy",
                "Time series storage reports unhealthy status"
            );
        }
        
        // Parse query parameters
        QueryParameters params = QueryParameters::parse_url_parameters(request);
        
        // Validate required parameters
        if (!params.start_time.has_value() || !params.end_time.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Missing required parameters",
                "Both 'start' and 'end' parameters are required in ISO 8601 format"
            );
        }
        
        // Parse timestamps
        auto start_tp = params.parse_iso8601(params.start_time.value());
        auto end_tp = params.parse_iso8601(params.end_time.value());
        
        if (!start_tp.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid start time",
                "Start time must be in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)"
            );
        }
        
        if (!end_tp.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid end time",
                "End time must be in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)"
            );
        }
        
        // Validate time range
        if (!params.is_time_range_valid()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid time range",
                "Start time must be before end time and range must not exceed 7 days"
            );
        }
        
        // Query readings from storage
        std::vector<SensorData> readings = storage_->get_readings_in_range(
            start_tp.value(), 
            end_tp.value()
        );
        
        // Generate JSON response
        return JsonResponseBuilder::create_range_response(
            readings, 
            params.start_time.value(), 
            params.end_time.value()
        );
        
    } catch (const std::exception& e) {
        // Log error and return internal server error
        std::cerr << "Exception in handle_range_data_request: " << e.what() << std::endl;
        
        return JsonResponseBuilder::create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error",
            "An unexpected error occurred while processing the request"
        );
    }
}

std::string HealthMonitorServer::handle_data_info_request(const std::string& request) const {
    try {
        // Check if storage is available
        if (!storage_) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage not available",
                "Time series storage is not configured or unavailable"
            );
        }
        
        // Get database information from storage
        TimeSeriesStorage::DatabaseInfo info = storage_->get_database_info();
        
        // Generate JSON response
        return JsonResponseBuilder::create_info_response(info);
        
    } catch (const std::exception& e) {
        // Log error and return internal server error
        std::cerr << "Exception in handle_data_info_request: " << e.what() << std::endl;
        
        return JsonResponseBuilder::create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error",
            "An unexpected error occurred while retrieving database information"
        );
    }
}

std::string HealthMonitorServer::handle_aggregates_request(const std::string& request) const {
    try {
        // Check if storage is available
        if (!storage_) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage not available",
                "Time series storage is not configured or unavailable"
            );
        }
        
        // Check if storage is healthy
        if (!storage_->is_healthy()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::SERVICE_UNAVAILABLE,
                "Storage unhealthy",
                "Time series storage reports unhealthy status"
            );
        }
        
        // Parse query parameters
        QueryParameters params = QueryParameters::parse_url_parameters(request);
        
        // Validate required parameters
        if (!params.start_time.has_value() || !params.end_time.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Missing required parameters",
                "Both 'start' and 'end' parameters are required in ISO 8601 format"
            );
        }
        
        // Parse timestamps
        auto start_tp = params.parse_iso8601(params.start_time.value());
        auto end_tp = params.parse_iso8601(params.end_time.value());
        
        if (!start_tp.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid start time",
                "Start time must be in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)"
            );
        }
        
        if (!end_tp.has_value()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid end time",
                "End time must be in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)"
            );
        }
        
        // Validate time range
        if (!params.is_time_range_valid()) {
            return JsonResponseBuilder::create_error_response(
                HttpStatus::BAD_REQUEST,
                "Invalid time range",
                "Start time must be before end time and range must not exceed 7 days"
            );
        }
        
        // Get interval parameter (default to "1H")
        std::string interval = "1H";
        if (params.interval.has_value()) {
            if (!params.is_interval_valid()) {
                return JsonResponseBuilder::create_error_response(
                    HttpStatus::BAD_REQUEST,
                    "Invalid interval parameter",
                    "Interval must be in format like '1H', '30T', '1D'. Supported: " + 
                    [](){ 
                        auto formats = IntervalParser::get_supported_formats();
                        std::string result;
                        for (size_t i = 0; i < formats.size() && i < 5; ++i) {
                            if (i > 0) result += ", ";
                            result += formats[i].substr(0, formats[i].find(' '));
                        }
                        return result;
                    }()
                );
            }
            interval = params.interval.value();
        }
        
        // Query raw readings from storage
        std::vector<SensorData> readings = storage_->get_readings_in_range(
            start_tp.value(), 
            end_tp.value()
        );
        
        // Aggregate the readings
        std::vector<AggregateData> aggregates = DataAggregator::aggregate_by_interval(
            readings, 
            interval
        );
        
        // Generate JSON response
        return JsonResponseBuilder::create_aggregates_response(
            aggregates, 
            params.start_time.value(), 
            params.end_time.value(),
            interval
        );
        
    } catch (const std::exception& e) {
        // Log error and return internal server error
        std::cerr << "Exception in handle_aggregates_request: " << e.what() << std::endl;
        
        return JsonResponseBuilder::create_error_response(
            HttpStatus::INTERNAL_SERVER_ERROR,
            "Internal server error",
            "An unexpected error occurred while processing the aggregation request"
        );
    }
}

std::string HealthMonitorServer::process_request_with_security(const std::string& request, const std::string& client_ip) {
    try {
        // Start performance monitoring
        auto start_time = security_manager_->start_request_monitoring("request_processing");
        
        // Validate request security
        auto validation_result = security_manager_->validate_request(request, client_ip);
        if (!validation_result.is_valid) {
            // Log security violation
            std::cerr << "Security validation failed for IP " << client_ip 
                      << ": " << validation_result.error_message << std::endl;
            
            // Return appropriate error response
            if (validation_result.error_category == HttpErrorCategory::RATE_LIMITING) {
                return HttpErrorHandler::create_rate_limit_error(60);
            } else {
                return HttpErrorHandler::create_parameter_error(
                    "request", "", validation_result.error_details);
            }
        }
        
        // Extract method and path
        auto [method, path] = HttpParameterParser::extract_method_and_path(request);
        
        // Route the request
        std::string response = route_request(request, method, path);
        
        // End performance monitoring
        security_manager_->end_request_monitoring("request_processing", start_time);
        
        return response;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in process_request_with_security: " << e.what() << std::endl;
        return HttpErrorHandler::create_internal_error("request_processing");
    }
}

std::string HealthMonitorServer::route_request(const std::string& request, const std::string& method, const std::string& path) {
    // Only allow GET requests for data endpoints
    if (method != "GET") {
        return HttpErrorHandler::create_method_not_allowed_error(method, {"GET"});
    }
    
    // Route to appropriate handler based on path
    if (path == "/health") {
        return handle_health_request();
    } else if (path == "/metrics") {
        return handle_metrics_request();
    } else if (path == "/diagnostic") {
        return handle_diagnostic_request();
    } else if (path == "/ready") {
        return handle_readiness_request();
    } else if (path == "/alive") {
        return handle_liveness_request();
    } else if (path == "/data/recent") {
        return handle_recent_data_request(request);
    } else if (path == "/data/range") {
        return handle_range_data_request(request);
    } else if (path == "/data/info") {
        return handle_data_info_request(request);
    } else if (path == "/data/aggregates") {
        return handle_aggregates_request(request);
    } else {
        // Enhanced 404 response with better error information
        ErrorDetails details(ErrorCodes::ENDPOINT_NOT_FOUND, "Requested endpoint not found");
        details.with_details("The requested path '" + path + "' is not available")
               .with_suggestion("Check the available endpoints list below")
               .with_context("requested_path", path)
               .with_context("method", method);
        
        // Create enhanced 404 response with available endpoints
        std::ostringstream json;
        json << "{\n";
        json << "  \"error\": \"" << details.user_message << "\",\n";
        json << "  \"error_code\": \"" << details.error_code << "\",\n";
        json << "  \"details\": \"" << details.technical_details << "\",\n";
        json << "  \"suggestion\": \"" << details.suggested_action << "\",\n";
        json << "  \"requested_path\": \"" << path << "\",\n";
        json << "  \"method\": \"" << method << "\",\n";
        json << "  \"available_endpoints\": [\n";
        json << "    {\n";
        json << "      \"path\": \"/health\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Basic health status\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/metrics\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Detailed metrics\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/diagnostic\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Comprehensive diagnostics\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/ready\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Readiness probe\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/alive\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Liveness probe\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/data/recent\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Recent sensor readings\",\n";
        json << "      \"parameters\": \"?count=N (optional, default=100)\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/data/range\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Sensor readings in time range\",\n";
        json << "      \"parameters\": \"?start=TIME&end=TIME (required, ISO 8601 format)\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/data/aggregates\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Aggregated statistics\",\n";
        json << "      \"parameters\": \"?start=TIME&end=TIME&interval=INTERVAL (start/end required, interval optional)\"\n";
        json << "    },\n";
        json << "    {\n";
        json << "      \"path\": \"/data/info\",\n";
        json << "      \"method\": \"GET\",\n";
        json << "      \"description\": \"Database information and statistics\"\n";
        json << "    }\n";
        json << "  ],\n";
        json << "  \"timestamp\": \"" << JsonResponseBuilder::get_current_timestamp() << "\",\n";
        json << "  \"status_code\": 404\n";
        json << "}\n";
        
        std::string json_body = json.str();
        return JsonResponseBuilder::create_http_header(HttpStatus::NOT_FOUND, json_body.length()) + json_body;
    }
}

std::string HealthMonitorServer::extract_client_ip(int client_fd) const {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN)) {
            return std::string(ip_str);
        }
    }
    
    return "unknown";
}

} // namespace sensor_daemon