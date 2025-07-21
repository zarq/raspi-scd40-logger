#!/usr/bin/env python3
"""
Basic usage examples for sensor_daemon Python interface.
"""

import sys
import os
from datetime import datetime, timedelta

# Add parent directory to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from sensor_daemon import SensorDataReader


def main():
    """Demonstrate basic usage of SensorDataReader."""
    
    # Use default API URL or specify custom URL
    api_url = "http://localhost:8080"
    
    try:
        # Initialize reader
        with SensorDataReader(api_url) as reader:
            print("=== Sensor Daemon Python Interface Demo ===\n")
            
            # Check daemon status
            print("1. Checking daemon status...")
            is_running = reader.is_daemon_running()
            status = "RUNNING" if is_running else "NOT RUNNING"
            print(f"   Daemon status: {status}\n")
            
            # Get database info
            print("2. Database information...")
            try:
                info = reader.get_database_info()
                print(f"   Total records: {info.get('total_records', 'Unknown')}")
                print(f"   Implementation: {info.get('implementation', 'HTTP API')}")
                if info.get('earliest_timestamp'):
                    print(f"   Earliest reading: {info['earliest_timestamp']}")
                    print(f"   Latest reading: {info['latest_timestamp']}")
                print()
            except Exception as e:
                print(f"   Error getting database info: {e}\n")
            
            # Get recent readings
            print("3. Getting recent readings...")
            try:
                recent_data = reader.get_recent_readings(10)
                if not recent_data.empty:
                    print(f"   Retrieved {len(recent_data)} recent readings:")
                    print(recent_data.to_string(index=False))
                else:
                    print("   No recent readings found")
                print()
            except Exception as e:
                print(f"   Error getting recent readings: {e}\n")
            
            # Get readings from last hour
            print("4. Getting readings from last hour...")
            try:
                end_time = datetime.now()
                start_time = end_time - timedelta(hours=1)
                hourly_data = reader.get_readings_range(start_time, end_time)
                
                if not hourly_data.empty:
                    print(f"   Retrieved {len(hourly_data)} readings from last hour:")
                    print(hourly_data.head().to_string(index=False))
                    if len(hourly_data) > 5:
                        print(f"   ... and {len(hourly_data) - 5} more")
                else:
                    print("   No readings found in the last hour")
                print()
            except Exception as e:
                print(f"   Error getting hourly readings: {e}\n")
            
            # Get aggregated data
            print("5. Getting hourly aggregates for last 24 hours...")
            try:
                end_time = datetime.now()
                start_time = end_time - timedelta(hours=24)
                aggregates = reader.get_aggregates(start_time, end_time, interval='1H')
                
                if not aggregates.empty:
                    print(f"   Retrieved {len(aggregates)} hourly aggregates:")
                    # Show only relevant columns
                    cols_to_show = ['timestamp', 'co2_ppm_mean', 'temperature_c_mean', 'humidity_percent_mean']
                    available_cols = [col for col in cols_to_show if col in aggregates.columns]
                    if available_cols:
                        print(aggregates[available_cols].head().to_string(index=False))
                    else:
                        print(aggregates.head().to_string(index=False))
                else:
                    print("   No aggregate data available")
                print()
            except Exception as e:
                print(f"   Error getting aggregates: {e}\n")
            
            print("=== Demo completed successfully ===")
            
    except ConnectionError:
        print(f"Error: Cannot connect to sensor-daemon API at {api_url}")
        print("Make sure the sensor-daemon is running with HTTP API enabled.")
        print("Check configuration: http_server_enabled = true")
        return 1
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0


def demonstrate_with_mock_data():
    """Demonstrate the interface structure with mock data."""
    import pandas as pd
    
    print("=== Mock Data Demo ===\n")
    
    # Create sample data structure
    sample_data = pd.DataFrame({
        'timestamp': pd.date_range('2024-01-01 12:00:00', periods=5, freq='30T'),
        'co2_ppm': [410.5, 415.2, 408.7, 412.1, 409.8],
        'temperature_c': [22.1, 22.3, 22.0, 22.2, 22.1],
        'humidity_percent': [45.2, 46.1, 44.8, 45.5, 45.0],
        'quality_flags': [7, 7, 7, 7, 7]  # All sensors valid
    })
    
    print("Sample sensor data structure:")
    print(sample_data.to_string(index=False))
    print()
    
    print("Data types:")
    print(sample_data.dtypes)
    print()
    
    print("Basic statistics:")
    print(sample_data.describe())
    print()
    
    print("=== Mock Demo completed ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())