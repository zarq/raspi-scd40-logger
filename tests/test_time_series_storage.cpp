#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include "time_series_storage.hpp"

using namespace sensor_daemon;

class TimeSeriesStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test database
        test_db_path_ = std::filesystem::temp_directory_path() / "sensor_daemon_test_db";
        std::filesystem::remove_all(test_db_path_);
        std::filesystem::create_directories(test_db_path_);
    }
    
    void TearDown() override {
        // Clean up test database
        std::filesystem::remove_all(test_db_path_);
    }
    
    std::filesystem::path test_db_path_;
};

TEST_F(TimeSeriesStorageTest, InitializationSuccess) {
    TimeSeriesStorage storage;
    
    EXPECT_TRUE(storage.initialize(test_db_path_.string()));
    EXPECT_TRUE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, InitializationWithCustomRetention) {
    TimeSeriesStorage storage;
    
    // Initialize with 48 hour retention
    auto retention = std::chrono::hours(48);
    EXPECT_TRUE(storage.initialize(test_db_path_.string(), retention));
    EXPECT_TRUE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, InitializationInvalidPath) {
    TimeSeriesStorage storage;
    
    // Try to initialize with invalid path (assuming /invalid/path doesn't exist and can't be created)
    EXPECT_FALSE(storage.initialize("/invalid/path/that/should/not/exist"));
    EXPECT_FALSE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, StoreReadingSuccess) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    // Create test sensor reading
    SensorReading reading;
    reading.timestamp = std::chrono::system_clock::now();
    reading.co2_ppm = 450.5f;
    reading.temperature_c = 22.3f;
    reading.humidity_percent = 65.2f;
    reading.quality_flags = SensorReading::CO2_VALID | 
                           SensorReading::TEMP_VALID | 
                           SensorReading::HUMIDITY_VALID;
    
    EXPECT_TRUE(storage.store_reading(reading));
}

TEST_F(TimeSeriesStorageTest, StoreReadingWithMissingValues) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    // Create test sensor reading with some missing values
    SensorReading reading;
    reading.timestamp = std::chrono::system_clock::now();
    reading.co2_ppm = 450.5f;  // Only CO2 is valid
    // temperature_c and humidity_percent are not set (optional fields)
    reading.quality_flags = SensorReading::CO2_VALID;
    
    EXPECT_TRUE(storage.store_reading(reading));
}

TEST_F(TimeSeriesStorageTest, StoreMultipleReadings) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    auto base_time = std::chrono::system_clock::now();
    
    // Store multiple readings with different timestamps
    for (int i = 0; i < 10; ++i) {
        SensorReading reading;
        reading.timestamp = base_time + std::chrono::seconds(i * 30);  // 30 second intervals
        reading.co2_ppm = 400.0f + i * 10.0f;  // Varying CO2 values
        reading.temperature_c = 20.0f + i * 0.5f;  // Varying temperature
        reading.humidity_percent = 50.0f + i * 2.0f;  // Varying humidity
        reading.quality_flags = SensorReading::CO2_VALID | 
                               SensorReading::TEMP_VALID | 
                               SensorReading::HUMIDITY_VALID;
        
        EXPECT_TRUE(storage.store_reading(reading));
    }
    
    EXPECT_TRUE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, StoreReadingUninitializedStorage) {
    TimeSeriesStorage storage;
    // Don't initialize storage
    
    SensorReading reading;
    reading.timestamp = std::chrono::system_clock::now();
    reading.co2_ppm = 450.5f;
    
    EXPECT_FALSE(storage.store_reading(reading));
    EXPECT_FALSE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, DatabaseSizeTracking) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    uint64_t initial_size = storage.get_database_size();
    
    // Store some readings
    auto base_time = std::chrono::system_clock::now();
    for (int i = 0; i < 5; ++i) {
        SensorReading reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + i;
        reading.temperature_c = 20.0f + i;
        reading.humidity_percent = 50.0f + i;
        reading.quality_flags = SensorReading::CO2_VALID | 
                               SensorReading::TEMP_VALID | 
                               SensorReading::HUMIDITY_VALID;
        
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    uint64_t final_size = storage.get_database_size();
    
    // Database should have grown
    EXPECT_GT(final_size, initial_size);
}

TEST_F(TimeSeriesStorageTest, StatisticsRetrieval) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    std::string stats = storage.get_statistics();
    EXPECT_FALSE(stats.empty());
    EXPECT_NE(stats, "Database not initialized");
}

TEST_F(TimeSeriesStorageTest, StatisticsUninitializedStorage) {
    TimeSeriesStorage storage;
    // Don't initialize storage
    
    std::string stats = storage.get_statistics();
    EXPECT_EQ(stats, "Database not initialized");
}

