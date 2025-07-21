#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class TimeSeriesQueriesTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_time_series_queries";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        storage_ = std::make_unique<TimeSeriesStorage>();
        ASSERT_TRUE(storage_->initialize(test_dir_, std::chrono::hours(24)));
    }
    
    void TearDown() override {
        storage_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    void insert_test_data() {
        auto base_time = std::chrono::system_clock::now() - std::chrono::hours(1);
        
        for (int i = 0; i < 10; ++i) {
            SensorData reading;
            reading.timestamp = base_time + std::chrono::minutes(i * 5);
            reading.co2_ppm = 400.0f + i * 10.0f;
            reading.temperature_c = 20.0f + i * 0.5f;
            reading.humidity_percent = 40.0f + i * 2.0f;
            reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            
            ASSERT_TRUE(storage_->store_reading(reading));
        }
    }
    
    std::string test_dir_;
    std::unique_ptr<TimeSeriesStorage> storage_;
};

TEST_F(TimeSeriesQueriesTest, GetRecentReadingsEmpty) {
    auto readings = storage_->get_recent_readings(5);
    EXPECT_TRUE(readings.empty());
}

TEST_F(TimeSeriesQueriesTest, GetRecentReadingsWithData) {
    insert_test_data();
    
    auto readings = storage_->get_recent_readings(5);
    EXPECT_EQ(readings.size(), 5);
    
    // Should be in reverse chronological order (newest first)
    for (size_t i = 1; i < readings.size(); ++i) {
        EXPECT_GE(readings[i-1].timestamp, readings[i].timestamp);
    }
}

TEST_F(TimeSeriesQueriesTest, GetRecentReadingsLimitCount) {
    insert_test_data();
    
    auto readings = storage_->get_recent_readings(3);
    EXPECT_EQ(readings.size(), 3);
    
    // Test with count larger than available data
    readings = storage_->get_recent_readings(20);
    EXPECT_EQ(readings.size(), 10);  // Should return all 10 inserted readings
}

TEST_F(TimeSeriesQueriesTest, GetReadingsInRangeEmpty) {
    auto start = std::chrono::system_clock::now() - std::chrono::hours(2);
    auto end = std::chrono::system_clock::now() - std::chrono::hours(1);
    
    auto readings = storage_->get_readings_in_range(start, end);
    EXPECT_TRUE(readings.empty());
}

TEST_F(TimeSeriesQueriesTest, GetReadingsInRangeWithData) {
    insert_test_data();
    
    auto start = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto end = std::chrono::system_clock::now();
    
    auto readings = storage_->get_readings_in_range(start, end);
    EXPECT_EQ(readings.size(), 10);
    
    // Should be in chronological order
    for (size_t i = 1; i < readings.size(); ++i) {
        EXPECT_LE(readings[i-1].timestamp, readings[i].timestamp);
    }
}

TEST_F(TimeSeriesQueriesTest, GetReadingsInRangePartial) {
    insert_test_data();
    
    auto start = std::chrono::system_clock::now() - std::chrono::minutes(30);
    auto end = std::chrono::system_clock::now() - std::chrono::minutes(10);
    
    auto readings = storage_->get_readings_in_range(start, end);
    EXPECT_GT(readings.size(), 0);
    EXPECT_LT(readings.size(), 10);
    
    // All readings should be within the specified range
    for (const auto& reading : readings) {
        EXPECT_GE(reading.timestamp, start);
        EXPECT_LE(reading.timestamp, end);
    }
}

TEST_F(TimeSeriesQueriesTest, GetDatabaseInfoEmpty) {
    auto info = storage_->get_database_info();
    
    EXPECT_EQ(info.database_path, test_dir_);
    EXPECT_TRUE(info.is_healthy);
    EXPECT_EQ(info.total_records, 0);
    EXPECT_EQ(info.implementation, "RocksDB via HTTP API");
}

TEST_F(TimeSeriesQueriesTest, GetDatabaseInfoWithData) {
    insert_test_data();
    
    auto info = storage_->get_database_info();
    
    EXPECT_EQ(info.database_path, test_dir_);
    EXPECT_TRUE(info.is_healthy);
    EXPECT_GT(info.total_records, 0);
    EXPECT_GT(info.database_size_bytes, 0);
    EXPECT_LE(info.earliest_timestamp, info.latest_timestamp);
}

TEST_F(TimeSeriesQueriesTest, InvalidParameters) {
    // Test negative count
    auto readings = storage_->get_recent_readings(-1);
    EXPECT_TRUE(readings.empty());
    
    // Test invalid time range (start > end)
    auto start = std::chrono::system_clock::now();
    auto end = start - std::chrono::hours(1);
    
    readings = storage_->get_readings_in_range(start, end);
    EXPECT_TRUE(readings.empty());
}

TEST_F(TimeSeriesQueriesTest, LargeCountLimiting) {
    insert_test_data();
    
    // Test that very large counts are limited
    auto readings = storage_->get_recent_readings(100000);
    EXPECT_EQ(readings.size(), 10);  // Should return all available data, not crash
}