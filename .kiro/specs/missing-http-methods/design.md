# Design Document

## Overview

This design addresses the missing HTTP API method implementations that are causing linker errors. The missing methods are primarily in the JsonResponseBuilder, HttpParameterParser, SecurityManager, HttpErrorHandler, and IntervalParser classes.

## Architecture

The missing methods fall into several categories:

1. **JSON Response Builder Methods**: Methods for creating HTTP headers and timestamps
2. **HTTP Parameter Parser Methods**: Methods for extracting HTTP method and path
3. **Security Manager Methods**: Methods for request validation and monitoring
4. **HTTP Error Handler Methods**: Methods for creating specific error responses
5. **Interval Parser Methods**: Methods for getting supported interval formats

## Components and Interfaces

### 1. JsonResponseBuilder Missing Methods

```cpp
class JsonResponseBuilder {
public:
    // Missing methods that need implementation
    static std::string get_current_timestamp();
    static std::string create_http_header(int status_code, size_t content_length = 0);
};
```

### 2. HttpParameterParser Missing Methods

```cpp
class HttpParameterParser {
public:
    // Missing method that needs implementation
    static std::pair<std::string, std::string> extract_method_and_path(const std::string& request);
};
```

### 3. SecurityManager Missing Methods

```cpp
class SecurityManager {
public:
    // Missing methods that need implementation
    ValidationResult validate_request(const std::string& request, const std::string& client_ip);
    std::chrono::steady_clock::time_point start_request_monitoring(const std::string& endpoint);
    void end_request_monitoring(const std::string& endpoint,
                              std::chrono::steady_clock::time_point start_time,
                              size_t result_count = 0);
};
```

### 4. HttpErrorHandler Missing Methods

```cpp
class HttpErrorHandler {
public:
    // Missing methods that need implementation
    static std::string create_rate_limit_error(int retry_after_seconds = 60);
    static std::string create_internal_error(const std::string& operation = "",
                                           const std::string& error_id = "");
    static std::string create_parameter_error(const std::string& parameter_name,
                                            const std::string& provided_value = "",
                                            const std::string& expected_format = "");
    static std::string create_method_not_allowed_error(const std::string& method,
                                                     const std::vector<std::string>& allowed_methods = {"GET"});
};
```

### 5. IntervalParser Missing Methods

```cpp
class IntervalParser {
public:
    // Missing method that needs implementation
    static std::vector<std::string> get_supported_formats();
};
```

## Data Models

### Error Response Format
All error methods should return consistent JSON error responses with proper HTTP headers.

### Security Validation
Security validation should check for malicious patterns and rate limiting.

### Performance Monitoring
Request monitoring should track timing and result counts for performance analysis.

## Error Handling

All missing methods should implement proper error handling and return appropriate HTTP status codes and JSON responses.

## Implementation Strategy

1. Implement missing JsonResponseBuilder methods for HTTP headers and timestamps
2. Implement missing HttpParameterParser methods for request parsing
3. Implement missing SecurityManager methods for validation and monitoring
4. Implement missing HttpErrorHandler methods for consistent error responses
5. Implement missing IntervalParser methods for format information
6. Ensure all methods integrate properly with existing code
7. Test that linker errors are resolved