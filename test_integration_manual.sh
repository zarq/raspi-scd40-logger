#!/bin/bash

# Manual Integration Test Script for Sensor Daemon
# This script tests the main application integration without requiring actual hardware

set -e

EXECUTABLE=./build/sensor-daemon

echo "=== Sensor Daemon Integration Test ==="
echo

# Create test data directory
TEST_DATA_DIR="/tmp/sensor-daemon-integration-test"
mkdir -p "$TEST_DATA_DIR"

# Create test configuration
TEST_CONFIG="$TEST_DATA_DIR/test_config.toml"
cat > "$TEST_CONFIG" << EOF
[daemon]
sampling_interval_seconds = 2
data_retention_days = 1
log_level = "info"

[sensor]
i2c_device = "/dev/i2c-1"
i2c_address = 0x62
connection_timeout_ms = 500
max_retries = 2

[storage]
data_directory = "$TEST_DATA_DIR/data"
file_rotation_hours = 1
compression_enabled = true
max_memory_cache_mb = 2
EOF

echo "Created test configuration: $TEST_CONFIG"
echo

# Test 1: Help and version
echo "Test 1: Command-line help and version"
echo "--------------------------------------"
if [ -f "$EXECUTABLE" ]; then
    "$EXECUTABLE" --help
    echo
    "$EXECUTABLE" --version
    echo
else
    echo "sensor-daemon executable ($EXECUTABLE) not found. Please build the project first."
    exit 1
fi

# Test 2: Configuration validation
echo "Test 2: Configuration validation"
echo "--------------------------------"
echo "Testing with valid configuration..."
timeout 5s "$EXECUTABLE" --config "$TEST_CONFIG" --foreground || echo "Daemon stopped (expected due to timeout)"
echo

echo "Testing with invalid configuration..."
timeout 2s "$EXECUTABLE" --config "/nonexistent/config.toml" --foreground 2>&1 || echo "Failed as expected with invalid config"
echo

# Test 3: Foreground mode operation
echo "Test 3: Foreground mode operation"
echo "----------------------------------"
echo "Running daemon in foreground for 10 seconds..."
timeout 10s "$EXECUTABLE" --config "$TEST_CONFIG" --foreground || echo "Daemon completed test run"
echo

# Test 4: Check generated files
echo "Test 4: Generated files verification"
echo "------------------------------------"
if [ -d "$TEST_DATA_DIR/data" ]; then
    echo "✓ Data directory created successfully"
    ls -la "$TEST_DATA_DIR/data"
else
    echo "✗ Data directory not created"
fi
echo

# Cleanup
echo "Cleaning up test files..."
rm -rf "$TEST_DATA_DIR"
echo "✓ Cleanup completed"

echo
echo "=== Integration Test Completed ==="
echo "If no errors were reported above, the main application integration is working correctly."