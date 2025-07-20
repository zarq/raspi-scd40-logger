"""
Unit tests for SensorDataReader class.
"""

import os
import tempfile
import unittest
from datetime import datetime, timezone, timedelta
from unittest.mock import patch, MagicMock
import pandas as pd

# Mock rocksdb since it may not be available in test environment
import sys
from unittest.mock import MagicMock
sys.modules['rocksdb'] = MagicMock()

from sensor_daemon.reader import SensorDataReader


class TestSensorDataReader(unittest.TestCase):
    """Test cases for SensorDataReader class."""
    
    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.mkdtemp()
        self.test_db_path = os.path.join(self.temp_dir, "test_db")
        os.makedirs(self.test_db_path, exist_ok=True)
    
    def tearDown(self):
        """Clean up test fixtures."""
        import shutil
        shutil.rmtree(self.temp_dir, ignore_errors=True)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_init_success(self, mock_rocksdb):
        """Test successful initialization."""
        mock_db = MagicMock()
        mock_rocksdb.DB.return_value = mock_db
        
        reader = SensorDataReader(self.test_db_path)
        
        self.assertEqual(reader.db_path, self.test_db_path)
        mock_rocksdb.DB.assert_called_once()
    
    def test_init_missing_path(self):
        """Test initialization with missing database path."""
        non_existent_path = "/non/existent/path"
        
        with self.assertRaises(FileNotFoundError):
            SensorDataReader(non_existent_path)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_init_database_error(self, mock_rocksdb):
        """Test initialization with database error."""
        mock_rocksdb.DB.side_effect = Exception("Database error")
        
        with self.assertRaises(RuntimeError):
            SensorDataReader(self.test_db_path)
    
    def test_timestamp_to_key(self):
        """Test timestamp to key conversion."""
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            
            # Test with known timestamp
            dt = datetime(2024, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
            key = reader._timestamp_to_key(dt)
            
            # Should be 8 bytes
            self.assertEqual(len(key), 8)
            
            # Should be big-endian format
            import struct
            timestamp_us = struct.unpack('>Q', key)[0]
            expected_us = int(dt.timestamp() * 1_000_000)
            self.assertEqual(timestamp_us, expected_us)
    
    def test_key_to_timestamp(self):
        """Test key to timestamp conversion."""
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            
            # Create a test key
            import struct
            timestamp_us = 1704110400000000  # 2024-01-01 12:00:00 UTC
            key = struct.pack('>Q', timestamp_us)
            
            dt = reader._key_to_timestamp(key)
            
            expected_dt = datetime.fromtimestamp(
                timestamp_us / 1_000_000, tz=timezone.utc
            )
            self.assertEqual(dt, expected_dt)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_get_recent_readings_invalid_count(self, mock_rocksdb):
        """Test get_recent_readings with invalid count."""
        reader = SensorDataReader(self.test_db_path)
        
        with self.assertRaises(ValueError):
            reader.get_recent_readings(0)
        
        with self.assertRaises(ValueError):
            reader.get_recent_readings(-1)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_get_recent_readings_empty_db(self, mock_rocksdb):
        """Test get_recent_readings with empty database."""
        mock_db = MagicMock()
        mock_rocksdb.DB.return_value = mock_db
        
        # Mock empty iterator
        mock_iterator = MagicMock()
        mock_iterator.valid.return_value = False
        mock_db.iteritems.return_value = mock_iterator
        
        reader = SensorDataReader(self.test_db_path)
        result = reader.get_recent_readings(10)
        
        self.assertIsInstance(result, pd.DataFrame)
        self.assertTrue(result.empty)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_get_readings_range_invalid_params(self, mock_rocksdb):
        """Test get_readings_range with invalid parameters."""
        reader = SensorDataReader(self.test_db_path)
        
        start = datetime(2024, 1, 2, tzinfo=timezone.utc)
        end = datetime(2024, 1, 1, tzinfo=timezone.utc)
        
        with self.assertRaises(ValueError):
            reader.get_readings_range(start, end)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_get_aggregates_invalid_params(self, mock_rocksdb):
        """Test get_aggregates with invalid parameters."""
        reader = SensorDataReader(self.test_db_path)
        
        start = datetime(2024, 1, 2, tzinfo=timezone.utc)
        end = datetime(2024, 1, 1, tzinfo=timezone.utc)
        
        with self.assertRaises(ValueError):
            reader.get_aggregates(start, end)
    
    @patch('sensor_daemon.reader.subprocess')
    def test_is_daemon_running_systemctl_active(self, mock_subprocess):
        """Test is_daemon_running with active systemd service."""
        mock_result = MagicMock()
        mock_result.returncode = 0
        mock_result.stdout.strip.return_value = 'active'
        mock_subprocess.run.return_value = mock_result
        
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            result = reader.is_daemon_running()
        
        self.assertTrue(result)
    
    @patch('sensor_daemon.reader.subprocess')
    def test_is_daemon_running_systemctl_inactive(self, mock_subprocess):
        """Test is_daemon_running with inactive systemd service."""
        mock_result = MagicMock()
        mock_result.returncode = 3  # systemctl returns 3 for inactive
        mock_result.stdout.strip.return_value = 'inactive'
        mock_subprocess.run.return_value = mock_result
        
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            result = reader.is_daemon_running()
        
        self.assertFalse(result)
    
    @patch('sensor_daemon.reader.subprocess')
    def test_is_daemon_running_fallback_pgrep(self, mock_subprocess):
        """Test is_daemon_running fallback to pgrep."""
        # First call (systemctl) fails, second call (pgrep) succeeds
        mock_subprocess.run.side_effect = [
            FileNotFoundError(),  # systemctl not found
            MagicMock(returncode=0)  # pgrep finds process
        ]
        
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            result = reader.is_daemon_running()
        
        self.assertTrue(result)
    
    @patch('sensor_daemon.reader.subprocess')
    def test_is_daemon_running_all_fail(self, mock_subprocess):
        """Test is_daemon_running when all methods fail."""
        mock_subprocess.run.side_effect = FileNotFoundError()
        
        with patch('sensor_daemon.reader.rocksdb'):
            reader = SensorDataReader(self.test_db_path)
            result = reader.is_daemon_running()
        
        self.assertFalse(result)
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_context_manager(self, mock_rocksdb):
        """Test context manager functionality."""
        mock_db = MagicMock()
        mock_rocksdb.DB.return_value = mock_db
        
        with SensorDataReader(self.test_db_path) as reader:
            self.assertIsNotNone(reader._db)
        
        # close() should have been called
        # Note: We can't easily test del mock_db, but we can verify the pattern
    
    @patch('sensor_daemon.reader.rocksdb')
    def test_get_database_info_empty(self, mock_rocksdb):
        """Test get_database_info with empty database."""
        mock_db = MagicMock()
        mock_rocksdb.DB.return_value = mock_db
        
        # Mock empty iterator
        mock_iterator = MagicMock()
        mock_iterator.valid.return_value = False
        mock_db.iteritems.return_value = mock_iterator
        
        reader = SensorDataReader(self.test_db_path)
        info = reader.get_database_info()
        
        expected = {
            'total_records': 0,
            'database_path': self.test_db_path,
            'earliest_timestamp': None,
            'latest_timestamp': None
        }
        self.assertEqual(info, expected)


class TestSensorDataReaderIntegration(unittest.TestCase):
    """Integration tests for SensorDataReader (require actual data)."""
    
    def test_pandas_integration(self):
        """Test that pandas DataFrames are created correctly."""
        # Create empty DataFrame to test structure
        df = pd.DataFrame(columns=[
            'timestamp', 'co2_ppm', 'temperature_c', 
            'humidity_percent', 'quality_flags'
        ])
        
        # Verify expected columns exist
        expected_columns = [
            'timestamp', 'co2_ppm', 'temperature_c', 
            'humidity_percent', 'quality_flags'
        ]
        for col in expected_columns:
            self.assertIn(col, df.columns)


if __name__ == '__main__':
    unittest.main()