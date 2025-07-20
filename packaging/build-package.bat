@echo off
REM Build script for sensor-daemon Debian package (Windows version)
REM This script is for reference - actual package building should be done on Linux

echo Building sensor-daemon Debian packages...
echo.
echo This script is intended to run on a Linux system with Debian packaging tools.
echo On Windows, you can use WSL (Windows Subsystem for Linux) or a Linux VM.
echo.
echo Required tools on Linux:
echo   - dpkg-dev
echo   - cmake
echo   - g++
echo   - debhelper
echo.
echo To build on Linux:
echo   1. Copy this project to a Linux system
echo   2. Install build dependencies: sudo apt install dpkg-dev cmake g++ debhelper
echo   3. Run: ./packaging/build-package.sh
echo.
echo For WSL users:
echo   1. Enable WSL and install Ubuntu/Debian
echo   2. Copy project files to WSL filesystem
echo   3. Install dependencies and run build script
echo.
pause