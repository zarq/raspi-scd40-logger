#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <csignal>
#include <functional>
#include <systemd/sd-daemon.h>
#include "config_manager.hpp"
#include "logging_system.hpp"
#include "scd40_interface.hpp"
#include "time_series_storage.hpp"

namespace sensor_daemon {

/**
 * Error severity levels for error handling and recovery
 */
enum class ErrorSeverity {
    RECOVERABLE,    // Temporary errors that can be retried
    WARNING,        // Non-critical errors that don't stop operation
    CRITICAL        // Fatal errors that require daemon shutdown
};

/**
 * Error handler for managing different types of errors and recovery strategies
 */
class ErrorHandler {
public:
    /**
     * Handle an error with appropriate recovery strategy
     * @param e Exception that occurred
     * @param severity Severity level of the error
     * @param operation Description of the operation that failed
     */
    void handle_error(const std::exception& e, ErrorSeverity severity, const std::string& operation);
    
    /**
     * Check if an operation should be retried
     * @param operation Operation identifier
     * @param attempt_count Current attempt number
     * @return true if retry should be attempted
     */
    bool should_retry(const std::string& operation, int attempt_count);
    
    /**
     * Get exponential backoff delay for retry
     * @param attempt_count Current attempt number
     * @return Delay duration before next retry
     */
    std::chrono::milliseconds get_backoff_delay(int attempt_count);
    
    /**
     * Reset retry count for an operation
     * @param operation Operation identifier
     */
    void reset_retry_count(const std::string& operation);

private:
    std::unordered_map<std::string, int> retry_counts_;
    static constexpr int MAX_RETRIES = 5;
    static constexpr std::chrono::milliseconds BASE_DELAY{100};
    static constexpr std::chrono::milliseconds MAX_DELAY{30000}; // 30 seconds
    
    void log_error(const std::exception& e, ErrorSeverity severity, const std::string& operation);
    void update_health_metrics(ErrorSeverity severity);
};

/**
 * Main daemon core class responsible for process management and coordination
 */
class DaemonCore {
public:
    /**
     * Constructor
     */
    DaemonCore();
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~DaemonCore();
    
    // Non-copyable and non-movable
    DaemonCore(const DaemonCore&) = delete;
    DaemonCore& operator=(const DaemonCore&) = delete;
    DaemonCore(DaemonCore&&) = delete;
    DaemonCore& operator=(DaemonCore&&) = delete;
    
    /**
     * Initialize the daemon with configuration
     * @param config_path Path to configuration file
     * @return true if initialization successful
     */
    bool initialize(const std::string& config_path);
    
    /**
     * Run the main daemon loop
     * This method blocks until shutdown is requested
     */
    void run();
    
    /**
     * Request graceful shutdown
     */
    void shutdown();
    
    /**
     * Check if daemon is running
     * @return true if daemon is currently running
     */
    bool is_running() const;
    
    /**
     * Get current performance metrics
     * @return Performance metrics structure
     */
    PerformanceMetrics get_metrics() const;

private:
    // Configuration and components
    DaemonConfig config_;
    std::unique_ptr<SCD40Interface> sensor_interface_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<ErrorHandler> error_handler_;
    
    // Runtime state
    std::atomic<bool> running_;
    std::atomic<bool> shutdown_requested_;
    std::thread main_thread_;
    
    // Performance tracking
    PerformanceMetrics metrics_;
    std::chrono::steady_clock::time_point last_metrics_log_;
    
    // Signal handling
    static std::atomic<bool> signal_received_;
    static std::atomic<int> received_signal_;
    static DaemonCore* instance_;
    
    /**
     * Setup signal handlers for graceful shutdown
     */
    void setup_signal_handlers();
    
    /**
     * Static signal handler function
     * @param signal Signal number received
     */
    static void signal_handler(int signal);
    
    /**
     * Perform daemon process setup (fork, setsid, etc.)
     * @return true if daemonization successful
     */
    bool daemonize();
    
    /**
     * Main event loop implementation
     */
    void main_loop();
    
    /**
     * Perform a single sensor reading cycle
     * @return true if cycle completed successfully
     */
    bool perform_sensor_cycle();
    
    /**
     * Initialize all components
     * @return true if all components initialized successfully
     */
    bool initialize_components();
    
    /**
     * Cleanup all resources
     */
    void cleanup_resources();
    
    /**
     * Handle recoverable errors with retry logic
     * @param operation Operation that failed
     * @param error_message Error description
     * @return true if recovery successful or retry should continue
     */
    bool handle_recoverable_error(const std::string& operation, const std::string& error_message);
    
    /**
     * Update and log performance metrics
     */
    void update_performance_metrics();
    
    /**
     * Send systemd notifications about daemon status
     * @param status Status message to send
     */
    void notify_systemd(const std::string& status);
    
    /**
     * Check system resources and health
     * @return true if system is healthy
     */
    bool check_system_health();
    
    /**
     * Sleep for the configured sampling interval
     * Can be interrupted by shutdown signal
     * @return true if sleep completed normally, false if interrupted
     */
    bool sleep_until_next_cycle();
    
    /**
     * Get current memory usage in bytes
     * @return Memory usage in bytes, or 0 if unable to determine
     */
    uint64_t get_memory_usage() const;
    
    /**
     * Get current CPU usage percentage
     * @return CPU usage percentage, or 0.0 if unable to determine
     */
    double get_cpu_usage() const;
};

} // namespace sensor_daemon