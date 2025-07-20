#!/bin/bash
set -e

# Build script for sensor-daemon Debian package
# This script builds both the C++ daemon and Python interface packages

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Building sensor-daemon Debian packages..."
echo "Project root: $PROJECT_ROOT"

# Check for required tools
command -v dpkg-buildpackage >/dev/null 2>&1 || {
    echo "Error: dpkg-buildpackage not found. Install with: apt install dpkg-dev"
    exit 1
}

command -v cmake >/dev/null 2>&1 || {
    echo "Error: cmake not found. Install with: apt install cmake"
    exit 1
}

# Clean previous builds
echo "Cleaning previous builds..."
cd "$PROJECT_ROOT"
rm -rf build/
rm -rf debian/
mkdir -p build

# Copy debian packaging files
echo "Setting up debian packaging..."
cp -r packaging/debian .

# Make maintainer scripts executable
chmod +x debian/sensor-daemon.preinst
chmod +x debian/sensor-daemon.postinst
chmod +x debian/sensor-daemon.prerm
chmod +x debian/sensor-daemon.postrm

# Build source package
echo "Building source package..."
dpkg-buildpackage -S -us -uc

# Build binary packages
echo "Building binary packages..."
dpkg-buildpackage -b -us -uc

echo "Package build completed successfully!"
echo ""
echo "Generated packages:"
ls -la ../*.deb 2>/dev/null || echo "No .deb files found in parent directory"

echo ""
echo "To install the packages:"
echo "  sudo dpkg -i ../sensor-daemon_*.deb ../python3-sensor-daemon_*.deb"
echo "  sudo apt-get install -f  # Fix any dependency issues"
echo ""
echo "To test the installation:"
echo "  sudo systemctl start sensor-daemon"
echo "  sudo systemctl status sensor-daemon"
echo "  python3 -c 'import sensor_daemon; print(\"Python interface available\")'"