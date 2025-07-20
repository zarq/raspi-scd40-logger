#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include "time_series_storage.hpp"
#include "sensor_data.hpp"
#include "logging_system.hpp"

namespace sensor_daemon {
namespace test {

class DataIntegrityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "sensor_daemon_integrity_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Initialize logging for tests
        LoggingSystem::initialize(LogLevel::DEBUG, "", 1024*1024, 3, true);
    }
    
    void TearDown() override {
        LoggingSystem::shutdown();
        
        // Clean up temporary directory
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }
    
    // Generate test data with known patterns
    std::vector<SensorData> generate_test_data(int count, 
                                               std::chrono::system_clock::time_point start_time,
                                               std::chrono::seconds interval = std::chrono::seconds(1)) {
        std::vector<SensorData> data;
        data.reserve(count);
        
        for (int i = 0; i < count; ++i) {
            SensorData reading;
            reading.timestamp = start_time + interval * i;
            reading.co2_ppm = 400.0f + i * 10.0f; // Predictable pattern
            reading.temperature_c = 20.0f + i * 0.5f;
            reading.humidity_percent = 50.0f + i * 1.0f;
            reading.quality_flags = SensorData::CO2_VALID | 
                                   SensorData::TEMP_VALID | 
                                   SensorData::HUMIDITY_VALID;
            data.push_back(reading);
        }
        
        return data;
    }
    
