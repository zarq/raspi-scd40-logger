# Sensor Daemon Python Interface

This Python package provides a convenient interface for querying sensor data collected by the sensor-daemon C++ application. It directly accesses the RocksDB database in read-only mode for efficient data retrieval.

## Features

- **Direct Database Access**: Reads directly from RocksDB without requiring the daemon to be running
- **Pandas Integration**: Returns data as pandas DataFrames for easy analysis
- **Time-Series Queries**: Support for recent readings, time range queries, and statistical aggregations
- **Daemon Status Monitoring**: Check if the sensor daemon is currently running
- **Error Handling**: Comprehensive error handling for database and system issues
- **Multiple RocksDB Backends**: Supports both `python-rocksdb` (full features) and `rocksdb-python` (limited fallback)

## Important Notes

This package requires a RocksDB Python binding. Two options are supported:

1. **python-rocksdb** (Recommended): Full functionality including efficient time-series queries
2. **rocksdb-python** (Limited fallback): Basic functionality only, no time-series queries

The package will automatically detect which implementation is available and use the appropriate one. Run the RocksDB checker to see what's available on your system.

## Installation

### Check RocksDB Availability

Before installing, check which RocksDB Python bindings are available:

```bash
cd python/
python -m sensor_daemon.check_rocksdb
```

This will tell you which RocksDB implementation is available and provide installation guidance.

### From Source

```bash
cd python/
pip install -e .
```

### Dependencies

- Python 3.8+
- pandas >= 1.3.0
- python-rocksdb >= 0.8.0 (requires RocksDB C++ library)
- protobuf >= 3.19.0

### Installing RocksDB Dependencies

The `python-rocksdb` package requires the RocksDB C++ library to be installed on your system.

#### Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install librocksdb-dev python3-dev
pip install python-rocksdb
```

#### CentOS/RHEL/Fedora:
```bash
# For newer versions with dnf:
sudo dnf install rocksdb-devel python3-devel
# For older versions with yum:
sudo yum install rocksdb-devel python3-devel
pip install python-rocksdb
```

#### macOS:
```bash
brew install rocksdb
pip install python-rocksdb
```

#### Alternative Installation Methods:
If the above doesn't work, try:
```bash
# Using conda (often more reliable):
conda install python-rocksdb -c conda-forge

# Or install from system packages:
# Ubuntu/Debian:
sudo apt-get install python3-rocksdb

# If compilation fails, you may need additional build tools:
sudo apt-get install build-essential cmake
```

## Usage

### Basic Usage

```python
from sensor_daemon import SensorDataReader

# Initialize reader (default path: /var/lib/sensor-daemon/data)
reader = SensorDataReader()

# Get the last 100 readings
recent_data = reader.get_recent_readings(100)
print(recent_data.head())

# Get readings from the last hour
from datetime import datetime, timedelta
end_time = datetime.now()
start_time = end_time - timedelta(hours=1)
hourly_data = reader.get_readings_range(start_time, end_time)

# Compute hourly averages for the last day
daily_start = end_time - timedelta(days=1)
hourly_averages = reader.get_aggregates(daily_start, end_time, interval='1H')

# Check if daemon is running
if reader.is_daemon_running():
    print("Sensor daemon is active")
else:
    print("Sensor daemon is not running")
```

### Context Manager

```python
# Use as context manager for automatic cleanup
with SensorDataReader() as reader:
    data = reader.get_recent_readings(50)
    print(f"Retrieved {len(data)} readings")
```

### Custom Database Path

```python
# Specify custom database path
reader = SensorDataReader("/custom/path/to/database")
```

## API Reference

### SensorDataReader

#### Constructor

```python
SensorDataReader(db_path="/var/lib/sensor-daemon/data")
```

- `db_path`: Path to the RocksDB database directory

#### Methods

##### get_recent_readings(count=100)

Get the most recent N sensor readings.

**Parameters:**
- `count` (int): Number of recent readings to retrieve

**Returns:**
- `pandas.DataFrame`: DataFrame with columns: timestamp, co2_ppm, temperature_c, humidity_percent, quality_flags

##### get_readings_range(start, end)

Get sensor readings within a time range.

**Parameters:**
- `start` (datetime): Start time (inclusive)
- `end` (datetime): End time (inclusive)

**Returns:**
- `pandas.DataFrame`: DataFrame with readings in the specified time range

##### get_aggregates(start, end, interval="1H")

Compute statistical aggregates over time intervals.

**Parameters:**
- `start` (datetime): Start time (inclusive)
- `end` (datetime): End time (inclusive)
- `interval` (str): Pandas time interval string (e.g., '1H', '30T', '1D')

**Returns:**
- `pandas.DataFrame`: DataFrame with aggregated statistics (mean, min, max, count)

##### is_daemon_running()

Check if the sensor daemon is currently running.

**Returns:**
- `bool`: True if daemon is running, False otherwise

##### get_database_info()

Get information about the database.

**Returns:**
- `dict`: Database statistics and information

## Data Format

The returned DataFrames contain the following columns:

- `timestamp`: UTC timestamp of the reading
- `co2_ppm`: CO2 concentration in parts per million (float, nullable)
- `temperature_c`: Temperature in degrees Celsius (float, nullable)
- `humidity_percent`: Relative humidity as percentage 0-100 (float, nullable)
- `quality_flags`: Data quality flags (int)

Missing sensor values are represented as `None`/`NaN` in the DataFrame.

## Error Handling

The package includes comprehensive error handling:

- `FileNotFoundError`: Database path doesn't exist
- `RuntimeError`: Database access errors
- `ValueError`: Invalid parameters (negative counts, invalid time ranges)

## Examples

### Analyzing Recent Data

```python
import matplotlib.pyplot as plt
from sensor_daemon import SensorDataReader

with SensorDataReader() as reader:
    # Get last 24 hours of data
    from datetime import datetime, timedelta
    end_time = datetime.now()
    start_time = end_time - timedelta(hours=24)
    
    data = reader.get_readings_range(start_time, end_time)
    
    # Plot CO2 levels
    plt.figure(figsize=(12, 6))
    plt.plot(data['timestamp'], data['co2_ppm'])
    plt.title('CO2 Levels - Last 24 Hours')
    plt.xlabel('Time')
    plt.ylabel('CO2 (ppm)')
    plt.xticks(rotation=45)
    plt.tight_layout()
    plt.show()
```

### Computing Daily Statistics

```python
from sensor_daemon import SensorDataReader
from datetime import datetime, timedelta

with SensorDataReader() as reader:
    # Get last week's data aggregated by day
    end_time = datetime.now()
    start_time = end_time - timedelta(days=7)
    
    daily_stats = reader.get_aggregates(start_time, end_time, interval='1D')
    
    print("Daily CO2 Statistics:")
    print(daily_stats[['timestamp', 'co2_ppm_mean', 'co2_ppm_min', 'co2_ppm_max']])
```

## Development

### Running Tests

```bash
cd python/
python -m pytest tests/ -v
```

### Code Formatting

```bash
black sensor_daemon/ tests/
flake8 sensor_daemon/ tests/
```

## License

This project is licensed under the MIT License.