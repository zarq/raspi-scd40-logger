#include <gtest/gtest.h>
#include "http_utils.hpp"
#include <chrono>

using namespace sensor_daemon;

class HttpUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// HttpParameterParser tests
TEST_F(HttpUtilsTest, ExtractQueryStringBasic) {
    std::string request = "GET /data/recent?count=10 HTTP/1.1\r\n";
    std::string query = HttpParameterParser::extract_query_string(request);
    EXPECT_EQ(query, "count=10");
}

TEST_F(HttpUtilsTest, ExtractQueryStringMultipleParams) {
    std::string request = "GET /data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z HTTP/1.1\r\n";
    std::string query = HttpParameterParser::extract_query_string(request);
    EXPECT_EQ(query, "start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z");
}

TEST_F(HttpUtilsTest, ExtractQueryStringNoParams) {
    std::string request = "GET /data/recent HTTP/1.1\r\n";
    std::string query = HttpParameterParser::extract_query_string(request);
    EXPECT_EQ(query, "");
}

TEST_F(HttpUtilsTest, ParseQueryStringBasic) {
    std::string query = "count=10&interval=1H";
    auto params = HttpParameterParser::parse_query_string(query);
    
    EXPECT_EQ(params.size(), 2);
    EXPECT_EQ(params["count"], "10");
    EXPECT_EQ(params["interval"], "1H");
}

TEST_F(HttpUtilsTest, ParseQueryStringEmpty) {
    std::string query = "";
    auto params = HttpParameterParser::parse_query_string(query);
    EXPECT_TRUE(params.empty());
}

TEST_F(HttpUtilsTest, ParseQueryStringWithoutValues) {
    std::string query = "flag1&flag2=value&flag3";
    auto params = HttpParameterParser::parse_query_string(query);
    
    EXPECT_EQ(params.size(), 3);
    EXPECT_EQ(params["flag1"], "");
    EXPECT_EQ(params["flag2"], "value");
    EXPECT_EQ(params["flag3"], "");
}

TEST_F(HttpUtilsTest, UrlDecodeBasic) {
    EXPECT_EQ(HttpParameterParser::url_decode("hello%20world"), "hello world");
    EXPECT_EQ(HttpParameterParser::url_decode("test%2Bvalue"), "test+value");
    EXPECT_EQ(HttpParameterParser::url_decode("no+encoding"), "no encoding");
}