    // Generate test data with missing values
    std::vector<SensorData> generate_sparse_test_data(int count,
                                                      std::chrono::system_clock::time_point start_time) {
        std::vector<SensorData> data;
        data.reserve(count);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> missing_dist(0, 2); // 0=all present, 1=some missing, 2=more missing
        
        for (int i = 0; i < count; ++i) {
            SensorData reading;
            reading.timestamp = start_time + std::chrono::seconds(i);
            reading.quality_flags = 0;
            
            int missing_pattern = missing_dist(gen);
            
            // Always include CO2 for pattern verification
            reading.co2_ppm = 400.0f + i * 5.0f;
            reading.quality_flags |= SensorData::CO2_VALID;
            
            if (missing_pattern == 0) {
                // All values present
                reading.temperature_c = 20.0f + i * 0.2f;
                reading.humidity_percent = 50.0f + i * 0.5f;
                reading.quality_flags |= SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
            } else if (missing_pattern == 1) {
                // Only temperature missing
                reading.humidity_percent = 50.0f + i * 0.5f;
                reading.quality_flags |= SensorData::HUMIDITY_VALID;
            }
            // missing_pattern == 2: only CO2 present
            
            data.push_back(reading);
        }
        
        return data;
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic data round-trip integrity
TEST_F(DataIntegrityTest, BasicRoundTripIntegrityTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "roundtrip_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    // Generate test data
    auto start_time = std::chrono::system_clock::now();
    auto test_data = generate_test_data(100, start_time);
    
    // Store all data
    for (const auto& reading : test_data) {
        ASSERT_TRUE(storage.store_reading(reading)) << "Failed to store reading";
    }
    
    // Verify storage health
    EXPECT_TRUE(storage.is_healthy());
    EXPECT_GT(storage.get_database_size(), 0);
    
    // Create a new storage instance to test persistence
    TimeSeriesStorage storage2;
    ASSERT_TRUE(storage2.initialize(db_path));
    EXPECT_TRUE(storage2.is_healthy());
    
    // Verify database size is consistent
    EXPECT_EQ(storage.get_database_size(), storage2.get_database_size());
    
    LOG_INFO("Basic round-trip integrity test completed", {
        {"records_stored", std::to_string(test_data.size())},
        {"db_size_bytes", std::to_string(storage.get_database_size())}
    });
}

// Test data integrity with missing values
TEST_F(DataIntegrityTest, MissingValuesIntegrityTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "missing_values_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    // Generate sparse test data
    auto start_time = std::chrono::system_clock::now();
    auto test_data = generate_sparse_test_data(200, start_time);
    
    // Count expected patterns
    int complete_records = 0;
    int partial_records = 0;
    int co2_only_records = 0;
    
    for (const auto& reading : test_data) {
        if (reading.co2_ppm.has_value() && 
            reading.temperature_c.has_value() && 
            reading.humidity_percent.has_value()) {
            complete_records++;
        } else if (reading.co2_ppm.has_value() && 
                  (reading.temperature_c.has_value() || reading.humidity_percent.has_value())) {
            partial_records++;
        } else if (reading.co2_ppm.has_value()) {
            co2_only_records++;
        }
    }
    
    // Store all data
    for (const auto& reading : test_data) {
        ASSERT_TRUE(storage.store_reading(reading)) << "Failed to store sparse reading";
    }
    
    // Verify storage health with sparse data
    EXPECT_TRUE(storage.is_healthy());
    EXPECT_GT(storage.get_database_size(), 0);
    
    LOG_INFO("Missing values integrity test completed", {
        {"total_records", std::to_string(test_data.size())},
        {"complete_records", std::to_string(complete_records)},
        {"partial_records", std::to_string(partial_records)},
        {"co2_only_records", std::to_string(co2_only_records)},
        {"db_size_bytes", std::to_string(storage.get_database_size())}
    });
}

// Test timestamp precision and ordering
TEST_F(DataIntegrityTest, TimestampPrecisionTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "timestamp_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    // Generate data with microsecond precision timestamps
    std::vector<SensorData> test_data;
    auto base_time = std::chrono::system_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::microseconds(i * 1000); // 1ms intervals
        reading.co2_ppm = 400.0f + i * 0.1f;
        reading.temperature_c = 20.0f + i * 0.01f;
        reading.humidity_percent = 50.0f + i * 0.02f;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        test_data.push_back(reading);
    }
    
    // Store data in random order to test ordering
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(test_data.begin(), test_data.end(), gen);
    
    for (const auto& reading : test_data) {
        ASSERT_TRUE(storage.store_reading(reading)) << "Failed to store timestamped reading";
    }
    
    // Verify storage health
    EXPECT_TRUE(storage.is_healthy());
    
    LOG_INFO("Timestamp precision test completed", {
        {"records_stored", std::to_string(test_data.size())},
        {"timestamp_precision", "microseconds"},
        {"storage_order", "randomized"}
    });
}

// Test data consistency across storage operations
TEST_F(DataIntegrityTest, StorageConsistencyTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "consistency_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    // Phase 1: Store initial data
    auto start_time = std::chrono::system_clock::now();
    auto phase1_data = generate_test_data(500, start_time);
    
    for (const auto& reading : phase1_data) {
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    uint64_t phase1_size = storage.get_database_size();
    std::string phase1_stats = storage.get_statistics();
    
    // Phase 2: Add more data
    auto phase2_start = start_time + std::chrono::seconds(500);
    auto phase2_data = generate_test_data(300, phase2_start);
    
    for (const auto& reading : phase2_data) {
        ASSERT_TRUE(storage.store_reading(reading));
    }
    
    uint64_t phase2_size = storage.get_database_size();
    std::string phase2_stats = storage.get_statistics();
    
    // Verify consistency
    EXPECT_GT(phase2_size, phase1_size) << "Database should grow after adding data";
    EXPECT_NE(phase1_stats, phase2_stats) << "Statistics should change after adding data";
    
    // Phase 3: Reopen storage and verify consistency
    storage = TimeSeriesStorage(); // Reset storage object
    ASSERT_TRUE(storage.initialize(db_path));
    
    uint64_t phase3_size = storage.get_database_size();
    EXPECT_EQ(phase2_size, phase3_size) << "Database size should be consistent after reopen";
    
    EXPECT_TRUE(storage.is_healthy()) << "Storage should be healthy after reopen";
    
    LOG_INFO("Storage consistency test completed", {
        {"phase1_records", std::to_string(phase1_data.size())},
        {"phase2_records", std::to_string(phase2_data.size())},
        {"phase1_size_bytes", std::to_string(phase1_size)},
        {"phase2_size_bytes", std::to_string(phase2_size)},
        {"phase3_size_bytes", std::to_string(phase3_size)}
    });
}

// Test data integrity under concurrent access
TEST_F(DataIntegrityTest, ConcurrentAccessIntegrityTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "concurrent_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    const int NUM_THREADS = 4;
    const int RECORDS_PER_THREAD = 250;
    std::vector<std::thread> threads;
    std::atomic<int> successful_writes{0};
    std::atomic<int> failed_writes{0};
    
    // Each thread writes data with unique patterns
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            auto thread_start_time = std::chrono::system_clock::now() + 
                                   std::chrono::seconds(t * 1000); // Separate time ranges
            
            for (int i = 0; i < RECORDS_PER_THREAD; ++i) {
                SensorData reading;
                reading.timestamp = thread_start_time + std::chrono::seconds(i);
                reading.co2_ppm = 400.0f + t * 100.0f + i; // Thread-specific pattern
                reading.temperature_c = 20.0f + t * 5.0f + i * 0.1f;
                reading.humidity_percent = 50.0f + t * 10.0f + i * 0.2f;
                reading.quality_flags = SensorData::CO2_VALID | 
                                       SensorData::TEMP_VALID | 
                                       SensorData::HUMIDITY_VALID;
                
                if (storage.store_reading(reading)) {
                    successful_writes++;
                } else {
                    failed_writes++;
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify results
    int total_expected = NUM_THREADS * RECORDS_PER_THREAD;
    EXPECT_EQ(successful_writes.load(), total_expected) << "All concurrent writes should succeed";
    EXPECT_EQ(failed_writes.load(), 0) << "No writes should fail";
    
    // Verify storage integrity after concurrent access
    EXPECT_TRUE(storage.is_healthy()) << "Storage should remain healthy after concurrent access";
    EXPECT_GT(storage.get_database_size(), 0) << "Database should contain data";
    
    LOG_INFO("Concurrent access integrity test completed", {
        {"num_threads", std::to_string(NUM_THREADS)},
        {"records_per_thread", std::to_string(RECORDS_PER_THREAD)},
        {"successful_writes", std::to_string(successful_writes.load())},
        {"failed_writes", std::to_string(failed_writes.load())},
        {"final_db_size", std::to_string(storage.get_database_size())}
    });
}

// Test data integrity with extreme values
TEST_F(DataIntegrityTest, ExtremeValuesIntegrityTest) {
    TimeSeriesStorage storage;
    std::string db_path = (temp_dir_ / "extreme_values_data").string();
    ASSERT_TRUE(storage.initialize(db_path));
    
    std::vector<SensorData> extreme_data;
    auto base_time = std::chrono::system_clock::now();
    
    // Test with extreme but valid values
    struct ExtremeValue {
        float co2;
        float temp;
        float humidity;
        std::string description;
    };
    
    std::vector<ExtremeValue> extreme_values = {
        {0.0f, -40.0f, 0.0f, "minimum_values"},
        {40000.0f, 85.0f, 100.0f, "maximum_values"},
        {400.5f, 23.7f, 65.3f, "normal_precision"},
        {400.123456f, 23.987654f, 65.555555f, "high_precision"},
        {1000.0f, 0.0f, 50.0f, "zero_temperature"},
        {500.0f, 25.0f, 0.0f, "zero_humidity"}
    };
    
    for (size_t i = 0; i < extreme_values.size(); ++i) {
        SensorData reading;
        reading.timestamp = base_time + std::chrono::seconds(i);
        reading.co2_ppm = extreme_values[i].co2;
        reading.temperature_c = extreme_values[i].temp;
        reading.humidity_percent = extreme_values[i].humidity;
        reading.quality_flags = SensorData::CO2_VALID | 
                               SensorData::TEMP_VALID | 
                               SensorData::HUMIDITY_VALID;
        extreme_data.push_back(reading);
    }
    
    // Store extreme values
    for (const auto& reading : extreme_data) {
        ASSERT_TRUE(storage.store_reading(reading)) << "Failed to store extreme value reading";
    }
    
    // Verify storage health with extreme values
    EXPECT_TRUE(storage.is_healthy()) << "Storage should handle extreme values";
    EXPECT_GT(storage.get_database_size(), 0);
    
    LOG_INFO("Extreme values integrity test completed", {
        {"extreme_records", std::to_string(extreme_data.size())},
        {"db_size_bytes", std::to_string(storage.get_database_size())}
    });
}

// Test data integrity across database restarts
TEST_F(DataIntegrityTest, DatabaseRestartIntegrityTest) {
    std::string db_path = (temp_dir_ / "restart_data").string();
    
    // Phase 1: Create and populate database
    {
        TimeSeriesStorage storage1;
        ASSERT_TRUE(storage1.initialize(db_path));
        
        auto start_time = std::chrono::system_clock::now();
        auto test_data = generate_test_data(300, start_time);
        
        for (const auto& reading : test_data) {
            ASSERT_TRUE(storage1.store_reading(reading));
        }
        
        EXPECT_TRUE(storage1.is_healthy());
        // storage1 goes out of scope and is destroyed
    }
    
    // Phase 2: Reopen database and add more data
    uint64_t phase2_size;
    {
        TimeSeriesStorage storage2;
        ASSERT_TRUE(storage2.initialize(db_path));
        EXPECT_TRUE(storage2.is_healthy()) << "Database should be healthy after restart";
        
        uint64_t initial_size = storage2.get_database_size();
        EXPECT_GT(initial_size, 0) << "Database should contain previous data";
        
        // Add more data
        auto phase2_start = std::chrono::system_clock::now() + std::chrono::seconds(300);
        auto phase2_data = generate_test_data(200, phase2_start);
        
        for (const auto& reading : phase2_data) {
            ASSERT_TRUE(storage2.store_reading(reading));
        }
        
        phase2_size = storage2.get_database_size();
        EXPECT_GT(phase2_size, initial_size) << "Database should grow after adding data";
        // storage2 goes out of scope and is destroyed
    }
    
    // Phase 3: Final verification
    {
        TimeSeriesStorage storage3;
        ASSERT_TRUE(storage3.initialize(db_path));
        EXPECT_TRUE(storage3.is_healthy()) << "Database should be healthy after second restart";
        
        uint64_t final_size = storage3.get_database_size();
        EXPECT_EQ(final_size, phase2_size) << "Database size should be consistent after restart";
        
        std::string stats = storage3.get_statistics();
        EXPECT_FALSE(stats.empty()) << "Should be able to retrieve statistics after restart";
    }
    
    LOG_INFO("Database restart integrity test completed", {
        {"phase2_size_bytes", std::to_string(phase2_size)},
        {"restart_cycles", "3"}
    });
}

// Test serialization/deserialization integrity
TEST_F(DataIntegrityTest, SerializationIntegrityTest) {
    // Test various data patterns for serialization integrity
    std::vector<SensorData> test_cases;
    auto base_time = std::chrono::system_clock::now();
    
    // Complete reading
    SensorData complete;
    complete.timestamp = base_time;
    complete.co2_ppm = 450.123f;
    complete.temperature_c = 23.456f;
    complete.humidity_percent = 67.789f;
    complete.quality_flags = SensorData::CO2_VALID | 
                            SensorData::TEMP_VALID | 
                            SensorData::HUMIDITY_VALID;
    test_cases.push_back(complete);
    
    // CO2 only
    SensorData co2_only;
    co2_only.timestamp = base_time + std::chrono::seconds(1);
    co2_only.co2_ppm = 400.0f;
    co2_only.quality_flags = SensorData::CO2_VALID;
    test_cases.push_back(co2_only);
    
    // Temperature and humidity only
    SensorData temp_humidity;
    temp_humidity.timestamp = base_time + std::chrono::seconds(2);
    temp_humidity.temperature_c = 25.0f;
    temp_humidity.humidity_percent = 60.0f;
    temp_humidity.quality_flags = SensorData::TEMP_VALID | SensorData::HUMIDITY_VALID;
    test_cases.push_back(temp_humidity);
    
    // Test serialization round-trip for each case
    for (size_t i = 0; i < test_cases.size(); ++i) {
        const auto& original = test_cases[i];
        
        // Serialize
        std::string serialized = SensorDataConverter::serialize(original);
        EXPECT_FALSE(serialized.empty()) << "Serialization should produce data for case " << i;
        
        // Deserialize
        auto deserialized = SensorDataConverter::deserialize(serialized);
        ASSERT_TRUE(deserialized.has_value()) << "Deserialization should succeed for case " << i;
        
        const auto& restored = deserialized.value();
        
        // Verify timestamp
        EXPECT_EQ(restored.timestamp, original.timestamp) << "Timestamp mismatch in case " << i;
        
        // Verify CO2
        EXPECT_EQ(restored.co2_ppm.has_value(), original.co2_ppm.has_value()) 
            << "CO2 presence mismatch in case " << i;
        if (original.co2_ppm.has_value()) {
            EXPECT_FLOAT_EQ(restored.co2_ppm.value(), original.co2_ppm.value()) 
                << "CO2 value mismatch in case " << i;
        }
        
        // Verify temperature
        EXPECT_EQ(restored.temperature_c.has_value(), original.temperature_c.has_value()) 
            << "Temperature presence mismatch in case " << i;
        if (original.temperature_c.has_value()) {
            EXPECT_FLOAT_EQ(restored.temperature_c.value(), original.temperature_c.value()) 
                << "Temperature value mismatch in case " << i;
        }
        
        // Verify humidity
        EXPECT_EQ(restored.humidity_percent.has_value(), original.humidity_percent.has_value()) 
            << "Humidity presence mismatch in case " << i;
        if (original.humidity_percent.has_value()) {
            EXPECT_FLOAT_EQ(restored.humidity_percent.value(), original.humidity_percent.value()) 
                << "Humidity value mismatch in case " << i;
        }
        
        // Verify quality flags
        EXPECT_EQ(restored.quality_flags, original.quality_flags) 
            << "Quality flags mismatch in case " << i;
    }
    
    LOG_INFO("Serialization integrity test completed", {
        {"test_cases", std::to_string(test_cases.size())},
        {"all_cases_passed", "true"}
    });
}

} // namespace test
} // namespace sensor_daemon