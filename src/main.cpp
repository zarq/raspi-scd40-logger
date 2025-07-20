#include <iostream>
#include <string>
#include <cstdlib>
#include <getopt.h>
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
    
    try {
        // Create and initialize daemon
        sensor_daemon::DaemonCore daemon;
        
        if (!daemon.initialize(config_path)) {
            std::cerr << "Failed to initialize daemon" << std::endl;
            return 1;
        }
        
        // Run the daemon
        daemon.run();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}