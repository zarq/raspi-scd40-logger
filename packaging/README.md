# Sensor Daemon Packaging

This directory contains all the files needed to create Debian packages for the sensor-daemon project.

## Package Structure

The project creates two Debian packages:

1. **sensor-daemon** - Main C++ daemon executable and systemd service
2. **python3-sensor-daemon** - Python interface for data access

## Files Overview

### Systemd Service
- `systemd/sensor-daemon.service` - Systemd service unit file with security restrictions

### Debian Package Configuration
- `debian/control` - Package metadata and dependencies
- `debian/rules` - Build rules for both C++ and Python components
- `debian/compat` - Debian compatibility level
- `debian/changelog` - Package version history
- `debian/copyright` - License information

### Installation Scripts
- `debian/sensor-daemon.preinst` - Pre-installation (create user/group)
- `debian/sensor-daemon.postinst` - Post-installation (setup directories, enable service)
- `debian/sensor-daemon.prerm` - Pre-removal (stop service)
- `debian/sensor-daemon.postrm` - Post-removal (cleanup, optional data removal)

### Configuration Files
- `debian/sensor-daemon.install` - File installation mapping
- `debian/sensor-daemon.logrotate` - Log rotation configuration
- `debian/sensor-daemon.maintscript` - Configuration file preservation
- `debian/sensor-daemon.8` - Manual page

### Build Scripts
- `build-package.sh` - Linux package build script
- `build-package.bat` - Windows reference script
- `test-package.sh` - Package testing script

## Building Packages

### Prerequisites

On Debian/Ubuntu systems, install build dependencies:

```bash
sudo apt update
sudo apt install -y \
    debhelper-compat \
    cmake \
    g++ \
    libsystemd-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    librocksdb-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libtoml11-dev \
    python3-dev \
    python3-setuptools \
    python3-wheel \
    dh-python
```

### Build Process

1. **Build packages:**
   ```bash
   ./packaging/build-package.sh
   ```

2. **Install packages:**
   ```bash
   sudo dpkg -i ../sensor-daemon_*.deb ../python3-sensor-daemon_*.deb
   sudo apt-get install -f  # Fix any dependency issues
   ```

3. **Test installation:**
   ```bash
   ./packaging/test-package.sh
   ```

## Package Features

### Security
- Dedicated system user (`sensor-daemon`)
- Systemd security restrictions
- Limited file system access
- Resource limits (memory, CPU)
- System call filtering

### File Layout
```
/usr/bin/sensor-daemon                    # Main executable
/etc/sensor-daemon/config.toml            # Configuration file
/var/lib/sensor-daemon/                   # Data storage
/var/log/sensor-daemon/                   # Log files
/usr/lib/systemd/system/sensor-daemon.service  # Service file
/usr/lib/python3/dist-packages/sensor_daemon/  # Python module
```

### Service Management
```bash
# Start/stop service
sudo systemctl start sensor-daemon
sudo systemctl stop sensor-daemon

# Enable/disable auto-start
sudo systemctl enable sensor-daemon
sudo systemctl disable sensor-daemon

# Check status and logs
sudo systemctl status sensor-daemon
sudo journalctl -u sensor-daemon -f
```

### Configuration
The default configuration file is created at `/etc/sensor-daemon/config.toml` during installation. Key settings include:

- Sampling interval (1-3600 seconds)
- Data retention period (1-365 days)
- I2C device path and sensor address
- Storage and logging options

### Python Interface
```python
from sensor_daemon import SensorDataReader

reader = SensorDataReader()
recent_data = reader.get_recent_readings(100)
print(recent_data.head())
```

## Package Lifecycle

### Installation
1. Create system user and group
2. Create necessary directories with proper permissions
3. Install configuration file with sensible defaults
4. Register and enable systemd service
5. Install Python module

### Upgrade
1. Preserve existing configuration and data
2. Update binaries and service files
3. Restart service if running
4. Update Python module

### Removal
1. Stop and disable service
2. Remove binaries and service files
3. Preserve configuration and data
4. Remove Python module

### Purge
1. Remove all configuration files
2. Optionally remove data directories (user prompted)
3. Remove system user and group
4. Clean up systemd configuration

## Testing

The `test-package.sh` script performs comprehensive testing:

1. **Fresh Installation** - Install packages and verify setup
2. **Service Management** - Test start/stop functionality
3. **Package Upgrade** - Verify configuration preservation
4. **Package Removal** - Test clean removal
5. **Package Purge** - Test complete cleanup

## Troubleshooting

### Common Issues

1. **Build Dependencies Missing**
   ```bash
   sudo apt-get build-dep sensor-daemon
   ```

2. **I2C Permissions**
   ```bash
   sudo usermod -a -G i2c sensor-daemon
   ```

3. **Service Won't Start**
   ```bash
   sudo journalctl -u sensor-daemon -n 50
   ```

4. **Python Module Import Error**
   ```bash
   python3 -c "import sys; print(sys.path)"
   dpkg -L python3-sensor-daemon
   ```

### Log Locations
- System logs: `journalctl -u sensor-daemon`
- Application logs: `/var/log/sensor-daemon/`
- Package logs: `/var/log/dpkg.log`

## Requirements Compliance

This packaging implementation satisfies all requirements from section 8:

- **8.1**: Provides .deb package for standard Debian package management
- **8.2**: Automatically creates directories and configuration files
- **8.3**: Registers daemon as systemd service
- **8.4**: Clean uninstall with optional data preservation
- **8.5**: Preserves configuration and data during upgrades
- **8.6**: Declares appropriate dependencies for automatic installation

The packaging also includes comprehensive security restrictions, proper user management, and extensive testing capabilities.