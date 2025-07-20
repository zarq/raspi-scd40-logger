#pragma once

#include <chrono>
#include <optional>
#include "sensor_data.pb.h"

namespace sensor_daemon {

/**
 * Internal C++ representation of sensor data with optional fields for missing values
 * This struct is used throughout the application for type safety and convenience
 */
struct SensorReading {
    std::chrono::system_clock::time_point timestamp;
    std::optional<float> co2_ppm;
    std::optional<float> temperature_c;
    std::optional<float> humidity_percent;
    
    // Quality flags for data validation
    enum QualityFlags : uint32_t {
        CO2_VALID = 0x01,
        TEMP_VALID = 0x02,
        HUMIDITY_VALID = 0x04
    };
    uint32_t quality_flags = 0;
    
    // Default constructor
    SensorReading() = default;
    
    // Constructor with timestamp
    explicit SensorReading(std::chrono::system_clock::time_point ts) 
        : timestamp(ts) {}
    
    // Convenience methods for quality flags
    bool is_co2_valid() const { return quality_flags & CO2_VALID; }
    bool is_temperature_valid() const { return quality_flags & TEMP_VALID; }
    bool is_humidity_valid() const { return quality_flags & HUMIDITY_VALID; }
    
    void set_co2_valid(bool valid) {
        if (valid) quality_flags |= CO2_VALID;
        else quality_flags &= ~CO2_VALID;
    }
    
    void set_temperature_valid(bool valid) {
        if (valid) quality_flags |= TEMP_VALID;
        else quality_flags &= ~TEMP_VALID;
    }
    
    void set_humidity_valid(bool valid) {
        if (valid) quality_flags |= HUMIDITY_VALID;
        else quality_flags &= ~HUMIDITY_VALID;
    }
};

/**
 * Conversion functions between internal struct and protobuf message
 */
class SensorDataConverter {
public:
    /**
     * Convert internal SensorReading struct to protobuf message
     * @param reading Internal sensor reading struct
     * @return Protobuf message ready for serialization
     */
    static sensor_daemon::SensorReading to_protobuf(const SensorReading& reading);
    
    /**
     * Convert protobuf message to internal SensorReading struct
     * @param proto_reading Protobuf message from deserialization
     * @return Internal sensor reading struct
     */
    static SensorReading from_protobuf(const sensor_daemon::SensorReading& proto_reading);
    
    /**
     * Serialize internal SensorReading to binary string
     * @param reading Internal sensor reading struct
     * @return Serialized binary data
     */
    static std::string serialize(const SensorReading& reading);
    
    /**
     * Deserialize binary string to internal SensorReading
     * @param data Serialized binary data
     * @return Internal sensor reading struct, or nullopt if deserialization fails
     */
    static std::optional<SensorReading> deserialize(const std::string& data);
};

} // namespace sensor_daemon