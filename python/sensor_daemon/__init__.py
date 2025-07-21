"""
Sensor Daemon Python Interface

This module provides a Python interface for querying sensor data from the
sensor-daemon C++ application via its HTTP API. No direct database access
or RocksDB dependencies are required.
"""

from .reader import SensorDataReader

__version__ = "1.0.0"
__all__ = ["SensorDataReader"]