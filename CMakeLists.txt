cmake_minimum_required(VERSION 3.16)
project(sensor-daemon VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Find RocksDB
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h)
find_library(ROCKSDB_LIBRARY rocksdb)
if(NOT ROCKSDB_INCLUDE_DIR OR NOT ROCKSDB_LIBRARY)
    message(FATAL_ERROR "RocksDB not found. Please install librocksdb-dev")
endif()

# Find spdlog
find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h)
find_library(SPDLOG_LIBRARY spdlog)
if(NOT SPDLOG_INCLUDE_DIR OR NOT SPDLOG_LIBRARY)
    message(FATAL_ERROR "spdlog not found. Please install libspdlog-dev")
endif()

# Find toml11 (header-only library)
find_path(TOML11_INCLUDE_DIR toml.hpp)
if(NOT TOML11_INCLUDE_DIR)
    message(FATAL_ERROR "toml11 not found. Please install libtoml11-dev")
endif()

# Check for systemd (optional for service integration)
pkg_check_modules(SYSTEMD libsystemd)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/proto)
include_directories(${ROCKSDB_INCLUDE_DIR})
include_directories(${SPDLOG_INCLUDE_DIR})
include_directories(${TOML11_INCLUDE_DIR})

# Protocol Buffers generation
set(PROTO_FILES
    proto/sensor_data.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Create proto library
add_library(sensor_proto ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(sensor_proto ${Protobuf_LIBRARIES})
target_include_directories(sensor_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Main daemon executable
set(DAEMON_SOURCES
    src/main.cpp
    # Additional source files will be added in subsequent tasks
)

add_executable(sensor-daemon ${DAEMON_SOURCES})

# Link libraries
target_link_libraries(sensor-daemon
    sensor_proto
    ${ROCKSDB_LIBRARY}
    ${SPDLOG_LIBRARY}
    ${Protobuf_LIBRARIES}
    Threads::Threads
)

# Add systemd support if available
if(SYSTEMD_FOUND)
    target_compile_definitions(sensor-daemon PRIVATE HAVE_SYSTEMD)
    target_link_libraries(sensor-daemon ${SYSTEMD_LIBRARIES})
    target_include_directories(sensor-daemon PRIVATE ${SYSTEMD_INCLUDE_DIRS})
endif()

# Installation
install(TARGETS sensor-daemon DESTINATION bin)
install(FILES config/sensor-daemon.toml DESTINATION /etc/sensor-daemon/ OPTIONAL)

# Testing (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    find_package(GTest)
    if(GTest_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING "GTest not found, tests will not be built")
    endif()
endif()

# Print configuration summary
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "RocksDB: ${ROCKSDB_LIBRARY}")
message(STATUS "spdlog: ${SPDLOG_LIBRARY}")
message(STATUS "Protobuf: ${Protobuf_LIBRARIES}")
if(SYSTEMD_FOUND)
    message(STATUS "systemd support: enabled")
else()
    message(STATUS "systemd support: disabled")
endif()