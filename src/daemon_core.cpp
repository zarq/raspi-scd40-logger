#include "daemon_core.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace sensor_daemon {

// Static member definitions
std::atomic<bool> DaemonCore::signal_received_{false};
std::atomic<int> DaemonCore::received_signal_{0};
DaemonCore* DaemonCore::instance_ = nullptr;

// ErrorHandler implementation
void ErrorHandler::handle_error(const std::exception& e, ErrorSeverity severity, const std::string& operation) {
    log_error(e, severity, operation);
    update_health_metrics(severity);
    
    if (severity == ErrorSeverity::RECOVERABLE) {
        retry_counts_[operation]++;
    } else if (severity == ErrorSeverity::CRITICAL) {
        // Critical errors should trigger shutdown
        LOG_CRITICAL("Critical error in operation: " + operation + " - " + e.what());
    }
}

bool ErrorHandler::should_retry(const std::string& operation, int attempt_count) {
    return attempt_count < MAX_RETRIES;
}

std::chrono::milliseconds ErrorHandler::get_backoff_delay(int attempt_count) {
    auto delay = BASE_DELAY * (1 << std::min(attempt_count, 10)); // Cap at 2^10
    return std::min(delay, MAX_DELAY);
}

void ErrorHandler::reset_retry_count(const std::string& operation) {
    retry_counts_[operation] = 0;
}

void ErrorHandler::log_error(const std::exception& e, ErrorSeverity severity, const std::string& operation) {
    ErrorContext context("daemon_core", operation);
    context.add_data("error_type", typeid(e).name())
           .add_data("severity", severity == ErrorSeverity::RECOVERABLE ? "recoverable" :
                                severity == ErrorSeverity::WARNING ? "warning" : "critical");
    
    switch (severity) {
        case ErrorSeverity::RECOVERABLE:
            LoggingSystem::log_with_context(LogLevel::WARN, e.what(), context);
            break;
        case ErrorSeverity::WARNING:
            LoggingSystem::log_with_context(LogLevel::WARN, e.what(), context);
            break;
        case ErrorSeverity::CRITICAL:
            LoggingSystem::log_with_context(LogLevel::CRITICAL, e.what(), context);
            break;
    }
}

void ErrorHandler::update_health_metrics(ErrorSeverity severity) {
    // This would update health metrics - implementation depends on metrics system
    // For now, just log the severity
    LOG_DEBUG("Health metrics updated for error severity: " + 
              std::to_string(static_cast<int>(severity)));
}

// DaemonCore implementation
DaemonCore::DaemonCore() 
    : running_(false)
    , shutdown_requested_(false)
    , error_handler_(std::make_unique<ErrorHandler>())
    , last_metrics_log_(std::chrono::steady_clock::now()) {
    
    instance_ = this;
    metrics_.start_time = std::chrono::steady_clock::now();
}

DaemonCore::~DaemonCore() {
    if (running_) {
        shutdown();
    }
    cleanup_resources();
    instance_ = nullptr;
}

bool DaemonCore::initialize(const std::string& config_path) {
    try {
        // Load configuration
        config_ = ConfigManager::load_config(config_path);
        
        // Initialize logging system
        LogLevel log_level = LoggingSystem::string_to_log_level(config_.daemon.log_level);
        if (!LoggingSystem::initialize(log_level, "/var/log/sensor-daemon/daemon.log")) {
            std::cerr << "Failed to initialize logging system" << std::endl;
            return false;
        }
        
        LOG_INFO("Daemon initialization started", {{"config_path", config_path}});
        
        // Setup signal handlers
        setup_signal_handlers();
        
        // Initialize components
        if (!initialize_components()) {
            LOG_ERROR("Failed to initialize daemon components");
            return false;
        }
        
        // Notify systemd that we're ready
        notify_systemd("READY=1");
        
        LOG_INFO("Daemon initialization completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        error_handler_->handle_error(e, ErrorSeverity::CRITICAL, "initialization");
        return false;
    }
}

void DaemonCore::run() {
    if (running_) {
        LOG_WARN("Daemon is already running");
        return;
    }
    
    LOG_INFO("Starting daemon main loop");
    LoggingSystem::log_daemon_startup("1.0.0", "config_loaded");
    
    running_ = true;
    shutdown_requested_ = false;
    
    // Perform daemonization if not already done
    if (!daemonize()) {
        LOG_ERROR("Failed to daemonize process");
        running_ = false;
        return;
    }
    
    // Run main loop
    main_loop();
    
    // Cleanup
    cleanup_resources();
    running_ = false;
    
    LoggingSystem::log_daemon_shutdown("normal_shutdown");
    LOG_INFO("Daemon shutdown completed");
}

void DaemonCore::shutdown() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Shutdown requested");
    shutdown_requested_ = true;
    
    // Wait for main thread to finish if it's running
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    notify_systemd("STOPPING=1");
}

