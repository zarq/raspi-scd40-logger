"""
Command-line interface for sensor-daemon Python package.
"""

import argparse
import sys
from datetime import datetime, timedelta
from sensor_daemon import SensorDataReader, FULL_IMPLEMENTATION_AVAILABLE


def main():
    """Main CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Query sensor data from sensor-daemon database"
    )
    
    parser.add_argument(
        "--db-path", 
        default="/var/lib/sensor-daemon/data",
        help="Path to RocksDB database directory"
    )
    
    subparsers = parser.add_subparsers(dest="command", help="Available commands")
    
    # Recent readings command
    recent_parser = subparsers.add_parser("recent", help="Get recent readings")
    recent_parser.add_argument(
        "--count", "-n", 
        type=int, 
        default=10,
        help="Number of recent readings to retrieve"
    )
    
    # Range query command
    range_parser = subparsers.add_parser("range", help="Get readings in time range")
    range_parser.add_argument(
        "--start", 
        required=True,
        help="Start time (ISO format: 2024-01-01T12:00:00)"
    )
    range_parser.add_argument(
        "--end", 
        required=True,
        help="End time (ISO format: 2024-01-01T13:00:00)"
    )
    
    # Aggregates command
    agg_parser = subparsers.add_parser("aggregate", help="Get aggregated statistics")
    agg_parser.add_argument(
        "--start", 
        required=True,
        help="Start time (ISO format: 2024-01-01T12:00:00)"
    )
    agg_parser.add_argument(
        "--end", 
        required=True,
        help="End time (ISO format: 2024-01-01T13:00:00)"
    )
    agg_parser.add_argument(
        "--interval", 
        default="1H",
        help="Aggregation interval (e.g., 1H, 30T, 1D)"
    )
    
    # Status command
    subparsers.add_parser("status", help="Check daemon status")
    
    # Info command
    subparsers.add_parser("info", help="Get database information")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    # Check implementation capabilities
    if not FULL_IMPLEMENTATION_AVAILABLE and args.command in ["recent", "range", "aggregate"]:
        print(f"Error: Command '{args.command}' requires full python-rocksdb implementation", file=sys.stderr)
        print("Current implementation has limited functionality.", file=sys.stderr)
        print("Install python-rocksdb for full features.", file=sys.stderr)
        return 1
    
    try:
        with SensorDataReader(args.db_path) as reader:
            if args.command == "recent":
                data = reader.get_recent_readings(args.count)
                print(f"Retrieved {len(data)} recent readings:")
                print(data.to_string(index=False))
                
            elif args.command == "range":
                start = datetime.fromisoformat(args.start)
                end = datetime.fromisoformat(args.end)
                data = reader.get_readings_range(start, end)
                print(f"Retrieved {len(data)} readings from {start} to {end}:")
                print(data.to_string(index=False))
                
            elif args.command == "aggregate":
                start = datetime.fromisoformat(args.start)
                end = datetime.fromisoformat(args.end)
                data = reader.get_aggregates(start, end, args.interval)
                print(f"Aggregated data from {start} to {end} (interval: {args.interval}):")
                print(data.to_string(index=False))
                
            elif args.command == "status":
                is_running = reader.is_daemon_running()
                status = "RUNNING" if is_running else "NOT RUNNING"
                print(f"Sensor daemon status: {status}")
                
            elif args.command == "info":
                info = reader.get_database_info()
                print("Database Information:")
                for key, value in info.items():
                    print(f"  {key}: {value}")
                
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())