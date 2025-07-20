#!/usr/bin/env python3
"""
Advanced data analysis examples using sensor_daemon Python interface.
"""

import sys
import os
from datetime import datetime, timedelta
import pandas as pd

# Add parent directory to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from sensor_daemon import SensorDataReader


def analyze_daily_patterns(reader, days=7):
    """Analyze daily patterns in sensor data."""
    print(f"=== Daily Pattern Analysis (Last {days} days) ===")
    
    end_time = datetime.now()
    start_time = end_time - timedelta(days=days)
    
    try:
        # Get hourly aggregates
        hourly_data = reader.get_aggregates(start_time, end_time, interval='1H')
        
        if hourly_data.empty:
            print("No data available for analysis")
            return
        
        # Add hour of day column
        hourly_data['hour'] = pd.to_datetime(hourly_data['timestamp']).dt.hour
        
        # Group by hour to find daily patterns
        if 'co2_ppm_mean' in hourly_data.columns:
            hourly_patterns = hourly_data.groupby('hour')['co2_ppm_mean'].agg(['mean', 'std', 'min', 'max'])
            
            print("CO2 Patterns by Hour of Day:")
            print(hourly_patterns.round(2))
            
            # Find peak and low hours
            peak_hour = hourly_patterns['mean'].idxmax()
            low_hour = hourly_patterns['mean'].idxmin()
            
            print(f"\nPeak CO2 hour: {peak_hour}:00 ({hourly_patterns.loc[peak_hour, 'mean']:.1f} ppm)")
            print(f"Lowest CO2 hour: {low_hour}:00 ({hourly_patterns.loc[low_hour, 'mean']:.1f} ppm)")
        
        print()
        
    except Exception as e:
        print(f"Error in daily pattern analysis: {e}")


def analyze_correlations(reader, days=7):
    """Analyze correlations between sensor measurements."""
    print(f"=== Correlation Analysis (Last {days} days) ===")
    
    end_time = datetime.now()
    start_time = end_time - timedelta(days=days)
    
    try:
        # Get raw data for correlation analysis
        data = reader.get_readings_range(start_time, end_time)
        
        if data.empty:
            print("No data available for correlation analysis")
            return
        
        # Select numeric columns
        numeric_cols = ['co2_ppm', 'temperature_c', 'humidity_percent']
        available_cols = [col for col in numeric_cols if col in data.columns]
        
        if len(available_cols) < 2:
            print("Insufficient numeric data for correlation analysis")
            return
        
        # Calculate correlations
        correlations = data[available_cols].corr()
        
        print("Sensor Measurement Correlations:")
        print(correlations.round(3))
        
        # Highlight strong correlations
        print("\nStrong correlations (|r| > 0.5):")
        for i in range(len(correlations.columns)):
            for j in range(i+1, len(correlations.columns)):
                corr_val = correlations.iloc[i, j]
                if abs(corr_val) > 0.5:
                    col1 = correlations.columns[i]
                    col2 = correlations.columns[j]
                    print(f"  {col1} vs {col2}: {corr_val:.3f}")
        
        print()
        
    except Exception as e:
        print(f"Error in correlation analysis: {e}")


def analyze_data_quality(reader, days=1):
    """Analyze data quality and missing values."""
    print(f"=== Data Quality Analysis (Last {days} day(s)) ===")
    
    end_time = datetime.now()
    start_time = end_time - timedelta(days=days)
    
    try:
        data = reader.get_readings_range(start_time, end_time)
        
        if data.empty:
            print("No data available for quality analysis")
            return
        
        print(f"Total readings: {len(data)}")
        
        # Check for missing values
        print("\nMissing value analysis:")
        for col in ['co2_ppm', 'temperature_c', 'humidity_percent']:
            if col in data.columns:
                missing_count = data[col].isna().sum()
                missing_pct = (missing_count / len(data)) * 100
                print(f"  {col}: {missing_count} missing ({missing_pct:.1f}%)")
        
        # Analyze quality flags if available
        if 'quality_flags' in data.columns:
            print("\nQuality flag analysis:")
            flag_counts = data['quality_flags'].value_counts().sort_index()
            for flag, count in flag_counts.items():
                pct = (count / len(data)) * 100
                print(f"  Flag {flag}: {count} readings ({pct:.1f}%)")
        
        # Check for data gaps
        if len(data) > 1:
            data_sorted = data.sort_values('timestamp')
            time_diffs = data_sorted['timestamp'].diff().dt.total_seconds()
            
            # Assuming normal sampling interval (get median)
            normal_interval = time_diffs.median()
            large_gaps = time_diffs[time_diffs > normal_interval * 2]
            
            print(f"\nTiming analysis:")
            print(f"  Normal sampling interval: {normal_interval:.0f} seconds")
            print(f"  Large gaps detected: {len(large_gaps)}")
            if len(large_gaps) > 0:
                print(f"  Largest gap: {large_gaps.max():.0f} seconds")
        
        print()
        
    except Exception as e:
        print(f"Error in data quality analysis: {e}")


def generate_summary_report(reader, days=7):
    """Generate a comprehensive summary report."""
    print(f"=== Summary Report (Last {days} days) ===")
    
    end_time = datetime.now()
    start_time = end_time - timedelta(days=days)
    
    try:
        # Get daily aggregates for summary
        daily_data = reader.get_aggregates(start_time, end_time, interval='1D')
        
        if daily_data.empty:
            print("No data available for summary report")
            return
        
        print(f"Reporting period: {start_time.strftime('%Y-%m-%d')} to {end_time.strftime('%Y-%m-%d')}")
        print(f"Days with data: {len(daily_data)}")
        
        # Summary statistics for each sensor
        sensors = [
            ('co2_ppm_mean', 'CO2', 'ppm'),
            ('temperature_c_mean', 'Temperature', '°C'),
            ('humidity_percent_mean', 'Humidity', '%')
        ]
        
        for col, name, unit in sensors:
            if col in daily_data.columns:
                values = daily_data[col].dropna()
                if not values.empty:
                    print(f"\n{name} Summary:")
                    print(f"  Average: {values.mean():.1f} {unit}")
                    print(f"  Range: {values.min():.1f} - {values.max():.1f} {unit}")
                    print(f"  Std Dev: {values.std():.1f} {unit}")
        
        print()
        
    except Exception as e:
        print(f"Error generating summary report: {e}")


def main():
    """Run comprehensive data analysis."""
    db_path = "/var/lib/sensor-daemon/data"
    
    # Check if database exists
    if not os.path.exists(db_path):
        print(f"Database not found at {db_path}")
        print("This example requires actual sensor data to analyze.")
        return 1
    
    try:
        with SensorDataReader(db_path) as reader:
            print("=== Sensor Data Analysis Suite ===\n")
            
            # Check if daemon is running
            if reader.is_daemon_running():
                print("✓ Sensor daemon is running")
            else:
                print("⚠ Sensor daemon is not running (analyzing existing data)")
            print()
            
            # Run various analyses
            generate_summary_report(reader, days=7)
            analyze_daily_patterns(reader, days=7)
            analyze_correlations(reader, days=7)
            analyze_data_quality(reader, days=1)
            
            print("=== Analysis completed ===")
            
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())