bool DaemonCore::is_running() const {
    return running_;
}

PerformanceMetrics DaemonCore::get_metrics() const {
    return metrics_;
}

void DaemonCore::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // Handle SIGTERM (systemd shutdown)
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        LOG_ERROR("Failed to setup SIGTERM handler");
    }
    
    // Handle SIGINT (Ctrl+C)
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        LOG_ERROR("Failed to setup SIGINT handler");
    }
    
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    LOG_DEBUG("Signal handlers configured");
}

void DaemonCore::signal_handler(int signal) {
    signal_received_ = true;
    received_signal_ = signal;
    
    if (instance_) {
        instance_->shutdown_requested_ = true;
    }
}

bool DaemonCore::daemonize() {
    // Skip daemonization if already a daemon or in debug mode
    if (getppid() == 1) {
        return true; // Already a daemon
    }
    
    // Fork the first time
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("First fork failed");
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }
    
    // Child continues - become session leader
    if (setsid() < 0) {
        LOG_ERROR("setsid failed");
        return false;
    }
    
    // Fork again to ensure we can't acquire a controlling terminal
    pid = fork();
    if (pid < 0) {
        LOG_ERROR("Second fork failed");
        return false;
    }
    
    if (pid > 0) {
        // First child exits
        exit(0);
    }
    
    // Second child continues
    // Change working directory to root
    if (chdir("/") < 0) {
        LOG_ERROR("chdir to / failed");
        return false;
    }
    
    // Set file permissions mask
    umask(0);
    
    // Close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }
    
    // Redirect stdin, stdout, stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
    
    LOG_DEBUG("Process daemonization completed");
    return true;
}

