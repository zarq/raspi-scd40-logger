#include <gtest/gtest.h>
#include <chrono>
#include "sensor_data.hpp"

using namespace sensor_daemon;

class SensorDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test timestamp
        test_timestamp = std::chrono::system_clock::now();
        
        // Create a complete sensor reading
        complete_reading.timestamp = test_timestamp;
        complete_reading.co2_ppm = 450.5f;
        complete_reading.temperature_c = 23.2f;
        complete_reading.humidity_percent = 65.8f;
        complete_reading.quality_flags = SensorReading::CO2_VALID | 
                                       SensorReading::TEMP_VALID | 
                                       SensorReading::HUMIDITY_VALID;
        
        // Create a partial sensor reading (missing some values)
        partial_reading.timestamp = test_timestamp;
        partial_reading.co2_ppm = 420.0f;
        // temperature_c and humidity_percent are not set (std::nullopt)
        partial_reading.quality_flags = SensorReading::CO2_VALID;
    }
    
    std::chrono::system_clock::time_point test_timestamp;
    SensorReading complete_reading;
    SensorReading partial_reading;
};

// Test conversion from internal struct to protobuf
TEST_F(SensorDataTest, ToProtobufComplete) {
    auto proto_reading = SensorDataConverter::to_protobuf(complete_reading);
    
    // Check timestamp conversion
    auto duration = complete_reading.timestamp.time_since_epoch();
    auto expected_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    EXPECT_EQ(proto_reading.timestamp_us(), static_cast<uint64_t>(expected_us));
    
    // Check all values are present
    EXPECT_TRUE(proto_reading.has_co2_ppm());
    EXPECT_FLOAT_EQ(proto_reading.co2_ppm(), 450.5f);
    
    EXPECT_TRUE(proto_reading.has_temperature_c());
    EXPECT_FLOAT_EQ(proto_reading.temperature_c(), 23.2f);
    
    EXPECT_TRUE(proto_reading.has_humidity_percent());
    EXPECT_FLOAT_EQ(proto_reading.humidity_percent(), 65.8f);
    
    // Check quality flags
    EXPECT_EQ(proto_reading.quality_flags(), 
              SensorReading::CO2_VALID | SensorReading::TEMP_VALID | SensorReading::HUMIDITY_VALID);
}

// Test conversion from internal struct to protobuf with missing values
TEST_F(SensorDataTest, ToProtobufPartial) {
    auto proto_reading = SensorDataConverter::to_protobuf(partial_reading);
    
    // Check timestamp conversion
    auto duration = partial_reading.timestamp.time_since_epoch();
    auto expected_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    EXPECT_EQ(proto_reading.timestamp_us(), static_cast<uint64_t>(expected_us));
    
    // Check only CO2 is present
    EXPECT_TRUE(proto_reading.has_co2_ppm());
    EXPECT_FLOAT_EQ(proto_reading.co2_ppm(), 420.0f);
    
    // Check missing values are not set
    EXPECT_FALSE(proto_reading.has_temperature_c());
    EXPECT_FALSE(proto_reading.has_humidity_percent());
    
    // Check quality flags
    EXPECT_EQ(proto_reading.quality_flags(), SensorReading::CO2_VALID);
}

// Test conversion from protobuf to internal struct
TEST_F(SensorDataTest, FromProtobufComplete) {
    // First convert to protobuf
    auto proto_reading = SensorDataConverter::to_protobuf(complete_reading);
    
    // Then convert back to internal struct
    auto converted_reading = SensorDataConverter::from_protobuf(proto_reading);
    
    // Check timestamp
    EXPECT_EQ(converted_reading.timestamp, complete_reading.timestamp);
    
    // Check all values
    EXPECT_TRUE(converted_reading.co2_ppm.has_value());
    EXPECT_FLOAT_EQ(converted_reading.co2_ppm.value(), 450.5f);
    
    EXPECT_TRUE(converted_reading.temperature_c.has_value());
    EXPECT_FLOAT_EQ(converted_reading.temperature_c.value(), 23.2f);
    
    EXPECT_TRUE(converted_reading.humidity_percent.has_value());
    EXPECT_FLOAT_EQ(converted_reading.humidity_percent.value(), 65.8f);
    
    // Check quality flags
    EXPECT_EQ(converted_reading.quality_flags, complete_reading.quality_flags);
}

// Test conversion from protobuf to internal struct with missing values
TEST_F(SensorDataTest, FromProtobufPartial) {
    // First convert to protobuf
    auto proto_reading = SensorDataConverter::to_protobuf(partial_reading);
    
    // Then convert back to internal struct
    auto converted_reading = SensorDataConverter::from_protobuf(proto_reading);
    
    // Check timestamp
    EXPECT_EQ(converted_reading.timestamp, partial_reading.timestamp);
    
    // Check only CO2 is present
    EXPECT_TRUE(converted_reading.co2_ppm.has_value());
    EXPECT_FLOAT_EQ(converted_reading.co2_ppm.value(), 420.0f);
    
    // Check missing values
    EXPECT_FALSE(converted_reading.temperature_c.has_value());
    EXPECT_FALSE(converted_reading.humidity_percent.has_value());
    
    // Check quality flags
    EXPECT_EQ(converted_reading.quality_flags, SensorReading::CO2_VALID);
}

