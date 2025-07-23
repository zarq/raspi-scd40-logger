#include <gtest/gtest.h>
#include "data_aggregator.hpp"
#include <chrono>
#include <vector>

using namespace sensor_daemon;

class DataAggregatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test data with known timestamps and values
        base_time_ = std::chrono::system_clock::now();
        
        // Create readings every 10 minutes for 2 hours
        for (int i = 0; i < 12; ++i) {
            SensorData reading;
            reading.timestamp = base_time_ + std::chrono::minutes(i * 10);
            reading.co2_ppm = 400.0f + i * 5.0f;  // 400, 405, 410, ...
            reading.temperature_c = 20.0f + i * 0.5f;  // 20.0, 20.5, 21.0, ...
            reading.humidity_percent = 40.0f + i * 2.0f;  // 40, 42, 44, ...
            reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            
            test_readings_.push_back(reading);
        }
    }
    
    void TearDown() override {}
    
    std::chrono::system_clock::time_point base_time_;
    std::vector<SensorData> test_readings_;
};

// IntervalParser tests
TEST_F(DataAggregatorTest, ParseIntervalValid) {
    EXPECT_EQ(IntervalParser::parse("1T").value().count(), 1);
    EXPECT_EQ(IntervalParser::parse("5T").value().count(), 5);
    EXPECT_EQ(IntervalParser::parse("30T").value().count(), 30);
    EXPECT_EQ(IntervalParser::parse("1H").value().count(), 60);
    EXPECT_EQ(IntervalParser::parse("2H").value().count(), 120);
    EXPECT_EQ(IntervalParser::parse("1D").value().count(), 24 * 60);
    EXPECT_EQ(IntervalParser::parse("1M").value().count(), 30 * 24 * 60);
}

TEST_F(DataAggregatorTest, ParseIntervalInvalid) {
    EXPECT_FALSE(IntervalParser::parse("").has_value());
    EXPECT_FALSE(IntervalParser::parse("1X").has_value());
    EXPECT_FALSE(IntervalParser::parse("H1").has_value());
    EXPECT_FALSE(IntervalParser::parse("invalid").has_value());
    EXPECT_FALSE(IntervalParser::parse("0H").has_value());
    EXPECT_FALSE(IntervalParser::parse("-1H").has_value());
}

TEST_F(DataAggregatorTest, IsValidFormat) {
    EXPECT_TRUE(IntervalParser::is_valid_format("1T"));
    EXPECT_TRUE(IntervalParser::is_valid_format("30T"));
    EXPECT_TRUE(IntervalParser::is_valid_format("1H"));
    EXPECT_TRUE(IntervalParser::is_valid_format("24H"));
    EXPECT_TRUE(IntervalParser::is_valid_format("1D"));
    EXPECT_TRUE(IntervalParser::is_valid_format("1M"));
    
    EXPECT_FALSE(IntervalParser::is_valid_format(""));
    EXPECT_FALSE(IntervalParser::is_valid_format("1"));
    EXPECT_FALSE(IntervalParser::is_valid_format("H"));
    EXPECT_FALSE(IntervalParser::is_valid_format("1X"));
    EXPECT_FALSE(IntervalParser::is_valid_format("H1"));
}

// DataAggregator tests
TEST_F(DataAggregatorTest, AggregateByIntervalString) {
    auto aggregates = DataAggregator::aggregate_by_interval(test_readings_, "1H");
    
    EXPECT_GT(aggregates.size(), 0);
    
    // Should have at least 2 hourly intervals for 2 hours of data
    EXPECT_GE(aggregates.size(), 2);
}

TEST_F(DataAggregatorTest, AggregateByIntervalMinutes) {
    auto aggregates = DataAggregator::aggregate_by_interval(test_readings_, std::chrono::minutes(60));
    
    EXPECT_GT(aggregates.size(), 0);
    
    // Check that aggregates have data
    bool found_data = false;
    for (const auto& aggregate : aggregates) {
        if (aggregate.co2_ppm.has_data) {
            found_data = true;
            EXPECT_GT(aggregate.co2_ppm.count, 0);
            EXPECT_GE(aggregate.co2_ppm.mean, 400.0);
            EXPECT_LE(aggregate.co2_ppm.mean, 500.0);
            break;
        }
    }
    EXPECT_TRUE(found_data);
}

