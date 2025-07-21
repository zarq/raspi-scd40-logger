"""
Alternative SensorDataReader implementation using rocksdb-python.

This is a fallback implementation for systems where python-rocksdb
cannot be compiled. It's less efficient than the main implementation
as it lacks iterator support.

WARNING: This implementation is significantly less efficient for
time-series queries and should only be used if python-rocksdb
cannot be installed.
"""

import os
import struct
import subprocess
from datetime import datetime, timezone
from typing import Optional, List, Dict, Any
import pandas as pd

try:
    from rocksdb_python import Options, PyDB, ReadOptions, WriteOptions
    ROCKSDB_PYTHON_AVAILABLE = True
except ImportError:
    ROCKSDB_PYTHON_AVAILABLE = False

try:
    from . import sensor_data_pb2
except ImportError:
    raise ImportError(
        "Protobuf bindings not found. Please ensure sensor_data_pb2.py is generated "
        "from the proto file."
    )


class SimpleSensorDataReader:
    """
    Simple reader class using rocksdb-python (less efficient fallback).
    
    This implementation lacks iterator support and is much less efficient
    for time-series queries. Use the main SensorDataReader when possible.
    """
    
    def __init__(self, db_path: str = "/var/lib/sensor-daemon/data"):
        """
        Initialize the SimpleSensorDataReader.
        
        Args:
            db_path: Path to the RocksDB database directory
            
        Raises:
            ImportError: If rocksdb-python is not available
            FileNotFoundError: If the database path doesn't exist
            RuntimeError: If the database cannot be opened
        """
        if not ROCKSDB_PYTHON_AVAILABLE:
            raise ImportError(
                "rocksdb-python is not available. Install with:\n"
                "pip install git+https://github.com/gau-nernst/rocksdb-python.git"
            )
        
        self.db_path = db_path
        self._db = None
        
        if not os.path.exists(db_path):
            raise FileNotFoundError(f"Database path does not exist: {db_path}")
        
        try:
            # Open database in read-only mode
            opts = Options()
            opts.create_if_missing = False
            # Note: rocksdb-python doesn't support read_only parameter
            self._db = PyDB(opts, db_path)
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
        
        WARNING: This implementation is very inefficient as it cannot
        iterate backwards through the database. It's provided only as
        a fallback when python-rocksdb is not available.
        
        Args:
            count: Number of recent readings to retrieve (default: 100)
            
        Returns:
            pandas DataFrame with columns: timestamp, co2_ppm, temperature_c, 
            humidity_percent, quality_flags
            
        Raises:
            ValueError: If count is not positive
            RuntimeError: If database access fails
            NotImplementedError: This method is not efficiently implementable
        """
        raise NotImplementedError(
            "get_recent_readings() is not efficiently implementable with rocksdb-python "
            "due to lack of iterator support. Please use python-rocksdb instead."
        )
    
    def get_readings_range(self, start: datetime, end: datetime) -> pd.DataFrame:
        """
        Get sensor readings within a time range.
        
        WARNING: This implementation is very inefficient as it cannot
        iterate through key ranges. It's provided only as a fallback.
        
        Args:
            start: Start datetime (inclusive)
            end: End datetime (inclusive)
            
        Returns:
            pandas DataFrame with readings in the specified time range
            
        Raises:
            ValueError: If start is after end
            RuntimeError: If database access fails
            NotImplementedError: This method is not efficiently implementable
        """
        raise NotImplementedError(
            "get_readings_range() is not efficiently implementable with rocksdb-python "
            "due to lack of iterator support. Please use python-rocksdb instead."
        )
    
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
            NotImplementedError: This method requires get_readings_range
        """
        raise NotImplementedError(
            "get_aggregates() requires get_readings_range() which is not "
            "efficiently implementable with rocksdb-python. Please use python-rocksdb instead."
        )
    
    def get_single_reading(self, timestamp: datetime) -> Optional[Dict[str, Any]]:
        """
        Get a single reading by exact timestamp.
        
        This is the only efficient operation possible with rocksdb-python.
        
        Args:
            timestamp: Exact timestamp to look up
            
        Returns:
            Dictionary with sensor data or None if not found
        """
        if not self._db:
            raise RuntimeError("Database is not open")
        
        try:
            key = self._timestamp_to_key(timestamp)
            read_opts = ReadOptions()
            value = self._db.Get(read_opts, key)
            
            return self._parse_sensor_reading(key, value)
            
        except RuntimeError as e:
            # RocksDB throws RuntimeError for key not found
            if "NotFound" in str(e):
                return None
            raise RuntimeError(f"Failed to retrieve reading: {e}")
    
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
            Dictionary with basic database information
        """
        if not self._db:
            raise RuntimeError("Database is not open")
        
        return {
            'database_path': self.db_path,
            'implementation': 'rocksdb-python (limited functionality)',
            'note': 'This implementation lacks iterator support and cannot efficiently query time ranges'
        }