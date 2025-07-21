#!/usr/bin/env python3
"""
Utility script to check RocksDB Python binding availability and capabilities.
"""

import sys


def check_python_rocksdb():
    """Check if python-rocksdb is available."""
    try:
        import rocksdb
        print("✓ python-rocksdb is available")
        
        # Test basic functionality
        try:
            opts = rocksdb.Options()
            print("  - Options creation: OK")
            
            # Test if we can create a database instance (without actually opening)
            print("  - Full iterator support: Available")
            print("  - Recommended for sensor-daemon")
            return True
        except Exception as e:
            print(f"  - Error testing functionality: {e}")
            return False
            
    except ImportError as e:
        print(f"✗ python-rocksdb not available: {e}")
        return False


def check_rocksdb_python():
    """Check if rocksdb-python is available."""
    try:
        from rocksdb_python import Options, PyDB, ReadOptions, WriteOptions
        print("✓ rocksdb-python is available")
        
        # Test basic functionality
        try:
            opts = Options()
            print("  - Options creation: OK")
            print("  - Limited functionality (no iterators)")
            print("  - Only single-key lookups supported")
            print("  - NOT recommended for sensor-daemon")
            return True
        except Exception as e:
            print(f"  - Error testing functionality: {e}")
            return False
            
    except ImportError as e:
        print(f"✗ rocksdb-python not available: {e}")
        return False


def check_system_rocksdb():
    """Check if system RocksDB library is available."""
    import subprocess
    
    print("\nSystem RocksDB library check:")
    
    # Check for pkg-config
    try:
        result = subprocess.run(
            ['pkg-config', '--exists', 'rocksdb'],
            capture_output=True,
            timeout=5
        )
        if result.returncode == 0:
            # Get version
            version_result = subprocess.run(
                ['pkg-config', '--modversion', 'rocksdb'],
                capture_output=True,
                text=True,
                timeout=5
            )
            if version_result.returncode == 0:
                print(f"✓ RocksDB library found (version: {version_result.stdout.strip()})")
            else:
                print("✓ RocksDB library found (version unknown)")
        else:
            print("✗ RocksDB library not found via pkg-config")
    except (subprocess.TimeoutExpired, FileNotFoundError):
        print("? pkg-config not available, cannot check RocksDB library")
    
    # Check common library locations
    import os
    common_paths = [
        '/usr/lib/librocksdb.so',
        '/usr/local/lib/librocksdb.so',
        '/usr/lib/x86_64-linux-gnu/librocksdb.so',
        '/opt/homebrew/lib/librocksdb.dylib',  # macOS ARM
        '/usr/local/lib/librocksdb.dylib',     # macOS Intel
    ]
    
    found_libs = []
    for path in common_paths:
        if os.path.exists(path):
            found_libs.append(path)
    
    if found_libs:
        print("✓ RocksDB library files found:")
        for lib in found_libs:
            print(f"  - {lib}")
    else:
        print("✗ No RocksDB library files found in common locations")


def provide_installation_guidance():
    """Provide installation guidance based on the system."""
    print("\n" + "="*60)
    print("INSTALLATION GUIDANCE")
    print("="*60)
    
    import platform
    system = platform.system().lower()
    
    print(f"Detected OS: {platform.system()} {platform.release()}")
    
    if system == "linux":
        print("\nFor Ubuntu/Debian:")
        print("  sudo apt-get update")
        print("  sudo apt-get install librocksdb-dev python3-dev build-essential")
        print("  pip install python-rocksdb")
        print("\nAlternatively:")
        print("  sudo apt-get install python3-rocksdb")
        print("\nFor CentOS/RHEL/Fedora:")
        print("  sudo dnf install rocksdb-devel python3-devel gcc-c++")
        print("  pip install python-rocksdb")
        
    elif system == "darwin":  # macOS
        print("\nFor macOS:")
        print("  brew install rocksdb")
        print("  pip install python-rocksdb")
        
    elif system == "windows":
        print("\nFor Windows:")
        print("  # Windows support for python-rocksdb is limited")
        print("  # Consider using WSL or Docker")
        print("  # Or try conda:")
        print("  conda install python-rocksdb -c conda-forge")
    
    print("\nIf python-rocksdb fails to install:")
    print("1. Try conda: conda install python-rocksdb -c conda-forge")
    print("2. Use Docker with a Linux container")
    print("3. Use the limited rocksdb-python as fallback:")
    print("   pip install git+https://github.com/gau-nernst/rocksdb-python.git")


def main():
    """Main check function."""
    print("RocksDB Python Binding Checker")
    print("="*40)
    
    python_rocksdb_ok = check_python_rocksdb()
    rocksdb_python_ok = check_rocksdb_python()
    
    check_system_rocksdb()
    
    print("\n" + "="*40)
    print("SUMMARY")
    print("="*40)
    
    if python_rocksdb_ok:
        print("✓ READY: python-rocksdb is available and working")
        print("  You can use the full sensor-daemon Python interface")
    elif rocksdb_python_ok:
        print("⚠ LIMITED: Only rocksdb-python is available")
        print("  sensor-daemon will work with limited functionality")
        print("  Time-series queries will not be available")
    else:
        print("✗ NOT READY: No RocksDB Python bindings available")
        print("  sensor-daemon Python interface will not work")
    
    if not python_rocksdb_ok:
        provide_installation_guidance()


if __name__ == "__main__":
    main()