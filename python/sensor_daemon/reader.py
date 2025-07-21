"""
SensorDataReader class for querying sensor data from RocksDB.

This module provides direct read-only access to the RocksDB database
used by the sensor-daemon for storing time-series sensor data.
"""

import subprocess
from datetime import datetime
from typing import Dict, Any
import pandas as pd
import requests


class SensorDataReader:
    """
    Reader class for accessing sensor data via HTTP API.
    
    This class provides methods to query time-series sensor data including
    recent readings, time range queries, and statistical aggregations.
    """
    
    def __init__(self, api_url: str = "http://localhost:8080", timeout: int = 30):
        """
        Initialize the SensorDataReader.
        
        Args:
            api_url: Base URL of the sensor-daemon HTTP API
            timeout: Request timeout in seconds
            
        Raises:
            ConnectionError: If the API is not accessible
            RuntimeError: If the API is not responding correctly
        """
        self.api_url = api_url.rstrip('/')
        self.timeout = timeout
        self.session = requests.Session()
        
        # Test connection
        try:
            response = self.session.get(f"{self.api_url}/health", timeout=5)
            if response.status_code != 200:
                raise RuntimeError(f"API health check failed: {response.status_code}")
        except requests.exceptions.RequestException as e:
            raise ConnectionError(f"Cannot connect to sensor-daemon API at {api_url}: {e}")
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
    
    def close(self):
        """Close the HTTP session."""
        if self.session:
            self.session.close()
            self.session = None
    

    
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
            RuntimeError: If API request fails
        """
        if count <= 0:
            raise ValueError("Count must be positive")
        
        try:
            params = {'count': count}
            response = self.session.get(
                f"{self.api_url}/data/recent",
                params=params,
                timeout=self.timeout
            )
            response.raise_for_status()
            
            data = response.json()
            if 'error' in data:
                raise RuntimeError(f"API error: {data['error']}")
            
            # Convert to DataFrame
            readings = data.get('readings', [])
            if not readings:
                return pd.DataFrame(columns=['timestamp', 'co2_ppm', 'temperature_c', 'humidity_percent', 'quality_flags'])
            
            # Parse timestamps
            for reading in readings:
                reading['timestamp'] = pd.to_datetime(reading['timestamp'])
            
            return pd.DataFrame(readings)
            
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"Failed to retrieve recent readings: {e}")
        except (KeyError, ValueError) as e:
            raise RuntimeError(f"Invalid API response format: {e}")
    
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
            RuntimeError: If API request fails
        """
        if start >= end:
            raise ValueError("Start time must be before end time")
        
        try:
            params = {
                'start': start.isoformat(),
                'end': end.isoformat()
            }
            response = self.session.get(
                f"{self.api_url}/data/range",
                params=params,
                timeout=self.timeout
            )
            response.raise_for_status()
            
            data = response.json()
            if 'error' in data:
                raise RuntimeError(f"API error: {data['error']}")
            
            # Convert to DataFrame
            readings = data.get('readings', [])
            if not readings:
                return pd.DataFrame(columns=['timestamp', 'co2_ppm', 'temperature_c', 'humidity_percent', 'quality_flags'])
            
            # Parse timestamps
            for reading in readings:
                reading['timestamp'] = pd.to_datetime(reading['timestamp'])
            
            return pd.DataFrame(readings)
            
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"Failed to retrieve readings in range: {e}")
        except (KeyError, ValueError) as e:
            raise RuntimeError(f"Invalid API response format: {e}")
    
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
            RuntimeError: If API request fails
        """
        if start >= end:
            raise ValueError("Start time must be before end time")
        
        try:
            params = {
                'start': start.isoformat(),
                'end': end.isoformat(),
                'interval': interval
            }
            response = self.session.get(
                f"{self.api_url}/data/aggregates",
                params=params,
                timeout=self.timeout
            )
            response.raise_for_status()
            
            data = response.json()
            if 'error' in data:
                raise RuntimeError(f"API error: {data['error']}")
            
            # Convert to DataFrame
            aggregates = data.get('aggregates', [])
            if not aggregates:
                return pd.DataFrame()
            
            # Parse timestamps
            for aggregate in aggregates:
                aggregate['timestamp'] = pd.to_datetime(aggregate['timestamp'])
            
            return pd.DataFrame(aggregates)
            
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"Failed to compute aggregates: {e}")
        except (KeyError, ValueError) as e:
            raise RuntimeError(f"Invalid API response format: {e}")
    
    def is_daemon_running(self) -> bool:
        """
        Check if the sensor daemon is currently running.
        
        Returns:
            True if the daemon is running, False otherwise
        """
        try:
            response = self.session.get(f"{self.api_url}/alive", timeout=5)
            return response.status_code == 200
        except requests.exceptions.RequestException:
            return False
    
    def get_database_info(self) -> Dict[str, Any]:
        """
        Get information about the database.
        
        Returns:
            Dictionary with database statistics and information
        """
        try:
            response = self.session.get(f"{self.api_url}/data/info", timeout=self.timeout)
            response.raise_for_status()
            
            data = response.json()
            if 'error' in data:
                raise RuntimeError(f"API error: {data['error']}")
            
            # Parse timestamps if present
            if data.get('earliest_timestamp'):
                data['earliest_timestamp'] = pd.to_datetime(data['earliest_timestamp'])
            if data.get('latest_timestamp'):
                data['latest_timestamp'] = pd.to_datetime(data['latest_timestamp'])
            
            return data
            
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"Failed to get database info: {e}")
        except (KeyError, ValueError) as e:
            raise RuntimeError(f"Invalid API response format: {e}")