// Test serialization and deserialization
TEST_F(SensorDataTest, SerializeDeserializeComplete) {
    // Serialize
    std::string serialized = SensorDataConverter::serialize(complete_reading);
    EXPECT_FALSE(serialized.empty());
    
    // Deserialize
    auto deserialized = SensorDataConverter::deserialize(serialized);
    EXPECT_TRUE(deserialized.has_value());
    
    auto& reading = deserialized.value();
    
    // Check all values match
    EXPECT_EQ(reading.timestamp, complete_reading.timestamp);
    EXPECT_TRUE(reading.co2_ppm.has_value());
    EXPECT_FLOAT_EQ(reading.co2_ppm.value(), 450.5f);
    EXPECT_TRUE(reading.temperature_c.has_value());
    EXPECT_FLOAT_EQ(reading.temperature_c.value(), 23.2f);
    EXPECT_TRUE(reading.humidity_percent.has_value());
    EXPECT_FLOAT_EQ(reading.humidity_percent.value(), 65.8f);
    EXPECT_EQ(reading.quality_flags, complete_reading.quality_flags);
}

// Test serialization and deserialization with partial data
TEST_F(SensorDataTest, SerializeDeserializePartial) {
    // Serialize
    std::string serialized = SensorDataConverter::serialize(partial_reading);
    EXPECT_FALSE(serialized.empty());
    
    // Deserialize
    auto deserialized = SensorDataConverter::deserialize(serialized);
    EXPECT_TRUE(deserialized.has_value());
    
    auto& reading = deserialized.value();
    
    // Check values match
    EXPECT_EQ(reading.timestamp, partial_reading.timestamp);
    EXPECT_TRUE(reading.co2_ppm.has_value());
    EXPECT_FLOAT_EQ(reading.co2_ppm.value(), 420.0f);
    EXPECT_FALSE(reading.temperature_c.has_value());
    EXPECT_FALSE(reading.humidity_percent.has_value());
    EXPECT_EQ(reading.quality_flags, SensorReading::CO2_VALID);
}

// Test deserialization failure with invalid data
TEST_F(SensorDataTest, DeserializeInvalidData) {
    std::string invalid_data = "invalid protobuf data";
    auto result = SensorDataConverter::deserialize(invalid_data);
    EXPECT_FALSE(result.has_value());
}

// Test quality flag convenience methods
TEST_F(SensorDataTest, QualityFlagMethods) {
    SensorReading reading;
    
    // Initially no flags set
    EXPECT_FALSE(reading.is_co2_valid());
    EXPECT_FALSE(reading.is_temperature_valid());
    EXPECT_FALSE(reading.is_humidity_valid());
    
    // Set CO2 valid
    reading.set_co2_valid(true);
    EXPECT_TRUE(reading.is_co2_valid());
    EXPECT_FALSE(reading.is_temperature_valid());
    EXPECT_FALSE(reading.is_humidity_valid());
    EXPECT_EQ(reading.quality_flags, SensorReading::CO2_VALID);
    
    // Set temperature valid
    reading.set_temperature_valid(true);
    EXPECT_TRUE(reading.is_co2_valid());
    EXPECT_TRUE(reading.is_temperature_valid());
    EXPECT_FALSE(reading.is_humidity_valid());
    EXPECT_EQ(reading.quality_flags, SensorReading::CO2_VALID | SensorReading::TEMP_VALID);
    
    // Set humidity valid
    reading.set_humidity_valid(true);
    EXPECT_TRUE(reading.is_co2_valid());
    EXPECT_TRUE(reading.is_temperature_valid());
    EXPECT_TRUE(reading.is_humidity_valid());
    EXPECT_EQ(reading.quality_flags, 
              SensorReading::CO2_VALID | SensorReading::TEMP_VALID | SensorReading::HUMIDITY_VALID);
    
    // Unset CO2 valid
    reading.set_co2_valid(false);
    EXPECT_FALSE(reading.is_co2_valid());
    EXPECT_TRUE(reading.is_temperature_valid());
    EXPECT_TRUE(reading.is_humidity_valid());
    EXPECT_EQ(reading.quality_flags, SensorReading::TEMP_VALID | SensorReading::HUMIDITY_VALID);
}

// Test timestamp precision
TEST_F(SensorDataTest, TimestampPrecision) {
    // Create a reading with a specific timestamp
    auto now = std::chrono::system_clock::now();
    SensorReading reading(now);
    reading.co2_ppm = 400.0f;
    reading.quality_flags = SensorReading::CO2_VALID;
    
    // Serialize and deserialize
    std::string serialized = SensorDataConverter::serialize(reading);
    auto deserialized = SensorDataConverter::deserialize(serialized);
    
    EXPECT_TRUE(deserialized.has_value());
    
    // Check timestamp precision (should be accurate to microseconds)
    auto original_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    auto deserialized_us = std::chrono::duration_cast<std::chrono::microseconds>(
        deserialized->timestamp.time_since_epoch()).count();
    
    EXPECT_EQ(original_us, deserialized_us);
}