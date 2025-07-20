#include "include/config_manager.hpp"
#include <iostream>
#include <exception>

using namespace sensor_daemon;

int main() {
    try {
        // Test 1: Get default configuration
        std::cout << "Testing default configuration..." << std::endl;
        auto default_config = ConfigManager::get_default_config();
        
        std::cout << "Default sampling interval: " << default_config.daemon.sampling_interval.count() << " seconds" << std::endl;
        std::cout << "Default log level: " << default_config.daemon.log_level << std::endl;
        std::cout << "Default I2C device: " << default_config.sensor.i2c_device << std::endl;
        std::cout << "Default I2C address: 0x" << std::hex << static_cast<int>(default_config.sensor.i2c_address) << std::endl;
        std::cout << "Default data directory: " << default_config.storage.data_directory << std::endl;
        
        // Test 2: Validate default configuration (should pass except for I2C device check)
        std::cout << "\nTesting configuration validation..." << std::endl;
        try {
            ConfigManager::validate_config(default_config);
            std::cout << "Default configuration validation: PASSED" << std::endl;
        } catch (const ConfigurationError& e) {
            std::cout << "Default configuration validation: FAILED (expected due to I2C device check)" << std::endl;
            std::cout << "Error: " << e.what() << std::endl;
        }
        
        // Test 3: Try to load existing config file
        std::cout << "\nTesting configuration file loading..." << std::endl;
        try {
            auto config = ConfigManager::load_config("config/sensor-daemon.toml");
            std::cout << "Configuration loaded successfully!" << std::endl;
            std::cout << "Loaded sampling interval: " << config.daemon.sampling_interval.count() << " seconds" << std::endl;
            std::cout << "Loaded log level: " << config.daemon.log_level << std::endl;
        } catch (const ConfigurationError& e) {
            std::cout << "Configuration loading failed (expected): " << e.what() << std::endl;
        }
        
        // Test 4: Test validation with invalid values
        std::cout << "\nTesting validation with invalid values..." << std::endl;
        auto invalid_config = default_config;
        invalid_config.daemon.sampling_interval = std::chrono::seconds(0);  // Invalid
        
        try {
            ConfigManager::validate_config(invalid_config);
            std::cout << "Invalid configuration validation: UNEXPECTEDLY PASSED" << std::endl;
        } catch (const ConfigurationError& e) {
            std::cout << "Invalid configuration validation: CORRECTLY FAILED" << std::endl;
            std::cout << "Error: " << e.what() << std::endl;
        }
        
        std::cout << "\nConfiguration manager test completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}