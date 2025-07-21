"""
Sensor Daemon Python Interface

This module provides a Python interface for querying sensor data stored by the
sensor-daemon C++ application. It directly accesses the RocksDB database in
read-only mode for efficient data retrieval.
"""

# Try to import the full-featured implementation first
try:
    from .reader import SensorDataReader
    FULL_IMPLEMENTATION_AVAILABLE = True
except ImportError as e:
    FULL_IMPLEMENTATION_AVAILABLE = False
    _import_error = e

# If the main implementation fails, provide the simple fallback
if not FULL_IMPLEMENTATION_AVAILABLE:
    try:
        from .reader_simple import SimpleSensorDataReader as SensorDataReader
        import warnings
        warnings.warn(
            "Using limited rocksdb-python implementation. "
            "Many features are not available. "
            "Install python-rocksdb for full functionality.",
            UserWarning
        )
    except ImportError:
        # Neither implementation is available
        def SensorDataReader(*args, **kwargs):
            raise ImportError(
                f"No RocksDB implementation available.\n"
                f"Original error: {_import_error}\n"
                f"Please install either:\n"
                f"1. python-rocksdb (recommended): pip install python-rocksdb\n"
                f"2. rocksdb-python (limited): pip install git+https://github.com/gau-nernst/rocksdb-python.git"
            )

__version__ = "1.0.0"
__all__ = ["SensorDataReader", "FULL_IMPLEMENTATION_AVAILABLE"]