# Test configuration

if(GTest_FOUND)
    # Test sources
    set(TEST_SOURCES
        test_config_manager.cpp
        test_sensor_data.cpp
        # Additional test sources will be added in subsequent tasks
        # test_daemon_core.cpp
        # test_sensor_interface.cpp
        # test_storage.cpp
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
        Threads::Threads
    )
    
    # Include directories for tests
    target_include_directories(sensor_daemon_tests PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    
    # Add source files to test build
    target_sources(sensor_daemon_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/config_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/sensor_data.cpp
    )
    
    # Register test
    add_test(NAME sensor_daemon_tests COMMAND sensor_daemon_tests)
endif()