TEST_F(DataAggregatorTest, AggregateEmptyReadings) {
    std::vector<SensorData> empty_readings;
    auto aggregates = DataAggregator::aggregate_by_interval(empty_readings, "1H");
    
    EXPECT_TRUE(aggregates.empty());
}

TEST_F(DataAggregatorTest, AggregateInvalidInterval) {
    auto aggregates = DataAggregator::aggregate_by_interval(test_readings_, "invalid");
    
    EXPECT_TRUE(aggregates.empty());
}

TEST_F(DataAggregatorTest, AggregateStatisticsCorrect) {
    // Create simple test data for easy verification
    std::vector<SensorData> simple_readings;
    auto start_time = std::chrono::system_clock::now();
    
    // Add 3 readings in the same hour with known values
    for (int i = 0; i < 3; ++i) {
        SensorData reading;
        reading.timestamp = start_time + std::chrono::minutes(i * 10);
        reading.co2_ppm = 400.0f + i * 10.0f;  // 400, 410, 420
        reading.temperature_c = 20.0f + i * 1.0f;  // 20, 21, 22
        reading.humidity_percent = 40.0f + i * 5.0f;  // 40, 45, 50
        reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
        
        simple_readings.push_back(reading);
    }
    
    auto aggregates = DataAggregator::aggregate_by_interval(simple_readings, std::chrono::minutes(60));
    
    ASSERT_GT(aggregates.size(), 0);
    
    // Find the aggregate with data
    AggregateData* data_aggregate = nullptr;
    for (auto& aggregate : aggregates) {
        if (aggregate.co2_ppm.has_data) {
            data_aggregate = &aggregate;
            break;
        }
    }
    
    ASSERT_NE(data_aggregate, nullptr);
    
    // Check CO2 statistics
    EXPECT_EQ(data_aggregate->co2_ppm.count, 3);
    EXPECT_FLOAT_EQ(data_aggregate->co2_ppm.mean, 410.0f);  // (400+410+420)/3
    EXPECT_FLOAT_EQ(data_aggregate->co2_ppm.min, 400.0f);
    EXPECT_FLOAT_EQ(data_aggregate->co2_ppm.max, 420.0f);
    
    // Check temperature statistics
    EXPECT_EQ(data_aggregate->temperature_c.count, 3);
    EXPECT_FLOAT_EQ(data_aggregate->temperature_c.mean, 21.0f);  // (20+21+22)/3
    EXPECT_FLOAT_EQ(data_aggregate->temperature_c.min, 20.0f);
    EXPECT_FLOAT_EQ(data_aggregate->temperature_c.max, 22.0f);
    
    // Check humidity statistics
    EXPECT_EQ(data_aggregate->humidity_percent.count, 3);
    EXPECT_FLOAT_EQ(data_aggregate->humidity_percent.mean, 45.0f);  // (40+45+50)/3
    EXPECT_FLOAT_EQ(data_aggregate->humidity_percent.min, 40.0f);
    EXPECT_FLOAT_EQ(data_aggregate->humidity_percent.max, 50.0f);
}

TEST_F(DataAggregatorTest, AggregateWithMissingValues) {
    std::vector<SensorData> readings_with_missing;
    auto start_time = std::chrono::system_clock::now();
    
    // First reading with all values
    SensorData reading1;
    reading1.timestamp = start_time;
    reading1.co2_ppm = 400.0f;
    reading1.temperature_c = 20.0f;
    reading1.humidity_percent = 40.0f;
    reading1.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
    readings_with_missing.push_back(reading1);
    
    // Second reading with missing temperature
    SensorData reading2;
    reading2.timestamp = start_time + std::chrono::minutes(10);
    reading2.co2_ppm = 410.0f;
    // temperature_c is nullopt
    reading2.humidity_percent = 45.0f;
    reading2.quality_flags = SensorData::CO2_VALID | SensorData::HUMIDITY_VALID;
    readings_with_missing.push_back(reading2);
    
    auto aggregates = DataAggregator::aggregate_by_interval(readings_with_missing, std::chrono::minutes(60));
    
    ASSERT_GT(aggregates.size(), 0);
    
    // Find the aggregate with data
    AggregateData* data_aggregate = nullptr;
    for (auto& aggregate : aggregates) {
        if (aggregate.co2_ppm.has_data) {
            data_aggregate = &aggregate;
            break;
        }
    }
    
    ASSERT_NE(data_aggregate, nullptr);
    
    // CO2 should have 2 values
    EXPECT_EQ(data_aggregate->co2_ppm.count, 2);
    EXPECT_FLOAT_EQ(data_aggregate->co2_ppm.mean, 405.0f);
    
    // Temperature should have only 1 value (missing value ignored)
    EXPECT_EQ(data_aggregate->temperature_c.count, 1);
    EXPECT_FLOAT_EQ(data_aggregate->temperature_c.mean, 20.0f);
    
    // Humidity should have 2 values
    EXPECT_EQ(data_aggregate->humidity_percent.count, 2);
    EXPECT_FLOAT_EQ(data_aggregate->humidity_percent.mean, 42.5f);
}