void DaemonCore::main_loop() {
    LOG_INFO("Entering main event loop");
    
    while (!shutdown_requested_ && running_) {
        try {
            // Check for signals
            if (signal_received_) {
                LOG_INFO("Signal received: " + std::to_string(received_signal_.load()));
                break;
            }
            
            // Check system health
            if (!check_system_health()) {
                LOG_WARN("System health check failed, continuing with caution");
            }
            
            // Perform sensor reading cycle
            if (!perform_sensor_cycle()) {
                if (!handle_recoverable_error("sensor_cycle", "Sensor cycle failed")) {
                    LOG_ERROR("Sensor cycle failed and recovery unsuccessful");
                    // Continue running but log the failure
                }
            } else {
                // Reset retry count on success
                error_handler_->reset_retry_count("sensor_cycle");
            }
            
            // Update performance metrics periodically
            update_performance_metrics();
            
            // Sleep until next cycle (can be interrupted by shutdown)
            if (!sleep_until_next_cycle()) {
                LOG_DEBUG("Sleep interrupted by shutdown signal");
                break;
            }
            
            // Send keepalive to systemd
            notify_systemd("WATCHDOG=1");
            
        } catch (const std::exception& e) {
            error_handler_->handle_error(e, ErrorSeverity::RECOVERABLE, "main_loop");
            
            // Sleep briefly before retrying to avoid tight error loops
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Main event loop exited");
}

bool DaemonCore::perform_sensor_cycle() {
    try {
        PERF_TIMER("sensor_cycle");
        
        // Read sensor data
        SensorReading reading = sensor_interface_->read_sensor();
        
        // Store the reading
        if (storage_->store_reading(reading)) {
            metrics_.sensor_readings_success++;
            metrics_.storage_writes_success++;
            
            LOG_DEBUG("Sensor reading stored successfully", {
                {"co2", reading.co2_ppm ? std::to_string(*reading.co2_ppm) : "null"},
                {"temp", reading.temperature_c ? std::to_string(*reading.temperature_c) : "null"},
                {"humidity", reading.humidity_percent ? std::to_string(*reading.humidity_percent) : "null"}
            });
            
            return true;
        } else {
            metrics_.storage_writes_failed++;
            LOG_WARN("Failed to store sensor reading");
            return false;
        }
        
    } catch (const I2CError& e) {
        metrics_.sensor_readings_failed++;
        metrics_.i2c_connection_failures++;
        error_handler_->handle_error(e, ErrorSeverity::RECOVERABLE, "sensor_reading");
        return false;
        
    } catch (const std::exception& e) {
        metrics_.sensor_readings_failed++;
        error_handler_->handle_error(e, ErrorSeverity::WARNING, "sensor_cycle");
        return false;
    }
}

bool DaemonCore::initialize_components() {
    try {
        // Initialize sensor interface
        sensor_interface_ = std::make_unique<SCD40Interface>(config_.sensor);
        if (!sensor_interface_->initialize()) {
            LOG_ERROR("Failed to initialize sensor interface");
            return false;
        }
        LOG_INFO("Sensor interface initialized");
        
        // Initialize storage
        storage_ = std::make_unique<TimeSeriesStorage>();
        if (!storage_->initialize(config_.storage.data_directory, config_.daemon.data_retention)) {
            LOG_ERROR("Failed to initialize storage engine");
            return false;
        }
        LOG_INFO("Storage engine initialized");
        
        return true;
        
    } catch (const std::exception& e) {
        error_handler_->handle_error(e, ErrorSeverity::CRITICAL, "component_initialization");
        return false;
    }
}

void DaemonCore::cleanup_resources() {
    LOG_DEBUG("Cleaning up resources");
    
    // Reset components in reverse order of initialization
    storage_.reset();
    sensor_interface_.reset();
    error_handler_.reset();
    
    // Shutdown logging system
    LoggingSystem::shutdown();
}

bool DaemonCore::handle_recoverable_error(const std::string& operation, const std::string& error_message) {
    static std::unordered_map<std::string, int> attempt_counts;
    
    int& attempts = attempt_counts[operation];
    attempts++;
    
    if (!error_handler_->should_retry(operation, attempts)) {
        LOG_ERROR("Maximum retry attempts exceeded for operation: " + operation);
        attempts = 0; // Reset for next time
        return false;
    }
    
    auto delay = error_handler_->get_backoff_delay(attempts);
    LOG_WARN("Retrying operation '" + operation + "' in " + std::to_string(delay.count()) + "ms (attempt " + std::to_string(attempts) + ")");
    
    std::this_thread::sleep_for(delay);
    return true;
}

void DaemonCore::update_performance_metrics() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_metrics_log_;
    
    // Log metrics every 5 minutes
    if (elapsed >= std::chrono::minutes(5)) {
        metrics_.memory_usage_bytes = get_memory_usage();
        metrics_.cpu_usage_percent = get_cpu_usage();
        
        LoggingSystem::log_performance_metrics(metrics_);
        last_metrics_log_ = now;
    }
}

void DaemonCore::notify_systemd(const std::string& status) {
    if (sd_notify(0, status.c_str()) < 0) {
        LOG_DEBUG("Failed to notify systemd: " + status);
    }
}

bool DaemonCore::check_system_health() {
    // Check memory usage
    uint64_t memory_usage = get_memory_usage();
    if (memory_usage > 10 * 1024 * 1024) { // 10MB limit
        LOG_WARN("Memory usage exceeds limit", {{"usage_mb", std::to_string(memory_usage / 1024 / 1024)}});
        return false;
    }
    
    // Check storage health
    if (!storage_->is_healthy()) {
        LOG_WARN("Storage engine reports unhealthy status");
        return false;
    }
    
    // Check sensor connectivity
    if (!sensor_interface_->is_connected()) {
        LOG_WARN("Sensor interface reports disconnected status");
        return false;
    }
    
    return true;
}

bool DaemonCore::sleep_until_next_cycle() {
    auto sleep_duration = config_.daemon.sampling_interval;
    auto start_time = std::chrono::steady_clock::now();
    
    while (!shutdown_requested_ && !signal_received_) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= sleep_duration) {
            break;
        }
        
        // Sleep in small increments to allow for responsive shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return !shutdown_requested_ && !signal_received_;
}

uint64_t DaemonCore::get_memory_usage() const {
    std::ifstream status_file("/proc/self/status");
    std::string line;
    
    while (std::getline(status_file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string label, value, unit;
            iss >> label >> value >> unit;
            
            uint64_t memory_kb = std::stoull(value);
            return memory_kb * 1024; // Convert to bytes
        }
    }
    
    return 0;
}

double DaemonCore::get_cpu_usage() const {
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
        // CPU time is in clock ticks, convert to percentage
        long clock_ticks_per_sec = sysconf(_SC_CLK_TCK);
        cpu_usage = (static_cast<double>(cpu_diff) / clock_ticks_per_sec) / (time_diff / 1000.0) * 100.0;
    }
    
    last_time = current_time;
    last_cpu_time = total_cpu_time;
    
    return cpu_usage;
}

} // namespace sensor_daemon