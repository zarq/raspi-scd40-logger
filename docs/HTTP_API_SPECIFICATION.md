# Sensor Daemon HTTP API Specification

This document specifies the HTTP API endpoints that need to be added to the sensor-daemon to support the Python interface.

## Overview

The sensor-daemon already has an HTTP server for health monitoring. This specification extends it with data query endpoints to enable Python client access without requiring direct RocksDB dependencies.

## Base Configuration

The HTTP server is configured in the daemon configuration file:

```toml
[monitoring]
http_server_enabled = true
http_server_port = 8080
http_server_bind_address = "127.0.0.1"  # Use "0.0.0.0" for remote access
```

## Existing Endpoints

The daemon already provides these health monitoring endpoints:

- `GET /health` - Basic health status
- `GET /metrics` - Detailed metrics
- `GET /diagnostic` - Comprehensive diagnostics
- `GET /ready` - Readiness probe
- `GET /alive` - Liveness probe

## New Data Query Endpoints

### 1. Recent Readings

**Endpoint:** `GET /data/recent`

**Parameters:**
- `count` (optional, default=100): Number of recent readings to retrieve

**Response:**
```json
{
  "readings": [
    {
      "timestamp": "2024-01-01T12:00:00.000Z",
      "co2_ppm": 410.5,
      "temperature_c": 22.1,
      "humidity_percent": 45.2,
      "quality_flags": 7
    },
    ...
  ],
  "total_count": 100
}
```

**Error Response:**
```json
{
  "error": "Invalid count parameter",
  "details": "Count must be positive integer"
}
```

### 2. Time Range Query

**Endpoint:** `GET /data/range`

**Parameters:**
- `start` (required): Start timestamp in ISO 8601 format
- `end` (required): End timestamp in ISO 8601 format

**Example:** `/data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z`

**Response:**
```json
{
  "readings": [
    {
      "timestamp": "2024-01-01T12:00:00.000Z",
      "co2_ppm": 410.5,
      "temperature_c": 22.1,
      "humidity_percent": 45.2,
      "quality_flags": 7
    },
    ...
  ],
  "start_time": "2024-01-01T12:00:00.000Z",
  "end_time": "2024-01-01T13:00:00.000Z",
  "total_count": 3600
}
```

### 3. Aggregated Data

**Endpoint:** `GET /data/aggregates`

**Parameters:**
- `start` (required): Start timestamp in ISO 8601 format
- `end` (required): End timestamp in ISO 8601 format
- `interval` (optional, default="1H"): Aggregation interval (1H, 30T, 1D, etc.)

**Example:** `/data/aggregates?start=2024-01-01T00:00:00Z&end=2024-01-02T00:00:00Z&interval=1H`

**Response:**
```json
{
  "aggregates": [
    {
      "timestamp": "2024-01-01T12:00:00.000Z",
      "co2_ppm_mean": 410.5,
      "co2_ppm_min": 405.0,
      "co2_ppm_max": 415.0,
      "co2_ppm_count": 3600,
      "temperature_c_mean": 22.1,
      "temperature_c_min": 21.8,
      "temperature_c_max": 22.4,
      "temperature_c_count": 3600,
      "humidity_percent_mean": 45.2,
      "humidity_percent_min": 44.0,
      "humidity_percent_max": 46.5,
      "humidity_percent_count": 3600
    },
    ...
  ],
  "start_time": "2024-01-01T00:00:00.000Z",
  "end_time": "2024-01-02T00:00:00.000Z",
  "interval": "1H",
  "total_intervals": 24
}
```

### 4. Database Information

**Endpoint:** `GET /data/info`

**Response:**
```json
{
  "total_records": 1000000,
  "database_path": "/var/lib/sensor-daemon/data",
  "earliest_timestamp": "2024-01-01T00:00:00.000Z",
  "latest_timestamp": "2024-01-01T23:59:59.000Z",
  "database_size_bytes": 52428800,
  "implementation": "RocksDB via HTTP API"
}
```

## Implementation Notes

### 1. URL Parsing
The existing HTTP server uses simple string matching for endpoints. The new endpoints should follow the same pattern:

```cpp
if (request.find("GET /data/recent") != std::string::npos) {
    response = handle_recent_data_request(request);
} else if (request.find("GET /data/range") != std::string::npos) {
    response = handle_range_data_request(request);
}
// ... etc
```

### 2. Parameter Parsing
Extract URL parameters from the request string:

```cpp
std::string extract_parameter(const std::string& request, const std::string& param_name) {
    // Parse URL parameters from request string
    // Example: "GET /data/recent?count=100 HTTP/1.1"
}
```

### 3. JSON Response Generation
Create JSON responses using string building (or integrate a JSON library):

```cpp
std::string create_readings_json(const std::vector<SensorReading>& readings) {
    std::string json = "{\"readings\":[";
    for (size_t i = 0; i < readings.size(); ++i) {
        if (i > 0) json += ",";
        json += reading_to_json(readings[i]);
    }
    json += "],\"total_count\":" + std::to_string(readings.size()) + "}";
    return json;
}
```

### 4. Database Access
Reuse existing TimeSeriesStorage methods or create new query methods:

```cpp
std::vector<SensorReading> get_recent_readings(int count);
std::vector<SensorReading> get_readings_in_range(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end
);
```

### 5. Error Handling
Return appropriate HTTP status codes and error messages:

```cpp
std::string create_error_response(int status_code, const std::string& error_msg) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " Error\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Connection: close\r\n\r\n";
    response += "{\"error\":\"" + error_msg + "\"}";
    return response;
}
```

### 6. Security Considerations
- Validate all input parameters
- Limit query result sizes to prevent memory exhaustion
- Consider rate limiting for expensive queries
- Bind to localhost by default for security

### 7. Performance Considerations
- Cache recent readings for better performance
- Implement query timeouts
- Consider pagination for large result sets
- Use efficient RocksDB iterators for range queries

## Testing

Test the endpoints using curl:

```bash
# Test recent readings
curl "http://localhost:8080/data/recent?count=10"

# Test range query
curl "http://localhost:8080/data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z"

# Test aggregates
curl "http://localhost:8080/data/aggregates?start=2024-01-01T00:00:00Z&end=2024-01-02T00:00:00Z&interval=1H"

# Test database info
curl "http://localhost:8080/data/info"
```

## Migration Path

1. **Phase 1**: Implement basic endpoints in the existing HTTP server
2. **Phase 2**: Test with Python client
3. **Phase 3**: Add performance optimizations and caching
4. **Phase 4**: Add security features if needed for remote access

This approach eliminates the need for Python RocksDB bindings while providing a clean, maintainable interface for data access.