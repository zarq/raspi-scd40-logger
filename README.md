# Sensor Data Daemon

A lightweight Linux daemon written in modern C++ that continuously reads sensor data from an SCD40 sensor over I2C and stores it in an efficient time-series format.

## Features

- Continuous SCD40 sensor data collection (CO2, temperature, humidity)
- Efficient time-series storage using RocksDB
- Minimal resource usage (<10MB RAM)
- Python interface for data analysis
- Systemd service integration
- Debian package distribution

## Build Requirements

### Dependencies

- CMake 3.16+
- C++20 compatible compiler (GCC 10+ or Clang 10+)
- pkg-conf
- librocksdb-dev
- libprotobuf-dev
- protobuf-compiler
- libspdlog-dev
- libtoml11-dev
- libsystemd-dev (optional, for systemd integration)

### Ubuntu/Debian Installation

```bash
sudo apt update
sudo apt install cmake g++ librocksdb-dev libprotobuf-dev protobuf-compiler \
                 libspdlog-dev libtoml11-dev libsystemd-dev
```

## Building

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Build Options

- `BUILD_TESTS=ON`: Enable unit tests (requires GTest)
- `CMAKE_BUILD_TYPE=Debug`: Build with debug symbols

Example:
```bash
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ..
```

## Installation

```bash
sudo make install
```

This installs:
- `/usr/bin/sensor-daemon` - Main executable
- `/etc/sensor-daemon/sensor-daemon.toml` - Configuration file

## Configuration

Edit `/etc/sensor-daemon/sensor-daemon.toml` to configure:
- Sampling intervals
- I2C device settings
- Data retention policies
- Storage locations

## Usage

### As a systemd service (recommended)
```bash
sudo systemctl enable sensor-daemon
sudo systemctl start sensor-daemon
sudo systemctl status sensor-daemon
```

### Manual execution
```bash
sensor-daemon --config /etc/sensor-daemon/sensor-daemon.toml
```

## Python Interface

```python
from sensor_daemon import SensorDataReader

# Read recent data
reader = SensorDataReader()
recent_data = reader.get_recent_readings(100)
print(recent_data.head())

# Query time range
from datetime import datetime, timedelta
end_time = datetime.now()
start_time = end_time - timedelta(hours=24)
daily_data = reader.get_readings_range(start_time, end_time)
```

## Project Structure

```
├── src/                 # Source files
├── include/             # Header files
├── proto/               # Protocol Buffers schemas
├── tests/               # Unit tests
├── config/              # Configuration files
├── build/               # Build directory (created during build)
└── CMakeLists.txt       # Build configuration
```

## Development Status

This project is currently under development. The initial project structure and build system have been set up. Implementation of core functionality is in progress.

## License

See LICENSE file for details.