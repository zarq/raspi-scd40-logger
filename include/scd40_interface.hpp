#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>
#include "sensor_data.hpp"
#include "config_manager.hpp"

namespace sensor_daemon {

/**
 * Exception thrown when I2C communication fails
 */
class I2CError : public std::runtime_error {
public:
    explicit I2CError(const std::string& message) 
        : std::runtime_error(message) {}
};

/**
 * Interface for I2C communication with SCD40 sensor
 * Handles connection management, data reading, and error recovery
 */
class SCD40Interface {
public:
    /**
     * Constructor
     * @param config Sensor configuration containing I2C parameters
     */
    explicit SCD40Interface(const DaemonConfig::SensorSettings& config);
    
    /**
     * Destructor - ensures proper cleanup
     */
    ~SCD40Interface();
    
    /**
     * Initialize I2C connection and sensor
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * Read sensor data (CO2, temperature, humidity)
     * @return SensorData with timestamp and sensor values
     * @throws I2CError if communication fails after all retries
     */
    SensorData read_sensor();
    
    /**
     * Check if sensor is currently connected
     * @return true if sensor is connected and responsive
     */
    bool is_connected() const;
    
    /**
     * Get last error message
     * @return String describing the last error that occurred
     */
    std::string get_last_error() const;
    
    /**
     * Get connection statistics
     */
    struct ConnectionStats {
        uint64_t successful_reads = 0;
        uint64_t failed_reads = 0;
        uint64_t reconnection_attempts = 0;
        std::chrono::system_clock::time_point last_successful_read;
        std::chrono::system_clock::time_point last_connection_attempt;
    };
    
    ConnectionStats get_stats() const;

private:
    // Configuration
    const DaemonConfig::SensorSettings config_;
    
    // I2C file descriptor
    int i2c_fd_;
    
    // Connection state
    mutable std::recursive_mutex connection_mutex_;
    std::atomic<bool> is_connected_;
    std::string last_error_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    ConnectionStats stats_;
    
    // SCD40 specific constants
    static constexpr uint16_t SCD40_CMD_START_PERIODIC = 0x21b1;
    static constexpr uint16_t SCD40_CMD_READ_MEASUREMENT = 0xec05;
    static constexpr uint16_t SCD40_CMD_STOP_PERIODIC = 0x3f86;
    static constexpr uint16_t SCD40_CMD_GET_SERIAL = 0x3682;
    
    // Timing constants
    static constexpr std::chrono::milliseconds MEASUREMENT_INTERVAL{5000};
    static constexpr std::chrono::milliseconds COMMAND_DELAY{1};
    static constexpr std::chrono::milliseconds READ_TIMEOUT{100};
    
    // Data validation ranges
    static constexpr float CO2_MIN = 400.0f;
    static constexpr float CO2_MAX = 40000.0f;
    static constexpr float TEMP_MIN = -40.0f;
    static constexpr float TEMP_MAX = 70.0f;
    static constexpr float HUMIDITY_MIN = 0.0f;
    static constexpr float HUMIDITY_MAX = 100.0f;
    
    /**
     * Open I2C device file
     * @return true if successful
     */
    bool open_i2c_device();
    
    /**
     * Close I2C device file
     */
    void close_i2c_device();
    
    /**
     * Set I2C slave address
     * @return true if successful
     */
    bool set_i2c_address();
    
    /**
     * Start periodic measurement mode on SCD40
     * @return true if successful
     */
    bool start_periodic_measurement();
    
    /**
     * Stop periodic measurement mode on SCD40
     * @return true if successful
     */
    bool stop_periodic_measurement();
    
    /**
     * Read raw measurement data from sensor
     * @param co2 Output parameter for CO2 value
     * @param temperature Output parameter for temperature value
     * @param humidity Output parameter for humidity value
     * @return true if read successful
     */
    bool read_measurement_raw(uint16_t& co2, uint16_t& temperature, uint16_t& humidity);
    
    /**
     * Send command to SCD40
     * @param command 16-bit command code
     * @return true if successful
     */
    bool send_command(uint16_t command);
    
    /**
     * Read data from I2C with CRC validation
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @return true if successful and CRC valid
     */
    bool read_data_with_crc(uint8_t* buffer, size_t length);
    
    /**
     * Write data to I2C
     * @param buffer Data to write
     * @param length Number of bytes to write
     * @return true if successful
     */
    bool write_data(const uint8_t* buffer, size_t length);
    
    /**
     * Calculate CRC8 checksum for SCD40 protocol
     * @param data Data to calculate CRC for
     * @param length Length of data
     * @return CRC8 checksum
     */
    uint8_t calculate_crc8(const uint8_t* data, size_t length);
    
    /**
     * Convert raw sensor values to physical units
     * @param raw_co2 Raw CO2 value from sensor
     * @param raw_temp Raw temperature value from sensor
     * @param raw_humidity Raw humidity value from sensor
     * @param reading Output SensorData structure
     */
    void convert_raw_values(uint16_t raw_co2, uint16_t raw_temp, uint16_t raw_humidity, 
                           SensorData& reading);
    
    /**
     * Validate sensor reading values
     * @param reading SensorData to validate
     * @return true if all present values are within valid ranges
     */
    bool validate_reading(const SensorData& reading);
    
    /**
     * Attempt to reconnect to sensor with exponential backoff
     * @param attempt_count Current attempt number (for backoff calculation)
     * @return true if reconnection successful
     */
    bool attempt_reconnection(int attempt_count);
    
    /**
     * Calculate exponential backoff delay
     * @param attempt_count Current attempt number
     * @return Delay duration
     */
    std::chrono::milliseconds calculate_backoff_delay(int attempt_count);
    
    /**
     * Update connection statistics
     * @param success Whether the operation was successful
     */
    void update_stats(bool success);
    
    /**
     * Set last error message
     * @param error Error message
     */
    void set_last_error(const std::string& error);
};

} // namespace sensor_daemon