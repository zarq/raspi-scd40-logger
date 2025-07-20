#!/usr/bin/env python3
"""
Script to generate Python protobuf bindings from the sensor_data.proto file.
"""

import os
import subprocess
import sys

def generate_protobuf():
    """Generate Python protobuf bindings."""
    proto_dir = "../proto"
    output_dir = "sensor_daemon"
    proto_file = "sensor_data.proto"
    
    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)
    
    # Generate Python bindings
    cmd = [
        "protoc",
        f"--proto_path={proto_dir}",
        f"--python_out={output_dir}",
        os.path.join(proto_dir, proto_file)
    ]
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(f"Successfully generated Python protobuf bindings in {output_dir}/")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error generating protobuf bindings: {e}")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("Error: protoc not found. Please install Protocol Buffers compiler.")
        return False

if __name__ == "__main__":
    if generate_protobuf():
        sys.exit(0)
    else:
        sys.exit(1)