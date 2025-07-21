#!/bin/bash

# Comprehensive Test Runner for Sensor Daemon
# This script runs all test categories and generates a comprehensive report

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
TEST_EXECUTABLE="./sensor_daemon_tests"
LOG_FILE="test_results.log"
REPORT_FILE="test_report.txt"

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to run test category
run_test_category() {
    local category=$1
    local filter=$2
    local description=$3
    
    print_status $BLUE "Running $description..."
    echo "=== $description ===" >> $LOG_FILE
    
    if $TEST_EXECUTABLE --gtest_filter="$filter" --gtest_output=xml:${category}_results.xml >> $LOG_FILE 2>&1; then
        print_status $GREEN "✓ $description PASSED"
        echo "PASSED" > ${category}_status.txt
    else
        print_status $RED "✗ $description FAILED"
        echo "FAILED" > ${category}_status.txt
    fi
    
    echo "" >> $LOG_FILE
}

# Function to generate summary report
generate_report() {
    local start_time=$1
    local end_time=$2
    
    echo "Sensor Daemon Comprehensive Test Report" > $REPORT_FILE
    echo "=======================================" >> $REPORT_FILE
    echo "Generated: $(date)" >> $REPORT_FILE
    echo "Duration: $((end_time - start_time)) seconds" >> $REPORT_FILE
    echo "" >> $REPORT_FILE
    
    # Test category results
    echo "Test Category Results:" >> $REPORT_FILE
    echo "---------------------" >> $REPORT_FILE
    
    categories=("unit" "integration" "performance" "stress" "data_integrity")
    descriptions=("Unit Tests" "Integration Tests" "Performance Tests" "Stress Tests" "Data Integrity Tests")
    
    for i in "${!categories[@]}"; do
        category=${categories[$i]}
        description=${descriptions[$i]}
        
        if [ -f "${category}_status.txt" ]; then
            status=$(cat ${category}_status.txt)
            if [ "$status" = "PASSED" ]; then
                echo "✓ $description: PASSED" >> $REPORT_FILE
            else
                echo "✗ $description: FAILED" >> $REPORT_FILE
            fi
        else
            echo "? $description: NOT RUN" >> $REPORT_FILE
        fi
    done
    
    echo "" >> $REPORT_FILE
    
    # System information
    echo "System Information:" >> $REPORT_FILE
    echo "------------------" >> $REPORT_FILE
    echo "OS: $(uname -s -r)" >> $REPORT_FILE
    echo "Architecture: $(uname -m)" >> $REPORT_FILE
    echo "CPU: $(nproc) cores" >> $REPORT_FILE
    echo "Memory: $(free -h | grep '^Mem:' | awk '{print $2}')" >> $REPORT_FILE
    echo "" >> $REPORT_FILE
    
    # Test statistics from XML files
    if command -v xmllint >/dev/null 2>&1; then
        echo "Test Statistics:" >> $REPORT_FILE
        echo "---------------" >> $REPORT_FILE
        
        total_tests=0
        total_failures=0
        
        for xml_file in *_results.xml; do
            if [ -f "$xml_file" ]; then
                tests=$(xmllint --xpath "//testsuites/@tests" "$xml_file" 2>/dev/null | sed 's/tests="//;s/"//' || echo "0")
                failures=$(xmllint --xpath "//testsuites/@failures" "$xml_file" 2>/dev/null | sed 's/failures="//;s/"//' || echo "0")
                
                total_tests=$((total_tests + tests))
                total_failures=$((total_failures + failures))
            fi
        done
        
        echo "Total Tests: $total_tests" >> $REPORT_FILE
        echo "Total Failures: $total_failures" >> $REPORT_FILE
        echo "Success Rate: $(( (total_tests - total_failures) * 100 / total_tests ))%" >> $REPORT_FILE
    fi
    
    echo "" >> $REPORT_FILE
    echo "Detailed logs available in: $LOG_FILE" >> $REPORT_FILE
}

# Main execution
main() {
    print_status $YELLOW "Starting Comprehensive Test Suite for Sensor Daemon"
    echo "Test run started at: $(date)" > $LOG_FILE
    
    # Check if test executable exists
    if [ ! -f "$TEST_EXECUTABLE" ]; then
        print_status $RED "Error: Test executable not found at $TEST_EXECUTABLE"
        print_status $YELLOW "Please build the tests first with: cmake --build . --target sensor_daemon_tests"
        exit 1
    fi
    
    start_time=$(date +%s)
    
    # Clean up previous results
    rm -f *_results.xml *_status.txt $REPORT_FILE
    
    # Run test categories
    print_status $YELLOW "Phase 1: Unit Tests"
    run_test_category "unit" "*Test.*:*ConfigManagerTest*:*SensorDataTest*:*TimeSeriesStorageTest*:*SCD40InterfaceTest*:*LoggingSystemTest*" "Unit Tests"
    
    print_status $YELLOW "Phase 2: Integration Tests"
    run_test_category "integration" "*IntegrationTest*:*MainIntegrationTest*:*DaemonCoreTest*" "Integration Tests"
    
    print_status $YELLOW "Phase 3: Performance Tests"
    run_test_category "performance" "*PerformanceTest*" "Performance Tests"
    
    print_status $YELLOW "Phase 4: Stress Tests"
    run_test_category "stress" "*StressTest*" "Stress Tests"
    
    print_status $YELLOW "Phase 5: Data Integrity Tests"
    run_test_category "data_integrity" "*DataIntegrityTest*" "Data Integrity Tests"
    
    end_time=$(date +%s)
    
    # Generate comprehensive report
    generate_report $start_time $end_time
    
    # Display summary
    print_status $YELLOW "Test Suite Complete!"
    print_status $BLUE "Summary Report:"
    cat $REPORT_FILE
    
    # Check overall result
    if grep -q "FAILED" *_status.txt 2>/dev/null; then
        print_status $RED "Overall Result: SOME TESTS FAILED"
        exit 1
    else
        print_status $GREEN "Overall Result: ALL TESTS PASSED"
        exit 0
    fi
}

# Handle script arguments
case "${1:-all}" in
    "unit")
        run_test_category "unit" "*Test.*:*ConfigManagerTest*:*SensorDataTest*:*TimeSeriesStorageTest*:*SCD40InterfaceTest*:*LoggingSystemTest*" "Unit Tests"
        ;;
    "integration")
        run_test_category "integration" "*IntegrationTest*:*MainIntegrationTest*:*DaemonCoreTest*" "Integration Tests"
        ;;
    "performance")
        run_test_category "performance" "*PerformanceTest*" "Performance Tests"
        ;;
    "stress")
        run_test_category "stress" "*StressTest*" "Stress Tests"
        ;;
    "integrity")
        run_test_category "data_integrity" "*DataIntegrityTest*" "Data Integrity Tests"
        ;;
    "all"|*)
        main
        ;;
esac