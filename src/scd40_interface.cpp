#include "scd40_interface.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "logging_system.hpp"

namespace sensor_daemon {

SCD40Interface::SCD40Interface(const DaemonConfig::SensorSettings& config)
    : config_(config), i2c_fd_(-1), is_connected_(false) {
}

SCD40Interface::~SCD40Interface() {
    if (is_connected_.load()) {
        stop_periodic_measurement();
    }
    close_i2c_device();
}

bool SCD40Interface::initialize() {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);
    
    // Close existing connection if any
    if (i2c_fd_ >= 0) {
        close_i2c_device();
    }
    
    // Open I2C device
    if (!open_i2c_device()) {
        set_last_error("Failed to open I2C device: " + config_.i2c_device);
        return false;
    }
    
    // Set I2C slave address
    if (!set_i2c_address()) {
        set_last_error("Failed to set I2C address: 0x" + 
                      std::to_string(config_.i2c_address));
        close_i2c_device();
        return false;
    }
    
    // Stop any existing periodic measurement
    stop_periodic_measurement();
    
    // Wait a bit for sensor to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Start periodic measurement
    if (!start_periodic_measurement()) {
        set_last_error("Failed to start periodic measurement");
        close_i2c_device();
        return false;
    }
    
    is_connected_.store(true);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.last_connection_attempt = std::chrono::system_clock::now();
    }
    
    return true;
}

SensorData SCD40Interface::read_sensor() {
    SensorData reading(std::chrono::system_clock::now());
    
    // Check if connected
    if (!is_connected_.load()) {
        set_last_error("Sensor not connected");
        throw I2CError("Sensor not connected");
    }

    int attempt = 0;
    bool success = false;
    
    while (attempt < config_.max_retries && !success) {
        try {
            uint16_t raw_co2, raw_temp, raw_humidity;
            
            if (read_measurement_raw(raw_co2, raw_temp, raw_humidity)) {
                convert_raw_values(raw_co2, raw_temp, raw_humidity, reading);
                
                if (validate_reading(reading)) {
                    success = true;
                    update_stats(true);
                    
                    // Update last successful read time
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        stats_.last_successful_read = reading.timestamp;
                    }
                } else {
                    set_last_error("Sensor reading validation failed");
                }
            } else {
                set_last_error("Failed to read raw measurement data");
            }
            
        } catch (const std::exception& e) {
            set_last_error("Exception during sensor read: " + std::string(e.what()));
        }
        
        if (!success) {
            attempt++;
            if (attempt < config_.max_retries) {
                // Try to reconnect
                if (attempt_reconnection(attempt)) {
                    // Wait a bit before retrying
                    std::this_thread::sleep_for(calculate_backoff_delay(attempt));
                } else {
                    // Reconnection failed, wait longer
                    std::this_thread::sleep_for(calculate_backoff_delay(attempt) * 2);
                }
            }
        }
    }
    
    if (!success) {
        update_stats(false);
        throw I2CError("Failed to read sensor after " + std::to_string(config_.max_retries) + 
                      " attempts: " + last_error_);
    }
    
    return reading;
}

bool SCD40Interface::is_connected() const {
    return is_connected_.load();
}

std::string SCD40Interface::get_last_error() const {
    return last_error_;
}

SCD40Interface::ConnectionStats SCD40Interface::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool SCD40Interface::open_i2c_device() {
    i2c_fd_ = open(config_.i2c_device.c_str(), O_RDWR);
    return i2c_fd_ >= 0;
}

void SCD40Interface::close_i2c_device() {
    if (i2c_fd_ >= 0) {
        close(i2c_fd_);
        i2c_fd_ = -1;
    }
    is_connected_.store(false);
}

bool SCD40Interface::set_i2c_address() {
    if (i2c_fd_ < 0) {
        return false;
    }
    
    return ioctl(i2c_fd_, I2C_SLAVE, config_.i2c_address) >= 0;
}

bool SCD40Interface::start_periodic_measurement() {
    return send_command(SCD40_CMD_START_PERIODIC);
}

bool SCD40Interface::stop_periodic_measurement() {
    return send_command(SCD40_CMD_STOP_PERIODIC);
}

bool SCD40Interface::read_measurement_raw(uint16_t& co2, uint16_t& temperature, uint16_t& humidity) {
    // Send read measurement command
    if (!send_command(SCD40_CMD_READ_MEASUREMENT)) {
        return false;
    }
    
    // Wait for measurement to be ready
    std::this_thread::sleep_for(COMMAND_DELAY);
    
    // Read 9 bytes (3 values * 2 bytes + 3 CRC bytes)
    uint8_t buffer[9];
    if (!read_data_with_crc(buffer, sizeof(buffer))) {
        return false;
    }
    
    // Extract values (big-endian format)
    co2 = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
    temperature = (static_cast<uint16_t>(buffer[3]) << 8) | buffer[4];
    humidity = (static_cast<uint16_t>(buffer[6]) << 8) | buffer[7];
    
    return true;
}

