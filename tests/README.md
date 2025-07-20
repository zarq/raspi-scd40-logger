# Sensor Daemon Comprehensive Testing Suite

This directory contains a comprehensive testing suite for the sensor daemon project, covering all aspects of functionality, performance, and reliability.

## Test Categories

### 1. Unit Tests
**Files:** `test_config_manager.cpp`, `test_sensor_data.cpp`, `test_time_series_storage.cpp`, `test_scd40_interface.cpp`, `test_logging_system.cpp`

**Purpose:** Test individual components in isolation with appropriate mocking of dependencies.

**Coverage:**
- Configuration management and validation
- Sensor data serialization/deserialization
- Time-series storage operations
- I2C sensor interface with mock hardware
- Logging system functionality

**Key Features:**
- Mock I2C operations for sensor testing
- Temporary file systems for storage testing
- Configuration validation edge cases
- Error handling scenarios

### 2. Integration Tests
**Files:** `test_integration.cpp`, `test_main_integration.cpp`, `test_daemon_core.cpp`

**Purpose:** Verify component interaction and end-to-end functionality.

**Coverage:**
- Component initialization and coordination
- Daemon lifecycle management
- Signal handling (SIGTERM, SIGINT)
- Configuration loading and validation
- Error recovery and restart logic
- Resource management

**Key Features:**
- Multi-process testing for signal handling
- Component interaction verification
- Error propagation testing
- Resource cleanup validation

### 3. Performance Tests
**File:** `test_performance.cpp`

**Purpose:** Verify performance requirements are met.

**Coverage:**
- Memory usage verification (<10MB requirement)
- Query response time testing (<10ms requirement)
- Storage write performance
- CPU usage monitoring
- Database size and compression effectiveness
- Concurrent access performance

**Key Features:**
- Resource usage monitoring
- Performance benchmarking
- Scalability testing
- Efficiency measurements

### 4. Stress Tests
**File:** `test_stress.cpp`

**Purpose:** Test system behavior under extreme conditions and extended operation.

**Coverage:**
- Extended operation testing (30+ seconds)
- High-frequency data generation
- Memory leak detection
- Error recovery under continuous stress
- Storage extreme load testing
- Daemon restart resilience

**Key Features:**
- Long-running stability tests
- Resource monitoring over time
- Concurrent stress testing
- Error injection and recovery
- Performance under load

### 5. Data Integrity Tests
**File:** `test_data_integrity.cpp`

**Purpose:** Ensure data accuracy and consistency throughout the system.

**Coverage:**
- Round-trip data integrity verification
- Missing value handling
- Timestamp precision and ordering
- Storage consistency across operations
- Concurrent access data integrity
- Extreme value handling
- Database restart integrity
- Serialization/deserialization accuracy

**Key Features:**
- Data pattern verification
- Concurrent write testing
- Database persistence validation
- Precision testing (microsecond timestamps)
- Edge case data handling

## Test Execution

### Prerequisites
```bash
# Build the project with tests enabled
cmake -DBUILD_TESTS=ON .
cmake --build . --target sensor_daemon_tests
```

### Running Tests

#### All Tests (Comprehensive Suite)
```bash
# Linux/Unix
./tests/run_comprehensive_tests.sh

# Windows
tests\run_comprehensive_tests.bat
```

#### Individual Test Categories
```bash
# Unit tests only
./tests/run_comprehensive_tests.sh unit

# Performance tests only
./tests/run_comprehensive_tests.sh performance

# Stress tests only
./tests/run_comprehensive_tests.sh stress

# Integration tests only
./tests/run_comprehensive_tests.sh integration

# Data integrity tests only
./tests/run_comprehensive_tests.sh integrity
```

#### Direct GTest Execution
```bash
# Run all tests
./sensor_daemon_tests

# Run specific test suite
./sensor_daemon_tests --gtest_filter="*PerformanceTest*"

# Run with verbose output
./sensor_daemon_tests --gtest_filter="*StressTest*" --gtest_verbose

# Generate XML output
./sensor_daemon_tests --gtest_output=xml:results.xml
```

