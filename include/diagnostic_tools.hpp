#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <unordered_map>
#include "health_monitor.hpp"
#include "scd40_interface.hpp"
#include "time_series_storage.hpp"
#include "http_utils.hpp"
#include "json_response_builder.hpp"
#include "data_aggregator.hpp"

namespace sensor_daemon {

/**
 * Diagnostic test result
 */
struct DiagnosticResult {
    std::string test_name;
    bool passed;
    std::string message;
    std::chrono::milliseconds duration;
    std::vector<std::string> details;
    
    DiagnosticResult(const std::string& name) 
        : test_name(name), passed(false), duration(0) {}
    
    DiagnosticResult& add_detail(const std::string& detail) {
        details.push_back(detail);
        return *this;
    }
};

/**
 * Comprehensive diagnostic report
 */
struct DiagnosticReport {
    std::chrono::system_clock::time_point timestamp;
    std::vector<DiagnosticResult> test_results;
    SystemHealthStatus health_status;
    std::string summary;
    
    // Get overall pass/fail status
    bool all_tests_passed() const;
    
    // Get formatted report
    std::string get_formatted_report() const;
    
    // Get JSON report
    std::string get_json_report() const;
};

/**
 * Health check endpoint configuration
 */
struct HealthEndpointConfig {
    bool enabled = true;
    std::string status_file_path = "/var/run/sensor-daemon/health.json";
    std::chrono::seconds update_interval{60};
    bool include_detailed_metrics = true;
    bool include_component_status = true;
};

/**
 * Diagnostic tools for troubleshooting system issues
 */
class DiagnosticTools {
public:
    /**
     * Constructor
     */
    DiagnosticTools() = default;
    
    /**
     * Run comprehensive system diagnostics
     * @param sensor_interface Sensor interface to test (optional)
     * @param storage Storage engine to test (optional)
     * @param health_monitor Health monitor to query (optional)
     * @return Comprehensive diagnostic report
     */
    static DiagnosticReport run_comprehensive_diagnostics(
        const SCD40Interface* sensor_interface = nullptr,
        const TimeSeriesStorage* storage = nullptr,
        const HealthMonitor* health_monitor = nullptr);
    
    /**
     * Test I2C communication and sensor connectivity
     * @param sensor_interface Sensor interface to test
     * @return Diagnostic result
     */
    static DiagnosticResult test_i2c_communication(const SCD40Interface* sensor_interface);
    
    /**
     * Test storage engine functionality
     * @param storage Storage engine to test
     * @return Diagnostic result
     */
    static DiagnosticResult test_storage_functionality(const TimeSeriesStorage* storage);
    
    /**
     * Test system resources (memory, CPU, disk)
     * @param data_directory Data directory to check
     * @return Diagnostic result
     */
    static DiagnosticResult test_system_resources(const std::string& data_directory = "/var/lib/sensor-daemon");
    
    /**
     * Test network connectivity (if applicable)
     * @return Diagnostic result
     */
    static DiagnosticResult test_network_connectivity();
    
    /**
     * Test file system permissions
     * @param data_directory Data directory to check
     * @return Diagnostic result
     */
    static DiagnosticResult test_file_permissions(const std::string& data_directory = "/var/lib/sensor-daemon");
    
    /**
     * Test configuration validity
     * @param config_path Path to configuration file
     * @return Diagnostic result
     */
    static DiagnosticResult test_configuration(const std::string& config_path = "/etc/sensor-daemon/config.toml");
    
    /**
     * Test logging system functionality
     * @return Diagnostic result
     */
    static DiagnosticResult test_logging_system();
    
    /**
     * Generate troubleshooting recommendations based on diagnostic results
     * @param report Diagnostic report
     * @return List of troubleshooting recommendations
     */
    static std::vector<std::string> generate_troubleshooting_recommendations(const DiagnosticReport& report);
    
    /**
     * Check system dependencies
     * @return Diagnostic result
     */
    static DiagnosticResult test_system_dependencies();
    
    /**
     * Test daemon process status
     * @return Diagnostic result
     */
    static DiagnosticResult test_daemon_status();
    
    /**
     * Test I2C bus for available devices
     * @param i2c_device I2C device path (e.g., "/dev/i2c-1")
     * @return Diagnostic result with detected devices
     */
    static DiagnosticResult test_i2c_bus(const std::string& i2c_device = "/dev/i2c-1");
    
    /**
     * Test sensor data quality
     * @param sensor_interface Sensor interface to test
     * @param num_samples Number of samples to collect for quality test
     * @return Diagnostic result with data quality metrics
     */
    static DiagnosticResult test_sensor_data_quality(
        const SCD40Interface* sensor_interface, 
        int num_samples = 5);
    
    /**
     * Test storage query performance
     * @param storage Storage engine to test
     * @return Diagnostic result with query performance metrics
     */
    static DiagnosticResult test_storage_query_performance(const TimeSeriesStorage* storage);
    
    /**
     * Create a health check endpoint for external monitoring
     * @param health_monitor Health monitor to use for status
     * @param config Endpoint configuration
     * @return true if endpoint created successfully
     */
    static bool create_health_endpoint(
        HealthMonitor* health_monitor,
        const HealthEndpointConfig& config = HealthEndpointConfig{});
    