bool SCD40Interface::send_command(uint16_t command) {
    if (i2c_fd_ < 0) {
        return false;
    }
    
    uint8_t buffer[2];
    buffer[0] = static_cast<uint8_t>(command >> 8);    // MSB
    buffer[1] = static_cast<uint8_t>(command & 0xFF);  // LSB
    return write_data(buffer, sizeof(buffer));
}

bool SCD40Interface::read_data_with_crc(uint8_t* buffer, size_t length) {
    if (i2c_fd_ < 0 || buffer == nullptr) {
        return false;
    }
    
    ssize_t bytes_read = read(i2c_fd_, buffer, length);
    if (bytes_read != static_cast<ssize_t>(length)) {
        LOG_DEBUG("Read failed, expected " + std::to_string(length) + 
                  " bytes, got " + std::to_string(bytes_read));
        return false;
    }
    LOG_DEBUG("Read " + std::to_string(bytes_read) + " bytes from sensor");
    
    // Validate CRC for each 3-byte group (2 data bytes + 1 CRC byte)
    for (size_t i = 0; i < length; i += 3) {
        if (i + 2 >= length) {
            break; // Not enough bytes for complete group
        }
        
        uint8_t calculated_crc = calculate_crc8(&buffer[i], 2);
        if (calculated_crc != buffer[i + 2]) {
            return false; // CRC mismatch
        }
    }
    
    return true;
}

bool SCD40Interface::write_data(const uint8_t* buffer, size_t length) {
    if (i2c_fd_ < 0 || buffer == nullptr) {
        return false;
    }
    
    ssize_t bytes_written = write(i2c_fd_, buffer, length);
    return bytes_written == static_cast<ssize_t>(length);
}

uint8_t SCD40Interface::calculate_crc8(const uint8_t* data, size_t length) {
    // CRC-8 polynomial: 0x31 (x^8 + x^5 + x^4 + 1)
    constexpr uint8_t CRC8_POLYNOMIAL = 0x31;
    constexpr uint8_t CRC8_INIT = 0xFF;
    
    uint8_t crc = CRC8_INIT;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        
        for (int bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

void SCD40Interface::convert_raw_values(uint16_t raw_co2, uint16_t raw_temp, uint16_t raw_humidity,
                                       SensorData& reading) {
    // Convert CO2 (ppm) - direct value
    if (raw_co2 != 0) {
        reading.co2_ppm = static_cast<float>(raw_co2);
        reading.set_co2_valid(true);
    }
    
    // Convert temperature (°C) - formula: -45 + 175 * (raw / 65536)
    if (raw_temp != 0) {
        reading.temperature_c = -45.0f + 175.0f * (static_cast<float>(raw_temp) / 65536.0f);
        reading.set_temperature_valid(true);
    }
    
    // Convert humidity (%) - formula: 100 * (raw / 65536)
    if (raw_humidity != 0) {
        reading.humidity_percent = 100.0f * (static_cast<float>(raw_humidity) / 65536.0f);
        reading.set_humidity_valid(true);
    }
}

bool SCD40Interface::validate_reading(const SensorData& reading) {
    bool valid = true;
    
    // Validate CO2 if present
    if (reading.co2_ppm.has_value()) {
        float co2 = reading.co2_ppm.value();
        if (co2 < CO2_MIN || co2 > CO2_MAX) {
            valid = false;
        }
    }
    
    // Validate temperature if present
    if (reading.temperature_c.has_value()) {
        float temp = reading.temperature_c.value();
        if (temp < TEMP_MIN || temp > TEMP_MAX) {
            valid = false;
        }
    }
    
    // Validate humidity if present
    if (reading.humidity_percent.has_value()) {
        float humidity = reading.humidity_percent.value();
        if (humidity < HUMIDITY_MIN || humidity > HUMIDITY_MAX) {
            valid = false;
        }
    }
    
    return valid;
}

bool SCD40Interface::attempt_reconnection(int attempt_count) {
    std::lock_guard<std::recursive_mutex> lock(connection_mutex_);

    // Update reconnection statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.reconnection_attempts++;
        stats_.last_connection_attempt = std::chrono::system_clock::now();
    }
    
    // Close existing connection
    close_i2c_device();
    
    // Wait before attempting reconnection
    std::this_thread::sleep_for(calculate_backoff_delay(attempt_count));
    
    // Try to reinitialize
    return initialize();
}

std::chrono::milliseconds SCD40Interface::calculate_backoff_delay(int attempt_count) {
    // Exponential backoff: base_delay * 2^(attempt - 1)
    // Cap at maximum delay to prevent excessive waiting
    constexpr auto base_delay = std::chrono::milliseconds(100);
    constexpr auto max_delay = std::chrono::milliseconds(5000);
    
    auto delay = base_delay * (1 << std::min(attempt_count - 1, 5)); // Cap exponent at 5
    return std::min(delay, max_delay);
}

void SCD40Interface::update_stats(bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (success) {
        stats_.successful_reads++;
    } else {
        stats_.failed_reads++;
    }
}

void SCD40Interface::set_last_error(const std::string& error) {
    last_error_ = error;
}

} // namespace sensor_daemon