## Test Configuration

### Mock Components
- **Mock I2C Device:** Simulates SCD40 sensor responses for automated testing
- **Temporary File Systems:** Isolated storage for each test
- **Mock System Calls:** Override system calls for controlled testing environments

### Test Data Patterns
- **Predictable Sequences:** For verification of data integrity
- **Random Data:** For stress testing and edge case coverage
- **Extreme Values:** Testing boundary conditions
- **Sparse Data:** Testing missing value handling

### Resource Monitoring
- Memory usage tracking (RSS monitoring)
- CPU usage measurement
- Database size monitoring
- Performance timing measurements

## Test Requirements Verification

### Functional Requirements
- ✅ Daemon runs continuously in background
- ✅ I2C sensor communication with error handling
- ✅ Time-series data storage with missing value support
- ✅ Configuration management and validation
- ✅ Structured logging with rotation
- ✅ Signal handling for graceful shutdown

### Performance Requirements
- ✅ Memory usage < 10MB (verified in performance tests)
- ✅ Query response time < 10ms (verified in performance tests)
- ✅ Efficient storage with compression
- ✅ Low CPU usage during idle periods

### Reliability Requirements
- ✅ Error recovery and retry logic
- ✅ Data integrity across restarts
- ✅ Graceful handling of hardware failures
- ✅ Resource cleanup on shutdown
- ✅ Concurrent access safety

## Test Reports

### Automated Reporting
The comprehensive test runner generates detailed reports including:
- Test category results (PASS/FAIL)
- Performance metrics
- Resource usage statistics
- System information
- Detailed logs for debugging

### Report Files
- `test_report.txt` - Summary report
- `test_results.log` - Detailed execution log
- `*_results.xml` - GTest XML output for CI integration
- `*_status.txt` - Individual category status files

## Continuous Integration

### CI Integration
The test suite is designed for CI/CD integration:
- XML output format for test result parsing
- Exit codes for build pipeline integration
- Categorized testing for parallel execution
- Resource usage reporting for performance monitoring

### Test Isolation
- Each test uses isolated temporary directories
- Mock components prevent external dependencies
- Cleanup procedures ensure no test interference
- Deterministic test execution order

## Troubleshooting

### Common Issues

#### Test Executable Not Found
```bash
# Ensure tests are built
cmake -DBUILD_TESTS=ON .
cmake --build . --target sensor_daemon_tests
```

#### Permission Errors
```bash
# Linux: Ensure test runner is executable
chmod +x tests/run_comprehensive_tests.sh

# Windows: Run from elevated command prompt if needed
```

#### Resource Constraints
- Stress tests may require significant system resources
- Performance tests are sensitive to system load
- Consider running on dedicated test systems for accurate results

#### Mock Hardware Dependencies
- I2C tests use mocked hardware interfaces
- Some integration tests may skip hardware-dependent operations
- This is expected behavior in test environments

### Debug Mode
```bash
# Run with debug output
./sensor_daemon_tests --gtest_filter="*YourTest*" --gtest_verbose

# Run single test for debugging
./sensor_daemon_tests --gtest_filter="*SpecificTest*"
```

## Contributing

### Adding New Tests
1. Choose appropriate test category (unit/integration/performance/stress/integrity)
2. Follow existing naming conventions
3. Include appropriate setup/teardown procedures
4. Add test to CMakeLists.txt
5. Update test runner scripts if needed

### Test Guidelines
- Use descriptive test names
- Include appropriate assertions
- Clean up resources in teardown
- Document complex test scenarios
- Consider both positive and negative test cases

### Performance Considerations
- Performance tests should be deterministic
- Use appropriate timeouts for stress tests
- Monitor resource usage in long-running tests
- Consider system load when setting performance thresholds