TEST_F(TimeSeriesStorageTest, CleanupOldData) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    // Store some readings
    auto base_time = std::chrono::system_clock::now();
    for (int i = 0; i < 3; ++i) {
        SensorReading reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + i;
        reading.quality_flags = SensorReading::CO2_VALID;
        
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    // Cleanup should not crash
    EXPECT_NO_THROW(storage.cleanup_old_data());
    EXPECT_TRUE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, TimestampKeyConversion) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    // Test with current time
    auto now = std::chrono::system_clock::now();
    
    // Store reading with specific timestamp
    SensorReading reading;
    reading.timestamp = now;
    reading.co2_ppm = 400.0f;
    reading.quality_flags = SensorReading::CO2_VALID;
    
    EXPECT_TRUE(storage.store_reading(reading));
    
    // Test with microsecond precision
    auto precise_time = std::chrono::system_clock::time_point(
        std::chrono::microseconds(1640995200123456)  // Specific microsecond timestamp
    );
    
    reading.timestamp = precise_time;
    reading.co2_ppm = 401.0f;
    
    EXPECT_TRUE(storage.store_reading(reading));
}

TEST_F(TimeSeriesStorageTest, HighFrequencyWrites) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    auto base_time = std::chrono::system_clock::now();
    
    // Store readings at high frequency (every 100ms)
    for (int i = 0; i < 50; ++i) {
        SensorReading reading;
        reading.timestamp = base_time + std::chrono::milliseconds(i * 100);
        reading.co2_ppm = 400.0f + (i % 10);  // Cycling values
        reading.temperature_c = 20.0f + (i % 5);
        reading.humidity_percent = 50.0f + (i % 8);
        reading.quality_flags = SensorReading::CO2_VALID | 
                               SensorReading::TEMP_VALID | 
                               SensorReading::HUMIDITY_VALID;
        
        EXPECT_TRUE(storage.store_reading(reading));
    }
    
    EXPECT_TRUE(storage.is_healthy());
}

TEST_F(TimeSeriesStorageTest, MoveSemantics) {
    TimeSeriesStorage storage1;
    ASSERT_TRUE(storage1.initialize(test_db_path_.string()));
    
    // Store a reading
    SensorReading reading;
    reading.timestamp = std::chrono::system_clock::now();
    reading.co2_ppm = 400.0f;
    reading.quality_flags = SensorReading::CO2_VALID;
    
    ASSERT_TRUE(storage1.store_reading(reading));
    ASSERT_TRUE(storage1.is_healthy());
    
    // Move construct
    TimeSeriesStorage storage2 = std::move(storage1);
    
    // Original should be in moved-from state
    EXPECT_FALSE(storage1.is_healthy());
    
    // New storage should work
    EXPECT_TRUE(storage2.is_healthy());
    EXPECT_TRUE(storage2.store_reading(reading));
}

// Test for TTL functionality (basic test - full TTL testing would require waiting)
TEST_F(TimeSeriesStorageTest, TTLConfiguration) {
    TimeSeriesStorage storage;
    
    // Initialize with very short retention for testing (1 hour)
    auto short_retention = std::chrono::hours(1);
    ASSERT_TRUE(storage.initialize(test_db_path_.string(), short_retention));
    
    // Store a reading
    SensorReading reading;
    reading.timestamp = std::chrono::system_clock::now();
    reading.co2_ppm = 400.0f;
    reading.quality_flags = SensorReading::CO2_VALID;
    
    EXPECT_TRUE(storage.store_reading(reading));
    EXPECT_TRUE(storage.is_healthy());
    
    // Manual cleanup should work
    EXPECT_NO_THROW(storage.cleanup_old_data());
}

// Performance test - ensure storage operations are reasonably fast
TEST_F(TimeSeriesStorageTest, PerformanceBaseline) {
    TimeSeriesStorage storage;
    ASSERT_TRUE(storage.initialize(test_db_path_.string()));
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Store 100 readings
    auto base_time = std::chrono::system_clock::now();
    for (int i = 0; i < 100; ++i) {
        SensorReading reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = 400.0f + i;
        reading.temperature_c = 20.0f + i * 0.1f;
        reading.humidity_percent = 50.0f + i * 0.2f;
        reading.quality_flags = SensorReading::CO2_VALID | 
                               SensorReading::TEMP_VALID | 
                               SensorReading::HUMIDITY_VALID;
        
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Should complete in reasonable time (less than 1 second for 100 writes)
    EXPECT_LT(duration.count(), 1000);
    
    std::cout << "Stored 100 readings in " << duration.count() << "ms" << std::endl;
}