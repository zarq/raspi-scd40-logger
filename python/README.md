# Sensor Daemon Python Interface

This Python package provides a convenient interface for querying sensor data collected by the sensor-daemon C++ application. It connects to the daemon's HTTP API for efficient and reliable data retrieval.

## Features

- **HTTP API Access**: Connects to the sensor-daemon's built-in HTTP API (no direct database dependencies)
- **Pandas Integration**: Returns data as pandas DataFrames for easy analysis
- **Time-Series Queries**: Support for recent readings, time range queries, and statistical aggregations
- **Daemon Status Monitoring**: Check if the sensor daemon is currently running
- **Error Handling**: Comprehensive error handling for network and API issues
- **No RocksDB Dependencies**: Works with any system that has the sensor-daemon running

## Requirements

- The sensor-daemon must be running with HTTP API enabled
- No RocksDB Python bindings required
- Only standard Python libraries (requests, pandas)

## Installation

### Enable HTTP API in sensor-daemon

Before using this package, ensure the sensor-daemon is configured with HTTP API enabled:

```toml
# In /etc/sensor-daemon/sensor-daemon.toml
[monitoring]
http_server_enabled = true
http_server_port = 8080
http_server_bind_address = "127.0.0.1"
```

Restart the daemon after configuration changes:
```bash
sudo systemctl restart sensor-daemon
```

### From Source

```bash
cd python/
pip install -e .
```

### Dependencies

- Python 3.8+
- pandas >= 1.3.0
- requests >= 2.25.0

## Usage

### Basic Usage

```python
from sensor_daemon import SensorDataReader

# Initialize reader (default API URL: http://localhost:8080)
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

### Custom API URL

```python
# Specify custom API URL (e.g., remote daemon)
reader = SensorDataReader("http://192.168.1.100:8080")
```

## API Reference

### SensorDataReader

#### Constructor

```python
SensorDataReader(api_url="http://localhost:8080", timeout=30)
```

- `api_url`: Base URL of the sensor-daemon HTTP API
- `timeout`: Request timeout in seconds

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