    /**
     * Update health check endpoint with current status
     * @param health_monitor Health monitor to use for status
     * @param config Endpoint configuration
     * @return true if endpoint updated successfully
     */
    static bool update_health_endpoint(
        const HealthMonitor* health_monitor,
        const HealthEndpointConfig& config = HealthEndpointConfig{});

private:
    /**
     * Helper function to time diagnostic tests
     * @param test_function Function to execute and time
     * @return Duration of execution
     */
    template<typename Func>
    static std::chrono::milliseconds time_execution(Func&& test_function);
    
    /**
     * Check if a file exists and is readable
     * @param file_path Path to file
     * @return true if file exists and is readable
     */
    static bool check_file_readable(const std::string& file_path);
    
    /**
     * Check if a directory exists and is writable
     * @param dir_path Path to directory
     * @return true if directory exists and is writable
     */
    static bool check_directory_writable(const std::string& dir_path);
    
    /**
     * Get system information
     * @return System information string
     */
    static std::string get_system_info();
    
    /**
     * Check if a process is running
     * @param process_name Name of process to check
     * @return true if process is running
     */
    static bool is_process_running(const std::string& process_name);
    
    /**
     * Create parent directories for a file path if they don't exist
     * @param file_path Path to file
     * @return true if directories created or already exist
     */
    static bool create_parent_directories(const std::string& file_path);
};

/**
 * Command-line diagnostic utility
 */
class DiagnosticCLI {
public:
    /**
     * Run diagnostic CLI
     * @param argc Command line argument count
     * @param argv Command line arguments
     * @return Exit code
     */
    static int run(int argc, char* argv[]);
    
    /**
     * Run a specific diagnostic test
     * @param test_name Name of test to run
     * @param args Additional arguments for the test
     * @return Exit code
     */
    static int run_specific_test(const std::string& test_name, const std::vector<std::string>& args);
    
private:
    /**
     * Print usage information
     */
    static void print_usage();
    
    /**
     * Parse command line arguments
     * @param argc Argument count
     * @param argv Arguments
     * @return true if arguments are valid
     */
    static bool parse_arguments(int argc, char* argv[], std::string& test_name, std::vector<std::string>& args);
    
    /**
     * Get available test names
     * @return List of available test names
     */
    static std::vector<std::string> get_available_tests();
};

/**
 * Health monitoring HTTP server for remote monitoring
 * This is an optional component that provides a simple HTTP endpoint
 * for monitoring systems to check daemon health
 */
class HealthMonitorServer {
public:
    /**
     * Constructor
     * @param health_monitor Health monitor to use for status
     * @param storage Optional time series storage for data queries
     */
    HealthMonitorServer(HealthMonitor* health_monitor, TimeSeriesStorage* storage = nullptr);
    
    /**
     * Destructor - ensures server is stopped
     */
    ~HealthMonitorServer();
    
    /**
     * Start the health monitor server
     * @param port Port to listen on
     * @param bind_address Address to bind to (default: localhost)
     * @return true if server started successfully
     */
    bool start(int port = 8080, const std::string& bind_address = "127.0.0.1");
    
    /**
     * Stop the health monitor server
     */
    void stop();
    
    /**
     * Check if server is running
     * @return true if server is running
     */
    bool is_running() const;
    
    /**
     * Get server URL
     * @return URL for health endpoint
     */
    std::string get_url() const;

private:
    HealthMonitor* health_monitor_;
    TimeSeriesStorage* storage_;
    bool running_;
    int port_;
    std::string bind_address_;
    std::thread server_thread_;
    
    /**
     * Server thread function
     */
    void server_loop();
    
    /**
     * Handle health check request
     * @return JSON response with health status
     */
    std::string handle_health_request() const;
    
    /**
     * Handle metrics request
     * @return JSON response with detailed metrics
     */
    std::string handle_metrics_request() const;
    
    /**
     * Handle diagnostic request
     * @return JSON response with diagnostic information
     */
    std::string handle_diagnostic_request() const;
    
    /**
     * Handle readiness probe request
     * @return HTTP response indicating if daemon is ready
     */
    std::string handle_readiness_request() const;
    
    /**
     * Handle liveness probe request
     * @return HTTP response indicating if daemon is alive
     */
    std::string handle_liveness_request() const;
    
    /**
     * Handle recent data request
     * @param request Full HTTP request string
     * @return HTTP response with recent sensor readings
     */
    std::string handle_recent_data_request(const std::string& request) const;
    
    /**
     * Handle range data request
     * @param request Full HTTP request string
     * @return HTTP response with sensor readings in time range
     */
    std::string handle_range_data_request(const std::string& request) const;
    
    /**
     * Handle data info request
     * @param request Full HTTP request string
     * @return HTTP response with database information
     */
    std::string handle_data_info_request(const std::string& request) const;
    
    /**
     * Handle aggregates request
     * @param request Full HTTP request string
     * @return HTTP response with aggregated sensor data
     */
    std::string handle_aggregates_request(const std::string& request) const;
};

} // namespace sensor_daemon