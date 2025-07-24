# Test configuration

if(GTest_FOUND)
    # Test sources
    set(TEST_SOURCES
        test_config_manager.cpp
        test_sensor_data.cpp
        test_time_series_storage.cpp
        test_scd40_interface.cpp
        test_logging_system.cpp
        test_daemon_core.cpp
        test_health_monitor.cpp
        test_diagnostic_tools.cpp
        test_integration.cpp
        test_main_integration.cpp
        test_performance.cpp
        test_stress.cpp
        test_data_integrity.cpp
        test_performance_optimizations.cpp
    )
    
    # Create test executable
    add_executable(sensor_daemon_tests ${TEST_SOURCES})
    
    # Link libraries
    target_link_libraries(sensor_daemon_tests
        sensor_proto
        ${ROCKSDB_LIBRARY}
        ${SPDLOG_LIBRARY}
        ${Protobuf_LIBRARIES}
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
        Threads::Threads
    )
    
    # Add systemd support if available
    if(SYSTEMD_FOUND)
        target_compile_definitions(sensor_daemon_tests PRIVATE HAVE_SYSTEMD)
        target_link_libraries(sensor_daemon_tests ${SYSTEMD_LIBRARIES})
        target_include_directories(sensor_daemon_tests PRIVATE ${SYSTEMD_INCLUDE_DIRS})
    endif()
    
    # Include directories for tests
    target_include_directories(sensor_daemon_tests PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}/..
    )
    
    # Add source files to test build
    target_sources(sensor_daemon_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/config_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sensor_data.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/time_series_storage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/scd40_interface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging_system.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/daemon_core.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/health_monitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/diagnostic_tools.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/performance_cache.cpp
    )
    
    # Register test
    add_test(NAME sensor_daemon_tests COMMAND sensor_daemon_tests)
endif()