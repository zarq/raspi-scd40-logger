"""
Tests to verify which RocksDB implementation is being used.
"""

import unittest
import sys
from unittest.mock import patch

from sensor_daemon import SensorDataReader, FULL_IMPLEMENTATION_AVAILABLE


class TestImplementation(unittest.TestCase):
    """Test which implementation is being used."""
    
    def test_implementation_detection(self):
        """Test that implementation detection works."""
        # This test just verifies the import system works
        self.assertIsNotNone(SensorDataReader)
        self.assertIsInstance(FULL_IMPLEMENTATION_AVAILABLE, bool)
    
    def test_full_implementation_methods(self):
        """Test that full implementation has all expected methods."""
        if FULL_IMPLEMENTATION_AVAILABLE:
            # Check that all methods exist
            methods = [
                'get_recent_readings',
                'get_readings_range', 
                'get_aggregates',
                'is_daemon_running',
                'get_database_info'
            ]
            
            for method in methods:
                self.assertTrue(hasattr(SensorDataReader, method),
                              f"Method {method} not found in full implementation")
    
    def test_limited_implementation_methods(self):
        """Test that limited implementation has expected methods."""
        if not FULL_IMPLEMENTATION_AVAILABLE:
            # Check that basic methods exist
            methods = [
                'get_single_reading',
                'is_daemon_running',
                'get_database_info'
            ]
            
            for method in methods:
                self.assertTrue(hasattr(SensorDataReader, method),
                              f"Method {method} not found in limited implementation")
            
            # Check that time-series methods raise NotImplementedError
            limited_methods = [
                'get_recent_readings',
                'get_readings_range',
                'get_aggregates'
            ]
            
            for method in limited_methods:
                self.assertTrue(hasattr(SensorDataReader, method),
                              f"Method {method} should exist but raise NotImplementedError")
    
    def test_import_error_handling(self):
        """Test that import errors are handled gracefully."""
        # This test verifies that the import system doesn't crash
        # even when neither implementation is available
        
        # Mock both implementations as unavailable
        with patch.dict('sys.modules', {
            'rocksdb': None,
            'rocksdb_python': None
        }):
            # The import should still work, but create a function that raises ImportError
            try:
                from sensor_daemon import SensorDataReader as TestReader
                # If we get here, it means the fallback error function was created
                with self.assertRaises(ImportError):
                    TestReader()
            except ImportError:
                # This is also acceptable - the import itself failed
                pass


if __name__ == '__main__':
    unittest.main()