TEST_F(HttpUtilsTest, UrlDecodeSpecialChars) {
    EXPECT_EQ(HttpParameterParser::url_decode("2024-01-01T12%3A00%3A00Z"), "2024-01-01T12:00:00Z");
    EXPECT_EQ(HttpParameterParser::url_decode("value%21%40%23"), "value!@#");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPath) {
    std::string request = "GET /data/recent?count=10 HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathPost) {
    std::string request = "POST /api/submit HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "POST");
    EXPECT_EQ(path, "/api/submit");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathPut) {
    std::string request = "PUT /api/update HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "PUT");
    EXPECT_EQ(path, "/api/update");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathDelete) {
    std::string request = "DELETE /api/delete HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "DELETE");
    EXPECT_EQ(path, "/api/delete");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathWithQueryString) {
    std::string request = "GET /data/recent?count=10&interval=1H HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent"); // Query string should be stripped
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathRootPath) {
    std::string request = "GET / HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathLongPath) {
    std::string request = "GET /api/v1/data/sensor/temperature/recent HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/api/v1/data/sensor/temperature/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathWithNewlineOnly) {
    std::string request = "GET /data/recent HTTP/1.1\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathNoNewline) {
    std::string request = "GET /data/recent HTTP/1.1";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

// Malformed request tests - should return empty strings
TEST_F(HttpUtilsTest, ExtractMethodAndPathEmptyRequest) {
    std::string request = "";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "");
    EXPECT_EQ(path, "");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathOnlyMethod) {
    std::string request = "GET\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "");
    EXPECT_EQ(path, "");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathOnlyMethodAndPath) {
    std::string request = "GET /data\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "");
    EXPECT_EQ(path, "");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathMissingPath) {
    std::string request = "GET HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "");
    EXPECT_EQ(path, "");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathInvalidFormat) {
    std::string request = "INVALID REQUEST FORMAT\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "");
    EXPECT_EQ(path, "");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathExtraSpaces) {
    std::string request = "GET  /data/recent  HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathLowercaseMethod) {
    std::string request = "get /data/recent HTTP/1.1\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "get");
    EXPECT_EQ(path, "/data/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathHttp10) {
    std::string request = "GET /data/recent HTTP/1.0\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

TEST_F(HttpUtilsTest, ExtractMethodAndPathHttp2) {
    std::string request = "GET /data/recent HTTP/2.0\r\n";
    auto [method, path] = HttpParameterParser::extract_method_and_path(request);
    
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(path, "/data/recent");
}

// QueryParameters tests
TEST_F(HttpUtilsTest, ParseUrlParametersCount) {
    std::string request = "GET /data/recent?count=50 HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_TRUE(params.count.has_value());
    EXPECT_EQ(params.count.value(), 50);
    EXPECT_FALSE(params.start_time.has_value());
    EXPECT_FALSE(params.end_time.has_value());
}

TEST_F(HttpUtilsTest, ParseUrlParametersTimeRange) {
    std::string request = "GET /data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_FALSE(params.count.has_value());
    EXPECT_TRUE(params.start_time.has_value());
    EXPECT_TRUE(params.end_time.has_value());
    EXPECT_EQ(params.start_time.value(), "2024-01-01T12:00:00Z");
    EXPECT_EQ(params.end_time.value(), "2024-01-01T13:00:00Z");
}

TEST_F(HttpUtilsTest, ParseUrlParametersInterval) {
    std::string request = "GET /data/aggregates?start=2024-01-01T00:00:00Z&end=2024-01-02T00:00:00Z&interval=1H HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_TRUE(params.start_time.has_value());
    EXPECT_TRUE(params.end_time.has_value());
    EXPECT_TRUE(params.interval.has_value());
    EXPECT_EQ(params.interval.value(), "1H");
}

TEST_F(HttpUtilsTest, ParseUrlParametersInvalidCount) {
    std::string request = "GET /data/recent?count=invalid HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_FALSE(params.count.has_value());
}

TEST_F(HttpUtilsTest, ParseUrlParametersNegativeCount) {
    std::string request = "GET /data/recent?count=-10 HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_FALSE(params.count.has_value());
}

// Iso8601Parser tests
TEST_F(HttpUtilsTest, Iso8601ParseBasic) {
    auto result = Iso8601Parser::parse("2024-01-01T12:00:00Z");
    EXPECT_TRUE(result.has_value());
}

TEST_F(HttpUtilsTest, Iso8601ParseWithMilliseconds) {
    auto result = Iso8601Parser::parse("2024-01-01T12:00:00.123Z");
    EXPECT_TRUE(result.has_value());
}

TEST_F(HttpUtilsTest, Iso8601ParseInvalid) {
    EXPECT_FALSE(Iso8601Parser::parse("invalid").has_value());
    EXPECT_FALSE(Iso8601Parser::parse("2024-01-01").has_value());
    EXPECT_FALSE(Iso8601Parser::parse("2024-01-01T12:00:00").has_value()); // Missing Z
    EXPECT_FALSE(Iso8601Parser::parse("2024-13-01T12:00:00Z").has_value()); // Invalid month
}

TEST_F(HttpUtilsTest, Iso8601Format) {
    auto now = std::chrono::system_clock::now();
    std::string formatted = Iso8601Parser::format(now);
    
    // Should be able to parse back
    auto parsed = Iso8601Parser::parse(formatted);
    EXPECT_TRUE(parsed.has_value());
    
    // Should end with Z
    EXPECT_TRUE(formatted.back() == 'Z');
    EXPECT_TRUE(formatted.find('T') != std::string::npos);
}

// ParameterValidator tests
TEST_F(HttpUtilsTest, ValidateCountValid) {
    EXPECT_TRUE(ParameterValidator::validate_count(1));
    EXPECT_TRUE(ParameterValidator::validate_count(100));
    EXPECT_TRUE(ParameterValidator::validate_count(10000));
}

TEST_F(HttpUtilsTest, ValidateCountInvalid) {
    EXPECT_FALSE(ParameterValidator::validate_count(0));
    EXPECT_FALSE(ParameterValidator::validate_count(-1));
    EXPECT_FALSE(ParameterValidator::validate_count(10001));
}

TEST_F(HttpUtilsTest, ValidateTimeRangeValid) {
    auto start = std::chrono::system_clock::now();
    auto end = start + std::chrono::hours(1);
    
    EXPECT_TRUE(ParameterValidator::validate_time_range(start, end));
}

TEST_F(HttpUtilsTest, ValidateTimeRangeInvalid) {
    auto start = std::chrono::system_clock::now();
    auto end = start - std::chrono::hours(1); // End before start
    
    EXPECT_FALSE(ParameterValidator::validate_time_range(start, end));
}

TEST_F(HttpUtilsTest, ValidateTimeRangeTooLong) {
    auto start = std::chrono::system_clock::now();
    auto end = start + std::chrono::hours(24 * 8); // 8 days (default max is 7 days)
    
    EXPECT_FALSE(ParameterValidator::validate_time_range(start, end));
}

TEST_F(HttpUtilsTest, ValidateIntervalValid) {
    EXPECT_TRUE(ParameterValidator::validate_interval("1H"));
    EXPECT_TRUE(ParameterValidator::validate_interval("30T"));
    EXPECT_TRUE(ParameterValidator::validate_interval("1D"));
    EXPECT_TRUE(ParameterValidator::validate_interval("2M"));
}

TEST_F(HttpUtilsTest, ValidateIntervalInvalid) {
    EXPECT_FALSE(ParameterValidator::validate_interval("invalid"));
    EXPECT_FALSE(ParameterValidator::validate_interval("1X"));
    EXPECT_FALSE(ParameterValidator::validate_interval("H1"));
    EXPECT_FALSE(ParameterValidator::validate_interval(""));
}

TEST_F(HttpUtilsTest, ParseIntervalValid) {
    auto result1 = ParameterValidator::parse_interval("1H");
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().count(), 60);
    
    auto result2 = ParameterValidator::parse_interval("30T");
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().count(), 30);
    
    auto result3 = ParameterValidator::parse_interval("1D");
    EXPECT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().count(), 24 * 60);
}

// QueryParameters validation tests
TEST_F(HttpUtilsTest, QueryParametersValidation) {
    std::string request = "GET /data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z&count=100&interval=1H HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_TRUE(params.is_count_valid());
    EXPECT_TRUE(params.is_time_range_valid());
    EXPECT_TRUE(params.is_interval_valid());
}

TEST_F(HttpUtilsTest, QueryParametersInvalidTimeRange) {
    std::string request = "GET /data/range?start=2024-01-01T13:00:00Z&end=2024-01-01T12:00:00Z HTTP/1.1\r\n";
    auto params = QueryParameters::parse_url_parameters(request);
    
    EXPECT_FALSE(params.is_time_range_valid());
}