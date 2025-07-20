#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <thread>
#include <chrono>
#include <cstdarg>
#include "scd40_interface.hpp"

namespace sensor_daemon {

// Mock I2C operations for testing
class MockI2CDevice {
public:
    static MockI2CDevice& instance() {
        static MockI2CDevice instance;
        return instance;
    }
    
    MOCK_METHOD(int, open, (const char* pathname, int flags));
    MOCK_METHOD(int, close, (int fd));
    MOCK_METHOD(int, ioctl, (int fd, unsigned long request, unsigned long arg));
    MOCK_METHOD(ssize_t, read, (int fd, void* buf, size_t count));
    MOCK_METHOD(ssize_t, write, (int fd, const void* buf, size_t count));
    
    void reset() {
        testing::Mock::VerifyAndClearExpectations(this);
    }
    
    // Helper methods to simulate sensor responses
    void setup_successful_init() {
        EXPECT_CALL(*this, open(testing::StrEq("/dev/i2c-1"), O_RDWR))
            .WillOnce(testing::Return(3)); // Mock file descriptor
        
        EXPECT_CALL(*this, ioctl(3, I2C_SLAVE, 0x62))
            .WillOnce(testing::Return(0));
        
        // Stop periodic measurement command
        EXPECT_CALL(*this, write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
        
        // Start periodic measurement command
        EXPECT_CALL(*this, write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
    }
    
    void setup_successful_read() {
        // Read measurement command
        EXPECT_CALL(*this, write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
        
        // Read measurement data with valid CRC
        EXPECT_CALL(*this, read(3, testing::_, 9))
            .WillOnce(testing::Invoke([](int, void* buf, size_t) -> ssize_t {
                uint8_t* buffer = static_cast<uint8_t*>(buf);
                
                // CO2: 800 ppm (0x0320)
                buffer[0] = 0x03;
                buffer[1] = 0x20;
                buffer[2] = calculate_test_crc8(&buffer[0], 2);
                
                // Temperature: ~25°C (raw value ~0x6666)
                buffer[3] = 0x66;
                buffer[4] = 0x66;
                buffer[5] = calculate_test_crc8(&buffer[3], 2);
                
                // Humidity: ~50% (raw value ~0x8000)
                buffer[6] = 0x80;
                buffer[7] = 0x00;
                buffer[8] = calculate_test_crc8(&buffer[6], 2);
                
                return 9;
            }));
    }
    
    void setup_failed_open() {
        EXPECT_CALL(*this, open(testing::StrEq("/dev/i2c-1"), O_RDWR))
            .WillOnce(testing::Return(-1));
    }
    
    void setup_failed_ioctl() {
        EXPECT_CALL(*this, open(testing::StrEq("/dev/i2c-1"), O_RDWR))
            .WillOnce(testing::Return(3));
        
        EXPECT_CALL(*this, ioctl(3, I2C_SLAVE, 0x62))
            .WillOnce(testing::Return(-1));
        
        EXPECT_CALL(*this, close(3))
            .WillOnce(testing::Return(0));
    }
    
    void setup_read_with_crc_error() {
        // Read measurement command
        EXPECT_CALL(*this, write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
        
        // Read measurement data with invalid CRC
        EXPECT_CALL(*this, read(3, testing::_, 9))
            .WillOnce(testing::Invoke([](int, void* buf, size_t) -> ssize_t {
                uint8_t* buffer = static_cast<uint8_t*>(buf);
                
                // CO2: 800 ppm (0x0320)
                buffer[0] = 0x03;
                buffer[1] = 0x20;
                buffer[2] = 0xFF; // Invalid CRC
                
                // Temperature and humidity with valid CRC
                buffer[3] = 0x66;
                buffer[4] = 0x66;
                buffer[5] = calculate_test_crc8(&buffer[3], 2);
                
                buffer[6] = 0x80;
                buffer[7] = 0x00;
                buffer[8] = calculate_test_crc8(&buffer[6], 2);
                
                return 9;
            }));
    }
    
    static uint8_t calculate_test_crc8(const uint8_t* data, size_t length) {
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
};

} // namespace sensor_daemon

// Override system calls for testing
extern "C" {
    int open(const char* pathname, int flags) {
        return sensor_daemon::MockI2CDevice::instance().open(pathname, flags);
    }
    
    int close(int fd) {
        return sensor_daemon::MockI2CDevice::instance().close(fd);
    }
    
    int ioctl(int fd, unsigned long request, ...) {
        va_list args;
        va_start(args, request);
        unsigned long arg = va_arg(args, unsigned long);
        va_end(args);
        return sensor_daemon::MockI2CDevice::instance().ioctl(fd, request, arg);
    }
    
    ssize_t read(int fd, void* buf, size_t count) {
        return sensor_daemon::MockI2CDevice::instance().read(fd, buf, count);
    }
    
    ssize_t write(int fd, const void* buf, size_t count) {
        return sensor_daemon::MockI2CDevice::instance().write(fd, buf, count);
    }
}

namespace sensor_daemon {

class SCD40InterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset mock expectations
        MockI2CDevice::instance().reset();
        
        // Setup default configuration
        config_.i2c_device = "/dev/i2c-1";
        config_.i2c_address = 0x62;
        config_.connection_timeout = std::chrono::milliseconds(1000);
        config_.max_retries = 3;
    }
    
    void TearDown() override {
        MockI2CDevice::instance().reset();
    }
    
    DaemonConfig::sensor config_;
};

TEST_F(SCD40InterfaceTest, ConstructorSetsConfiguration) {
    SCD40Interface interface(config_);
    
    EXPECT_FALSE(interface.is_connected());
    EXPECT_TRUE(interface.get_last_error().empty());
}

TEST_F(SCD40InterfaceTest, InitializeSuccess) {
    MockI2CDevice::instance().setup_successful_init();
    
    SCD40Interface interface(config_);
    EXPECT_TRUE(interface.initialize());
    EXPECT_TRUE(interface.is_connected());
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2)); // Stop periodic measurement
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, InitializeFailsOnDeviceOpenError) {
    MockI2CDevice::instance().setup_failed_open();
    
    SCD40Interface interface(config_);
    EXPECT_FALSE(interface.initialize());
    EXPECT_FALSE(interface.is_connected());
    EXPECT_FALSE(interface.get_last_error().empty());
}

TEST_F(SCD40InterfaceTest, InitializeFailsOnAddressSetError) {
    MockI2CDevice::instance().setup_failed_ioctl();
    
    SCD40Interface interface(config_);
    EXPECT_FALSE(interface.initialize());
    EXPECT_FALSE(interface.is_connected());
    EXPECT_FALSE(interface.get_last_error().empty());
}

TEST_F(SCD40InterfaceTest, ReadSensorSuccess) {
    MockI2CDevice::instance().setup_successful_init();
    MockI2CDevice::instance().setup_successful_read();
    
    SCD40Interface interface(config_);
    ASSERT_TRUE(interface.initialize());
    
    SensorReading reading = interface.read_sensor();
    
    EXPECT_TRUE(reading.co2_ppm.has_value());
    EXPECT_TRUE(reading.temperature_c.has_value());
    EXPECT_TRUE(reading.humidity_percent.has_value());
    
    EXPECT_FLOAT_EQ(reading.co2_ppm.value(), 800.0f);
    EXPECT_TRUE(reading.is_co2_valid());
    EXPECT_TRUE(reading.is_temperature_valid());
    EXPECT_TRUE(reading.is_humidity_valid());
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, ReadSensorFailsWhenNotConnected) {
    SCD40Interface interface(config_);
    
    EXPECT_THROW(interface.read_sensor(), I2CError);
}

TEST_F(SCD40InterfaceTest, ReadSensorHandlesCRCError) {
    MockI2CDevice::instance().setup_successful_init();
    
    // Setup multiple read attempts with CRC errors followed by success
    for (int i = 0; i < config_.max_retries - 1; i++) {
        MockI2CDevice::instance().setup_read_with_crc_error();
        
        // Reconnection attempts
        EXPECT_CALL(MockI2CDevice::instance(), close(3))
            .WillOnce(testing::Return(0));
        MockI2CDevice::instance().setup_successful_init();
    }
    
    // Final successful read
    MockI2CDevice::instance().setup_successful_read();
    
    SCD40Interface interface(config_);
    ASSERT_TRUE(interface.initialize());
    
    SensorReading reading = interface.read_sensor();
    EXPECT_TRUE(reading.co2_ppm.has_value());
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, ReadSensorThrowsAfterMaxRetries) {
    MockI2CDevice::instance().setup_successful_init();
    
    // Setup max_retries failed read attempts
    for (int i = 0; i < config_.max_retries; i++) {
        MockI2CDevice::instance().setup_read_with_crc_error();
        
        if (i < config_.max_retries - 1) {
            // Reconnection attempts
            EXPECT_CALL(MockI2CDevice::instance(), close(3))
                .WillOnce(testing::Return(0));
            MockI2CDevice::instance().setup_successful_init();
        }
    }
    
    SCD40Interface interface(config_);
    ASSERT_TRUE(interface.initialize());
    
    EXPECT_THROW(interface.read_sensor(), I2CError);
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, ValidateReadingRejectsOutOfRangeValues) {
    MockI2CDevice::instance().setup_successful_init();
    
    // Setup read with out-of-range CO2 value
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    
    EXPECT_CALL(MockI2CDevice::instance(), read(3, testing::_, 9))
        .WillOnce(testing::Invoke([](int, void* buf, size_t) -> ssize_t {
            uint8_t* buffer = static_cast<uint8_t*>(buf);
            
            // CO2: 50000 ppm (out of range, 0xC350)
            buffer[0] = 0xC3;
            buffer[1] = 0x50;
            buffer[2] = MockI2CDevice::calculate_test_crc8(&buffer[0], 2);
            
            // Valid temperature and humidity
            buffer[3] = 0x66;
            buffer[4] = 0x66;
            buffer[5] = MockI2CDevice::calculate_test_crc8(&buffer[3], 2);
            
            buffer[6] = 0x80;
            buffer[7] = 0x00;
            buffer[8] = MockI2CDevice::calculate_test_crc8(&buffer[6], 2);
            
            return 9;
        }));
    
    // Setup retry attempts that will also fail
    for (int i = 1; i < config_.max_retries; i++) {
        EXPECT_CALL(MockI2CDevice::instance(), close(3))
            .WillOnce(testing::Return(0));
        MockI2CDevice::instance().setup_successful_init();
        
        EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
        
        EXPECT_CALL(MockI2CDevice::instance(), read(3, testing::_, 9))
            .WillOnce(testing::Return(-1)); // Simulate read failure
    }
    
    SCD40Interface interface(config_);
    ASSERT_TRUE(interface.initialize());
    
    EXPECT_THROW(interface.read_sensor(), I2CError);
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, ConnectionStatsTracking) {
    MockI2CDevice::instance().setup_successful_init();
    MockI2CDevice::instance().setup_successful_read();
    
    SCD40Interface interface(config_);
    ASSERT_TRUE(interface.initialize());
    
    auto stats_before = interface.get_stats();
    EXPECT_EQ(stats_before.successful_reads, 0);
    EXPECT_EQ(stats_before.failed_reads, 0);
    
    SensorReading reading = interface.read_sensor();
    
    auto stats_after = interface.get_stats();
    EXPECT_EQ(stats_after.successful_reads, 1);
    EXPECT_EQ(stats_after.failed_reads, 0);
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, ExponentialBackoffCalculation) {
    SCD40Interface interface(config_);
    
    // Test exponential backoff calculation through reflection
    // Since calculate_backoff_delay is private, we test it indirectly
    // by observing timing behavior during retries
    
    MockI2CDevice::instance().setup_successful_init();
    
    // Setup failed reads to trigger backoff
    for (int i = 0; i < config_.max_retries; i++) {
        EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
            .WillOnce(testing::Return(2));
        EXPECT_CALL(MockI2CDevice::instance(), read(3, testing::_, 9))
            .WillOnce(testing::Return(-1));
        
        if (i < config_.max_retries - 1) {
            EXPECT_CALL(MockI2CDevice::instance(), close(3))
                .WillOnce(testing::Return(0));
            MockI2CDevice::instance().setup_successful_init();
        }
    }
    
    ASSERT_TRUE(interface.initialize());
    
    auto start_time = std::chrono::steady_clock::now();
    EXPECT_THROW(interface.read_sensor(), I2CError);
    auto end_time = std::chrono::steady_clock::now();
    
    // Should have taken some time due to backoff delays
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    EXPECT_GT(duration.count(), 100); // At least some backoff delay
    
    // Cleanup
    EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
        .WillOnce(testing::Return(2));
    EXPECT_CALL(MockI2CDevice::instance(), close(3))
        .WillOnce(testing::Return(0));
}

TEST_F(SCD40InterfaceTest, DestructorCleansUpProperly) {
    MockI2CDevice::instance().setup_successful_init();
    
    {
        SCD40Interface interface(config_);
        ASSERT_TRUE(interface.initialize());
        
        // Expect cleanup calls in destructor
        EXPECT_CALL(MockI2CDevice::instance(), write(3, testing::_, 2))
            .WillOnce(testing::Return(2)); // Stop periodic measurement
        EXPECT_CALL(MockI2CDevice::instance(), close(3))
            .WillOnce(testing::Return(0));
    } // Destructor called here
}

} // namespace sensor_daemon