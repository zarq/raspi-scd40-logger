#include <iostream>
#include <string>
#include <cstdlib>
#include <getopt.h>
#include <unistd.h>
#include <csignal>
#include <iomanip>
#include "daemon_core.hpp"
#include "config_manager.hpp"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Sensor Data Collection Daemon\n\n"
              << "Options:\n"
              << "  -c, --config PATH    Configuration file path (default: /etc/sensor-daemon/config.toml)\n"
              << "  -h, --help          Show this help message\n"
              << "  -v, --version       Show version information\n"
              << "  -f, --foreground    Run in foreground (don't daemonize)\n"
              << std::endl;
}

void print_version() {
    std::cout << "Sensor Daemon v1.0.0\n"
              << "Built with C++20, RocksDB, and systemd support\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string config_path = "/etc/sensor-daemon/config.toml";
    bool foreground = false;
    
    // Parse command line arguments
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"foreground", no_argument, 0, 'f'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "c:hvf", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                config_path = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            case 'f':
                foreground = true;
                break;
            case '?':
                print_usage(argv[0]);
                return 1;
            default:
                break;
        }
    }
    
    // Validate configuration file path
    if (!config_path.empty() && access(config_path.c_str(), R_OK) != 0) {
        std::cerr << "Error: Configuration file '" << config_path 
                  << "' is not readable or does not exist" << std::endl;
        return 1;
    }
    
    try {
        // Create and initialize daemon
        sensor_daemon::DaemonCore daemon;
        
        if (foreground) {
            std::cout << "Initializing sensor daemon..." << std::endl;
            std::cout << "Configuration file: " << config_path << std::endl;
        }
        
        if (!daemon.initialize(config_path, foreground)) {
            std::cerr << "Failed to initialize daemon. Check logs for details." << std::endl;
            if (foreground) {
                std::cerr << "Common issues:" << std::endl;
                std::cerr << "  - Configuration file not found or invalid" << std::endl;
                std::cerr << "  - Insufficient permissions for data directory" << std::endl;
                std::cerr << "  - I2C device not accessible (check /dev/i2c-* permissions)" << std::endl;
            }
            return 1;
        }
        
        if (foreground) {
            std::cout << "Sensor daemon initialized successfully" << std::endl;
            std::cout << "Starting main data collection loop..." << std::endl;
            std::cout << "Press Ctrl+C to stop the daemon" << std::endl;
        }
        
        // Run the daemon
        daemon.run();
        
        if (foreground) {
            auto metrics = daemon.get_metrics();
            std::cout << "Sensor daemon stopped." << std::endl;
            std::cout << "Session statistics:" << std::endl;
            std::cout << "  Uptime: " << metrics.get_uptime().count() << " seconds" << std::endl;
            std::cout << "  Successful sensor readings: " << metrics.sensor_readings_success << std::endl;
            std::cout << "  Failed sensor readings: " << metrics.sensor_readings_failed << std::endl;
            std::cout << "  Successful storage writes: " << metrics.storage_writes_success << std::endl;
            std::cout << "  Failed storage writes: " << metrics.storage_writes_failed << std::endl;
            if (metrics.sensor_readings_success + metrics.sensor_readings_failed > 0) {
                std::cout << "  Sensor success rate: " << 
                    std::fixed << std::setprecision(1) << 
                    (metrics.get_sensor_success_rate() * 100.0) << "%" << std::endl;
            }
        }
        
        return 0;
        
    } catch (const sensor_daemon::ConfigurationError& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        std::cerr << "Please check your configuration file: " << config_path << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}