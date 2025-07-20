#include "sensor_data.hpp"
#include <chrono>

namespace sensor_daemon {

sensor_daemon::SensorReading SensorDataConverter::to_protobuf(const SensorReading& reading) {
    sensor_daemon::SensorReading proto_reading;
    
    // Convert timestamp to microseconds since Unix epoch
    auto duration = reading.timestamp.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    proto_reading.set_timestamp_us(static_cast<uint64_t>(microseconds.count()));
    
    // Set optional fields only if they have values
    if (reading.co2_ppm.has_value()) {
        proto_reading.set_co2_ppm(reading.co2_ppm.value());
    }
    
    if (reading.temperature_c.has_value()) {
        proto_reading.set_temperature_c(reading.temperature_c.value());
    }
    
    if (reading.humidity_percent.has_value()) {
        proto_reading.set_humidity_percent(reading.humidity_percent.value());
    }
    
    // Set quality flags
    proto_reading.set_quality_flags(reading.quality_flags);
    
    return proto_reading;
}

SensorReading SensorDataConverter::from_protobuf(const sensor_daemon::SensorReading& proto_reading) {
    SensorReading reading;
    
    // Convert timestamp from microseconds since Unix epoch
    auto microseconds = std::chrono::microseconds(proto_reading.timestamp_us());
    reading.timestamp = std::chrono::system_clock::time_point(microseconds);
    
    // Set optional fields only if they are present in the protobuf message
    if (proto_reading.has_co2_ppm()) {
        reading.co2_ppm = proto_reading.co2_ppm();
    }
    
    if (proto_reading.has_temperature_c()) {
        reading.temperature_c = proto_reading.temperature_c();
    }
    
    if (proto_reading.has_humidity_percent()) {
        reading.humidity_percent = proto_reading.humidity_percent();
    }
    
    // Set quality flags
    reading.quality_flags = proto_reading.quality_flags();
    
    return reading;
}

std::string SensorDataConverter::serialize(const SensorReading& reading) {
    auto proto_reading = to_protobuf(reading);
    std::string serialized_data;
    
    if (!proto_reading.SerializeToString(&serialized_data)) {
        return ""; // Return empty string on serialization failure
    }
    
    return serialized_data;
}

std::optional<SensorReading> SensorDataConverter::deserialize(const std::string& data) {
    sensor_daemon::SensorReading proto_reading;
    
    if (!proto_reading.ParseFromString(data)) {
        return std::nullopt; // Return nullopt on deserialization failure
    }
    
    return from_protobuf(proto_reading);
}

} // namespace sensor_daemon