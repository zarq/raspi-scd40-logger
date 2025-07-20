"""
Sensor Daemon Python Interface

This module provides a Python interface for querying sensor data stored by the
sensor-daemon C++ application. It directly accesses the RocksDB database in
read-only mode for efficient data retrieval.
"""

from .reader import SensorDataReader

__version__ = "1.0.0"
__all__ = ["SensorDataReader"]