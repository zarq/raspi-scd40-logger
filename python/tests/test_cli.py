"""
Unit tests for CLI module.
"""

import unittest
from unittest.mock import patch, MagicMock
import sys
from io import StringIO

# Mock dependencies
sys.modules['rocksdb'] = MagicMock()
sys.modules['sensor_daemon.sensor_data_pb2'] = MagicMock()

from sensor_daemon.cli import main


class TestCLI(unittest.TestCase):
    """Test cases for CLI functionality."""
    
    def setUp(self):
        """Set up test fixtures."""
        self.original_argv = sys.argv.copy()
    
    def tearDown(self):
        """Clean up test fixtures."""
        sys.argv = self.original_argv
    
    @patch('sensor_daemon.cli.SensorDataReader')
    def test_recent_command(self, mock_reader_class):
        """Test recent readings command."""
        # Mock reader instance
        mock_reader = MagicMock()
        mock_reader_class.return_value.__enter__.return_value = mock_reader
        
        # Mock data
        import pandas as pd
        mock_data = pd.DataFrame({
            'timestamp': ['2024-01-01 12:00:00'],
            'co2_ppm': [400.0],
            'temperature_c': [22.5],
            'humidity_percent': [45.0],
            'quality_flags': [7]
        })
        mock_reader.get_recent_readings.return_value = mock_data
        
        # Test command
        sys.argv = ['cli.py', 'recent', '--count', '5']
        
        with patch('sys.stdout', new_callable=StringIO) as mock_stdout:
            result = main()
        
        self.assertEqual(result, 0)
        mock_reader.get_recent_readings.assert_called_once_with(5)
        output = mock_stdout.getvalue()
        self.assertIn("Retrieved 1 recent readings", output)
    
    @patch('sensor_daemon.cli.SensorDataReader')
    def test_status_command_running(self, mock_reader_class):
        """Test status command when daemon is running."""
        mock_reader = MagicMock()
        mock_reader_class.return_value.__enter__.return_value = mock_reader
        mock_reader.is_daemon_running.return_value = True
        
        sys.argv = ['cli.py', 'status']
        
        with patch('sys.stdout', new_callable=StringIO) as mock_stdout:
            result = main()
        
        self.assertEqual(result, 0)
        output = mock_stdout.getvalue()
        self.assertIn("RUNNING", output)
    
    @patch('sensor_daemon.cli.SensorDataReader')
    def test_status_command_not_running(self, mock_reader_class):
        """Test status command when daemon is not running."""
        mock_reader = MagicMock()
        mock_reader_class.return_value.__enter__.return_value = mock_reader
        mock_reader.is_daemon_running.return_value = False
        
        sys.argv = ['cli.py', 'status']
        
        with patch('sys.stdout', new_callable=StringIO) as mock_stdout:
            result = main()
        
        self.assertEqual(result, 0)
        output = mock_stdout.getvalue()
        self.assertIn("NOT RUNNING", output)
    
    @patch('sensor_daemon.cli.SensorDataReader')
    def test_info_command(self, mock_reader_class):
        """Test database info command."""
        mock_reader = MagicMock()
        mock_reader_class.return_value.__enter__.return_value = mock_reader
        mock_reader.get_database_info.return_value = {
            'total_records': 1000,
            'database_path': '/test/path',
            'earliest_timestamp': '2024-01-01 00:00:00',
            'latest_timestamp': '2024-01-01 23:59:59'
        }
        
        sys.argv = ['cli.py', 'info']
        
        with patch('sys.stdout', new_callable=StringIO) as mock_stdout:
            result = main()
        
        self.assertEqual(result, 0)
        output = mock_stdout.getvalue()
        self.assertIn("Database Information", output)
        self.assertIn("total_records: 1000", output)
    
    @patch('sensor_daemon.cli.SensorDataReader')
    def test_error_handling(self, mock_reader_class):
        """Test error handling in CLI."""
        mock_reader_class.side_effect = Exception("Database error")
        
        sys.argv = ['cli.py', 'status']
        
        with patch('sys.stderr', new_callable=StringIO) as mock_stderr:
            result = main()
        
        self.assertEqual(result, 1)
        error_output = mock_stderr.getvalue()
        self.assertIn("Error: Database error", error_output)
    
    def test_no_command(self):
        """Test CLI with no command specified."""
        sys.argv = ['cli.py']
        
        with patch('sys.stdout', new_callable=StringIO):
            result = main()
        
        self.assertEqual(result, 1)


if __name__ == '__main__':
    unittest.main()