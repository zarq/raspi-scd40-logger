#!/bin/bash
set -e

# Test script for sensor-daemon package installation, upgrade, and removal
# This script tests the complete package lifecycle

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Testing sensor-daemon package lifecycle..."

# Function to check if service exists and get status
check_service_status() {
    if systemctl list-unit-files | grep -q sensor-daemon.service; then
        echo "Service status: $(systemctl is-active sensor-daemon.service 2>/dev/null || echo 'inactive')"
        echo "Service enabled: $(systemctl is-enabled sensor-daemon.service 2>/dev/null || echo 'disabled')"
    else
        echo "Service not found"
    fi
}

# Function to check if user exists
check_user() {
    if getent passwd sensor-daemon >/dev/null 2>&1; then
        echo "User sensor-daemon exists"
        echo "Groups: $(groups sensor-daemon)"
    else
        echo "User sensor-daemon does not exist"
    fi
}

# Function to check directories
check_directories() {
    echo "Directory status:"
    for dir in /etc/sensor-daemon /var/lib/sensor-daemon /var/log/sensor-daemon; do
        if [ -d "$dir" ]; then
            echo "  $dir: exists ($(ls -ld "$dir" | awk '{print $1, $3, $4}'))"
        else
            echo "  $dir: missing"
        fi
    done
}

# Function to check Python package
check_python_package() {
    if python3 -c "import sensor_daemon" 2>/dev/null; then
        echo "Python package: available"
        python3 -c "import sensor_daemon; print(f'Version: {getattr(sensor_daemon, \"__version__\", \"unknown\")}')"
    else
        echo "Python package: not available"
    fi
}

# Test 1: Fresh installation
echo "=== Test 1: Fresh Installation ==="
echo "Installing packages..."

# Find the package files
DEB_FILES=(../sensor-daemon_*.deb ../python3-sensor-daemon_*.deb)
if [ ! -f "${DEB_FILES[0]}" ]; then
    echo "Error: Package files not found. Run build-package.sh first."
    exit 1
fi

sudo dpkg -i "${DEB_FILES[@]}" || {
    echo "Installing dependencies..."
    sudo apt-get install -f -y
}

echo "Post-installation checks:"
check_service_status
check_user
check_directories
check_python_package

# Test configuration file
if [ -f /etc/sensor-daemon/config.toml ]; then
    echo "Configuration file exists and is readable"
    # Test configuration parsing (if daemon supports --test-config)
    if command -v sensor-daemon >/dev/null 2>&1; then
        if sensor-daemon --test-config 2>/dev/null; then
            echo "Configuration file is valid"
        else
            echo "Configuration file validation failed or not supported"
        fi
    fi
else
    echo "ERROR: Configuration file missing"
fi

# Test 2: Service start/stop
echo ""
echo "=== Test 2: Service Management ==="
echo "Starting service..."
sudo systemctl start sensor-daemon.service || echo "Service start failed (expected if no I2C hardware)"

sleep 2
check_service_status

echo "Stopping service..."
sudo systemctl stop sensor-daemon.service
check_service_status

# Test 3: Package upgrade simulation
echo ""
echo "=== Test 3: Package Upgrade Simulation ==="
echo "Reinstalling packages (simulates upgrade)..."

# Backup configuration
sudo cp /etc/sensor-daemon/config.toml /tmp/config.toml.backup

# Modify configuration to test preservation
echo "# Test modification" | sudo tee -a /etc/sensor-daemon/config.toml

sudo dpkg -i "${DEB_FILES[@]}"

# Check if modification was preserved
if grep -q "Test modification" /etc/sensor-daemon/config.toml; then
    echo "Configuration preserved during upgrade: PASS"
else
    echo "Configuration NOT preserved during upgrade: FAIL"
fi

# Restore original configuration
sudo cp /tmp/config.toml.backup /etc/sensor-daemon/config.toml

# Test 4: Package removal (without purge)
echo ""
echo "=== Test 4: Package Removal (Preserve Data) ==="
echo "Removing packages (keeping configuration and data)..."

sudo apt-get remove -y sensor-daemon python3-sensor-daemon

echo "Post-removal checks:"
check_service_status
check_user
check_directories
check_python_package

# Test 5: Package purge
echo ""
echo "=== Test 5: Package Purge ==="
echo "Purging packages (remove all data)..."

# Simulate user input for data removal prompt
echo "y" | sudo apt-get purge -y sensor-daemon python3-sensor-daemon

echo "Post-purge checks:"
check_service_status
check_user
check_directories
check_python_package

echo ""
echo "=== Package Testing Complete ==="
echo "All tests completed. Review output above for any failures."