TEST_F(DataAggregatorTest, AggregateNoValidValues) {
    std::vector<SensorData> readings_no_values;
    auto start_time = std::chrono::system_clock::now();
    
    // Reading with no valid values
    SensorData reading;
    reading.timestamp = start_time;
    // All sensor values are nullopt
    reading.quality_flags = 0;
    readings_no_values.push_back(reading);
    
    auto aggregates = DataAggregator::aggregate_by_interval(readings_no_values, std::chrono::minutes(60));
    
    ASSERT_GT(aggregates.size(), 0);
    
    // All value stats should indicate no data
    for (const auto& aggregate : aggregates) {
        EXPECT_FALSE(aggregate.co2_ppm.has_data);
        EXPECT_FALSE(aggregate.temperature_c.has_data);
        EXPECT_FALSE(aggregate.humidity_percent.has_data);
    }
}

TEST_F(DataAggregatorTest, AggregateMultipleIntervals) {
    // Create data spanning multiple hours
    std::vector<SensorData> multi_hour_readings;
    auto start_time = std::chrono::system_clock::now();
    
    // Add readings for 3 hours (one reading per hour)
    for (int hour = 0; hour < 3; ++hour) {
        SensorData reading;
        reading.timestamp = start_time + std::chrono::hours(hour);
        reading.co2_ppm = 400.0f + hour * 10.0f;
        reading.temperature_c = 20.0f + hour * 1.0f;
        reading.humidity_percent = 40.0f + hour * 5.0f;
        reading.quality_flags = SensorData::CO2_VALID | SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
        
        multi_hour_readings.push_back(reading);
    }
    
    auto aggregates = DataAggregator::aggregate_by_interval(multi_hour_readings, std::chrono::minutes(60));
    
    // Should have at least 3 intervals
    EXPECT_GE(aggregates.size(), 3);
    
    // Count intervals with data
    int intervals_with_data = 0;
    for (const auto& aggregate : aggregates) {
        if (aggregate.co2_ppm.has_data) {
            intervals_with_data++;
            // Each interval should have exactly 1 reading
            EXPECT_EQ(aggregate.co2_ppm.count, 1);
        }
    }
    
    EXPECT_EQ(intervals_with_data, 3);
}

TEST_F(DataAggregatorTest, AggregateDifferentIntervalSizes) {
    // Test different interval sizes
    std::vector<std::string> intervals = {"30T", "1H", "2H"};
    
    for (const auto& interval : intervals) {
        auto aggregates = DataAggregator::aggregate_by_interval(test_readings_, interval);
        EXPECT_GT(aggregates.size(), 0) << "Failed for interval: " << interval;
        
        // Verify timestamps are set
        for (const auto& aggregate : aggregates) {
            EXPECT_NE(aggregate.timestamp, std::chrono::system_clock::time_point{});
        }
    }
}

TEST_F(DataAggregatorTest, GetSupportedFormats) {
    auto formats = IntervalParser::get_supported_formats();
    
    EXPECT_GT(formats.size(), 0);
    
    // Should contain common formats
    bool found_minute = false, found_hour = false, found_day = false;
    for (const auto& format : formats) {
        if (format.find("minute") != std::string::npos) found_minute = true;
        if (format.find("hour") != std::string::npos) found_hour = true;
        if (format.find("day") != std::string::npos) found_day = true;
    }
    
    EXPECT_TRUE(found_minute);
    EXPECT_TRUE(found_hour);
    EXPECT_TRUE(found_day);
}