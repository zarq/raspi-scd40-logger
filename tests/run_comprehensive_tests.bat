@echo off
REM Comprehensive Test Runner for Sensor Daemon (Windows)
REM This script runs all test categories and generates a comprehensive report

setlocal enabledelayedexpansion

REM Test configuration
set TEST_EXECUTABLE=sensor_daemon_tests.exe
set LOG_FILE=test_results.log
set REPORT_FILE=test_report.txt

echo Starting Comprehensive Test Suite for Sensor Daemon
echo Test run started at: %date% %time% > %LOG_FILE%

REM Check if test executable exists
if not exist "%TEST_EXECUTABLE%" (
    echo Error: Test executable not found at %TEST_EXECUTABLE%
    echo Please build the tests first with: cmake --build . --target sensor_daemon_tests
    exit /b 1
)

REM Clean up previous results
del /q *_results.xml *_status.txt %REPORT_FILE% 2>nul

echo Phase 1: Unit Tests
echo === Unit Tests === >> %LOG_FILE%
%TEST_EXECUTABLE% --gtest_filter="*Test.*:*ConfigManagerTest*:*SensorDataTest*:*TimeSeriesStorageTest*:*SCD40InterfaceTest*:*LoggingSystemTest*" --gtest_output=xml:unit_results.xml >> %LOG_FILE% 2>&1
if !errorlevel! equ 0 (
    echo [32m✓ Unit Tests PASSED[0m
    echo PASSED > unit_status.txt
) else (
    echo [31m✗ Unit Tests FAILED[0m
    echo FAILED > unit_status.txt
)

echo Phase 2: Integration Tests
echo === Integration Tests === >> %LOG_FILE%
%TEST_EXECUTABLE% --gtest_filter="*IntegrationTest*:*MainIntegrationTest*:*DaemonCoreTest*" --gtest_output=xml:integration_results.xml >> %LOG_FILE% 2>&1
if !errorlevel! equ 0 (
    echo [32m✓ Integration Tests PASSED[0m
    echo PASSED > integration_status.txt
) else (
    echo [31m✗ Integration Tests FAILED[0m
    echo FAILED > integration_status.txt
)

echo Phase 3: Performance Tests
echo === Performance Tests === >> %LOG_FILE%
%TEST_EXECUTABLE% --gtest_filter="*PerformanceTest*" --gtest_output=xml:performance_results.xml >> %LOG_FILE% 2>&1
if !errorlevel! equ 0 (
    echo [32m✓ Performance Tests PASSED[0m
    echo PASSED > performance_status.txt
) else (
    echo [31m✗ Performance Tests FAILED[0m
    echo FAILED > performance_status.txt
)

echo Phase 4: Stress Tests
echo === Stress Tests === >> %LOG_FILE%
%TEST_EXECUTABLE% --gtest_filter="*StressTest*" --gtest_output=xml:stress_results.xml >> %LOG_FILE% 2>&1
if !errorlevel! equ 0 (
    echo [32m✓ Stress Tests PASSED[0m
    echo PASSED > stress_status.txt
) else (
    echo [31m✗ Stress Tests FAILED[0m
    echo FAILED > stress_status.txt
)

echo Phase 5: Data Integrity Tests
echo === Data Integrity Tests === >> %LOG_FILE%
%TEST_EXECUTABLE% --gtest_filter="*DataIntegrityTest*" --gtest_output=xml:data_integrity_results.xml >> %LOG_FILE% 2>&1
if !errorlevel! equ 0 (
    echo [32m✓ Data Integrity Tests PASSED[0m
    echo PASSED > data_integrity_status.txt
) else (
    echo [31m✗ Data Integrity Tests FAILED[0m
    echo FAILED > data_integrity_status.txt
)

REM Generate report
echo Sensor Daemon Comprehensive Test Report > %REPORT_FILE%
echo ======================================= >> %REPORT_FILE%
echo Generated: %date% %time% >> %REPORT_FILE%
echo. >> %REPORT_FILE%

echo Test Category Results: >> %REPORT_FILE%
echo --------------------- >> %REPORT_FILE%

for %%f in (unit_status.txt integration_status.txt performance_status.txt stress_status.txt data_integrity_status.txt) do (
    if exist %%f (
        set /p status=<%%f
        if "!status!"=="PASSED" (
            echo ✓ %%~nf: PASSED >> %REPORT_FILE%
        ) else (
            echo ✗ %%~nf: FAILED >> %REPORT_FILE%
        )
    )
)

echo. >> %REPORT_FILE%
echo System Information: >> %REPORT_FILE%
echo ------------------ >> %REPORT_FILE%
echo OS: %OS% >> %REPORT_FILE%
echo Processor: %PROCESSOR_ARCHITECTURE% >> %REPORT_FILE%
echo Number of Processors: %NUMBER_OF_PROCESSORS% >> %REPORT_FILE%
echo. >> %REPORT_FILE%
echo Detailed logs available in: %LOG_FILE% >> %REPORT_FILE%

echo Test Suite Complete!
echo Summary Report:
type %REPORT_FILE%

REM Check overall result
findstr "FAILED" *_status.txt >nul 2>&1
if !errorlevel! equ 0 (
    echo [31mOverall Result: SOME TESTS FAILED[0m
    exit /b 1
) else (
    echo [32mOverall Result: ALL TESTS PASSED[0m
    exit /b 0
)