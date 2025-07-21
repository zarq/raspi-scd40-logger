"""
SensorDataReader class for querying sensor data from RocksDB.

This module provides direct read-only access to the RocksDB database
used by the sensor-daemon for storing time-series sensor data.
"""

import os
import struct
import subprocess
from datetime import datetime, timezone
from typing import Optional, List, Dict, Any
import pandas as pd

try:
    import rocksdb
except ImportError:
    raise ImportError(
        "python-rocksdb is required but failed to import. This may be due to compilation issues.\n"
        "Try one of these installation methods:\n"
        "1. pip install python-rocksdb\n"
        "2. conda install python-rocksdb -c conda-forge\n"
        "3. For Ubuntu/Debian: sudo apt-get install python3-rocksdb\n"
        "4. Build from source: https://github.com/twmht/python-rocksdb\n"
        "\nNote: python-rocksdb requires RocksDB C++ library to be installed.\n"
        "On Ubuntu/Debian: sudo apt-get install librocksdb-dev\n"
        "On macOS: brew install rocksdb"
    )

try:
    from . import sensor_data_pb2
except ImportError:
    raise ImportError(
        "Protobuf bindings not found. Please ensure sensor_data_pb2.py is generated "
        "from the proto file."
    )


class SensorDataReader:
    """
    Reader class for accessing sensor data stored in RocksDB.
    
    This class provides methods to query time-series sensor data including
    recent readings, time range queries, and statistical aggregations.
    """
    
    def __init__(self, db_path: str = "/var/lib/sensor-daemon/data"):
        """
        Initialize the SensorDataReader.
        
        Args:
            db_path: Path to the RocksDB database directory
            
        Raises:
            FileNotFoundError: If the database path doesn't exist
            RuntimeError: If the database cannot be opened
        """
        self.db_path = db_path
        self._db = None
        
        if not os.path.exists(db_path):
            raise FileNotFoundError(f"Database path does not exist: {db_path}")
        
        try:
            # Open database in read-only mode
            opts = rocksdb.Options()
            opts.create_if_missing = False
            self._db = rocksdb.DB(db_path, opts, read_only=True)
        except Exception as e:
            raise RuntimeError(f"Failed to open database: {e}")
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
    
    def close(self):
        """Close the database connection."""
        if self._db:
            del self._db
            self._db = None
    
    def _timestamp_to_key(self, timestamp: datetime) -> bytes:
        """
        Convert datetime to RocksDB key format.
        
        Args:
            timestamp: Datetime object to convert
            
        Returns:
            8-byte big-endian timestamp key
        """
        # Convert to microseconds since Unix epoch
        timestamp_us = int(timestamp.timestamp() * 1_000_000)
        # Pack as big-endian 64-bit unsigned integer for proper ordering
        return struct.pack('>Q', timestamp_us)
    
    def _key_to_timestamp(self, key: bytes) -> datetime:
        """
        Convert RocksDB key to datetime.
        
        Args:
            key: 8-byte big-endian timestamp key
            
        Returns:
            Datetime object in UTC
        """
        timestamp_us = struct.unpack('>Q', key)[0]
        return datetime.fromtimestamp(timestamp_us / 1_000_000, tz=timezone.utc)
    
    def _parse_sensor_reading(self, key: bytes, value: bytes) -> Dict[str, Any]:
        """
        Parse a sensor reading from RocksDB key-value pair.
        
        Args:
            key: RocksDB key (timestamp)
            value: Protobuf-encoded sensor reading
            
        Returns:
            Dictionary with parsed sensor data
        """
        # Parse protobuf message
        reading = sensor_data_pb2.SensorReading()
        reading.ParseFromString(value)
        
        # Extract timestamp from key for consistency
        timestamp = self._key_to_timestamp(key)
        
        # Build result dictionary
        result = {
            'timestamp': timestamp,
            'co2_ppm': reading.co2_ppm if reading.HasField('co2_ppm') else None,
            'temperature_c': reading.temperature_c if reading.HasField('temperature_c') else None,
            'humidity_percent': reading.humidity_percent if reading.HasField('humidity_percent') else None,
            'quality_flags': reading.quality_flags
        }
        
        return result
    
    def get_recent_readings(self, count: int = 100) -> pd.DataFrame:
        """
        Get the most recent N sensor readings.
        
        Args:
            count: Number of recent readings to retrieve (default: 100)
            
        Returns:
            pandas DataFrame with columns: timestamp, co2_ppm, temperature_c, 
            humidity_percent, quality_flags
            
        Raises:
            ValueError: If count is not positive
            RuntimeError: If database access fails
        """
        if count <= 0:
            raise ValueError("Count must be positive")
        
        if not self._db:
            raise RuntimeError("Database is not open")
        
        try:
            readings = []
            # Iterate in reverse order to get most recent first
            it = self._db.iteritems()
            it.seek_to_last()
            
            collected = 0
            while it.valid() and collected < count:
                key, value = it.item()
                try:
                    reading = self._parse_sensor_reading(key, value)
                    readings.append(reading)
                    collected += 1
                except Exception as e:
                    # Skip corrupted entries but log the issue
                    print(f"Warning: Skipping corrupted entry: {e}")
                
                it.prev()
            
            # Reverse to get chronological order (oldest first)
            readings.reverse()
            
            return pd.DataFrame(readings)
            
        except Exception as e:
            raise RuntimeError(f"Failed to retrieve recent readings: {e}")
    
    def get_readings_range(self, start: datetime, end: datetime) -> pd.DataFrame:
        """
        Get sensor readings within a time range.
        
        Args:
            start: Start datetime (inclusive)
            end: End datetime (inclusive)
            
        Returns:
            pandas DataFrame with readings in the specified time range
            
        Raises:
            ValueError: If start is after end
            RuntimeError: If database access fails
        """
        if start >= end:
            raise ValueError("Start time must be before end time")
        
        if not self._db:
            raise RuntimeError("Database is not open")
        
        try:
            readings = []
            start_key = self._timestamp_to_key(start)
            end_key = self._timestamp_to_key(end)
            
            # Iterate through the range
            it = self._db.iteritems()
            it.seek(start_key)
            
            while it.valid():
                key, value = it.item()
                
                # Check if we've passed the end time
                if key > end_key:
                    break
                
                try:
                    reading = self._parse_sensor_reading(key, value)
                    readings.append(reading)
                except Exception as e:
                    # Skip corrupted entries but log the issue
                    print(f"Warning: Skipping corrupted entry: {e}")
                
                it.next()
            
            return pd.DataFrame(readings)
            
        except Exception as e:
            raise RuntimeError(f"Failed to retrieve readings in range: {e}")
    
    def get_aggregates(self, start: datetime, end: datetime, 
                      interval: str = "1H") -> pd.DataFrame:
        """
        Compute statistical aggregates for sensor data over time intervals.
        
        Args:
            start: Start datetime (inclusive)
            end: End datetime (inclusive)
            interval: Pandas time interval string (e.g., '1H', '30T', '1D')
            
        Returns:
            pandas DataFrame with aggregated statistics (mean, min, max, count)
            for each time interval
            
        Raises:
            ValueError: If parameters are invalid
            RuntimeError: If database access fails
        """
        if start >= end:
            raise ValueError("Start time must be before end time")
        
        # Get raw data for the time range
        df = self.get_readings_range(start, end)
        
        if df.empty:
            return pd.DataFrame()
        
        try:
            # Set timestamp as index for resampling
            df.set_index('timestamp', inplace=True)
            
            # Define aggregation functions for each column
            agg_funcs = {
                'co2_ppm': ['mean', 'min', 'max', 'count'],
                'temperature_c': ['mean', 'min', 'max', 'count'],
                'humidity_percent': ['mean', 'min', 'max', 'count']
            }
            
            # Resample and aggregate
            aggregated = df.resample(interval).agg(agg_funcs)
            
            # Flatten column names
            aggregated.columns = [f"{col[0]}_{col[1]}" for col in aggregated.columns]
            
            # Reset index to make timestamp a column again
            aggregated.reset_index(inplace=True)
            
            # Remove rows with no data
            aggregated = aggregated.dropna(how='all')
            
            return aggregated
            
        except Exception as e:
            raise RuntimeError(f"Failed to compute aggregates: {e}")
    
    def is_daemon_running(self) -> bool:
        """
        Check if the sensor daemon is currently running.
        
        Returns:
            True if the daemon is running, False otherwise
        """
        try:
            # Check systemd service status
            result = subprocess.run(
                ['systemctl', 'is-active', 'sensor-daemon'],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0 and result.stdout.strip() == 'active'
        except (subprocess.TimeoutExpired, FileNotFoundError, subprocess.SubprocessError):
            # Fallback: check if process exists
            try:
                result = subprocess.run(
                    ['pgrep', '-f', 'sensor-daemon'],
                    capture_output=True,
                    timeout=5
                )
                return result.returncode == 0
            except (subprocess.TimeoutExpired, FileNotFoundError, subprocess.SubprocessError):
                return False
    
    def get_database_info(self) -> Dict[str, Any]:
        """
        Get information about the database.
        
        Returns:
            Dictionary with database statistics and information
        """
        if not self._db:
            raise RuntimeError("Database is not open")
        
        try:
            # Get approximate number of keys
            it = self._db.iteritems()
            it.seek_to_first()
            
            if not it.valid():
                return {
                    'total_records': 0,
                    'database_path': self.db_path,
                    'earliest_timestamp': None,
                    'latest_timestamp': None
                }
            
            # Get first timestamp
            first_key, _ = it.item()
            earliest_timestamp = self._key_to_timestamp(first_key)
            
            # Get last timestamp
            it.seek_to_last()
            last_key, _ = it.item()
            latest_timestamp = self._key_to_timestamp(last_key)
            
            # Approximate count (this is expensive for large databases)
            # For production, consider maintaining a separate counter
            count = 0
            it.seek_to_first()
            while it.valid() and count < 10000:  # Limit for performance
                count += 1
                it.next()
            
            approximate_count = count if count < 10000 else f"{count}+"
            
            return {
                'total_records': approximate_count,
                'database_path': self.db_path,
                'earliest_timestamp': earliest_timestamp,
                'latest_timestamp': latest_timestamp
            }
            
        except Exception as e:
            raise RuntimeError(f"Failed to get database info: {e}")