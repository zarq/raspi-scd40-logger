#include <gtest/gtest.h>
#include "http_error_handler.hpp"

using namespace sensor_daemon;

class HttpErrorHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test parameter error creation
TEST_F(HttpErrorHandlerTest, CreateParameterError) {
    std::string response = HttpErrorHandler::create_parameter_error("count", "-5", "positive integer");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"INVALID_PARAMETER\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"parameter\": \"count\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"provided_value\": \"-5\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"expected_format\": \"positive integer\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateParameterErrorMinimal) {
    std::string response = HttpErrorHandler::create_parameter_error("interval");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"INVALID_PARAMETER\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"parameter\": \"interval\"") != std::string::npos);
}

// Test missing parameters error
TEST_F(HttpErrorHandlerTest, CreateMissingParametersError) {
    std::vector<std::string> missing = {"start", "end"};
    std::string response = HttpErrorHandler::create_missing_parameters_error(missing);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"MISSING_PARAMETER\"") != std::string::npos);
    EXPECT_TRUE(response.find("'start', 'end'") != std::string::npos);
    EXPECT_TRUE(response.find("\"missing_count\": \"2\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateMissingParametersErrorSingle) {
    std::vector<std::string> missing = {"start"};
    std::string response = HttpErrorHandler::create_missing_parameters_error(missing);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("'start'") != std::string::npos);
    EXPECT_TRUE(response.find("\"missing_count\": \"1\"") != std::string::npos);
}

// Test storage unavailable error
TEST_F(HttpErrorHandlerTest, CreateStorageUnavailableError) {
    std::string response = HttpErrorHandler::create_storage_unavailable_error("Database connection failed");
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"STORAGE_UNAVAILABLE\"") != std::string::npos);
    EXPECT_TRUE(response.find("Database connection failed") != std::string::npos);
    EXPECT_TRUE(response.find("\"service\": \"time_series_storage\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateStorageUnavailableErrorNoReason) {
    std::string response = HttpErrorHandler::create_storage_unavailable_error();
    
    EXPECT_TRUE(response.find("HTTP/1.1 503 Service Unavailable") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"STORAGE_UNAVAILABLE\"") != std::string::npos);
}

// Test rate limit error
TEST_F(HttpErrorHandlerTest, CreateRateLimitError) {
    std::string response = HttpErrorHandler::create_rate_limit_error(120);
    
    EXPECT_TRUE(response.find("HTTP/1.1 429 Too Many Requests") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"RATE_LIMITED\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"retry_after\": \"120\"") != std::string::npos);
    EXPECT_TRUE(response.find("Wait 120 seconds") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateRateLimitErrorDefault) {
    std::string response = HttpErrorHandler::create_rate_limit_error();
    
    EXPECT_TRUE(response.find("HTTP/1.1 429 Too Many Requests") != std::string::npos);
    EXPECT_TRUE(response.find("\"retry_after\": \"60\"") != std::string::npos);
}

// Test internal error
TEST_F(HttpErrorHandlerTest, CreateInternalError) {
    std::string response = HttpErrorHandler::create_internal_error("database_query", "ERR-123");
    
    EXPECT_TRUE(response.find("HTTP/1.1 500 Internal Server Error") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"INTERNAL_ERROR\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"operation\": \"database_query\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_id\": \"ERR-123\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateInternalErrorAutoId) {
    std::string response = HttpErrorHandler::create_internal_error("test_operation");
    
    EXPECT_TRUE(response.find("HTTP/1.1 500 Internal Server Error") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"INTERNAL_ERROR\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"operation\": \"test_operation\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_id\": \"ERR-") != std::string::npos);
}

// Test not found error
TEST_F(HttpErrorHandlerTest, CreateNotFoundError) {
    std::string response = HttpErrorHandler::create_not_found_error("endpoint", "/data/invalid");
    
    EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"ENDPOINT_NOT_FOUND\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"resource_type\": \"endpoint\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"resource_id\": \"/data/invalid\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateNotFoundErrorDefault) {
    std::string response = HttpErrorHandler::create_not_found_error();
    
    EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found") != std::string::npos);
    EXPECT_TRUE(response.find("\"resource_type\": \"endpoint\"") != std::string::npos);
}

// Test method not allowed error
TEST_F(HttpErrorHandlerTest, CreateMethodNotAllowedError) {
    std::vector<std::string> allowed = {"GET", "POST"};
    std::string response = HttpErrorHandler::create_method_not_allowed_error("PUT", allowed);
    
    EXPECT_TRUE(response.find("HTTP/1.1 405") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"METHOD_NOT_ALLOWED\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"provided_method\": \"PUT\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"allowed_methods\": \"GET, POST\"") != std::string::npos);
}

TEST_F(HttpErrorHandlerTest, CreateMethodNotAllowedErrorDefault) {
    std::string response = HttpErrorHandler::create_method_not_allowed_error("POST");
    
    EXPECT_TRUE(response.find("HTTP/1.1 405") != std::string::npos);
    EXPECT_TRUE(response.find("\"provided_method\": \"POST\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"allowed_methods\": \"GET\"") != std::string::npos);
}

// Test status code mapping
TEST_F(HttpErrorHandlerTest, GetStatusCode) {
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::PARAMETER_VALIDATION), 400);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::AUTHENTICATION), 401);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::AUTHORIZATION), 403);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::RESOURCE_NOT_FOUND), 404);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::METHOD_NOT_ALLOWED), 405);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::RATE_LIMITING), 429);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::INTERNAL_ERROR), 500);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::SERVICE_UNAVAILABLE), 503);
    EXPECT_EQ(HttpErrorHandler::get_status_code(HttpErrorCategory::GATEWAY_TIMEOUT), 504);
}

// Test troubleshooting suggestions
TEST_F(HttpErrorHandlerTest, GetTroubleshootingSuggestions) {
    auto time_suggestions = HttpErrorHandler::get_troubleshooting_suggestions("INVALID_TIME_FORMAT");
    EXPECT_GT(time_suggestions.size(), 0);
    EXPECT_TRUE(std::any_of(time_suggestions.begin(), time_suggestions.end(),
        [](const std::string& s) { return s.find("ISO 8601") != std::string::npos; }));
    
    auto interval_suggestions = HttpErrorHandler::get_troubleshooting_suggestions("INVALID_INTERVAL");
    EXPECT_GT(interval_suggestions.size(), 0);
    EXPECT_TRUE(std::any_of(interval_suggestions.begin(), interval_suggestions.end(),
        [](const std::string& s) { return s.find("T=minutes") != std::string::npos; }));
    
    auto storage_suggestions = HttpErrorHandler::get_troubleshooting_suggestions("STORAGE_UNAVAILABLE");
    EXPECT_GT(storage_suggestions.size(), 0);
    
    auto unknown_suggestions = HttpErrorHandler::get_troubleshooting_suggestions("UNKNOWN_ERROR");
    EXPECT_GT(unknown_suggestions.size(), 0);
}

// Test detailed error creation
TEST_F(HttpErrorHandlerTest, CreateDetailedError) {
    ErrorDetails details("TEST_ERROR", "Test error message");
    details.with_details("Technical details here")
           .with_suggestion("Try this solution")
           .with_context("param1", "value1")
           .with_context("param2", "value2");
    
    std::string response = HttpErrorHandler::create_detailed_error(
        HttpErrorCategory::PARAMETER_VALIDATION, details);
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\": \"TEST_ERROR\"") != std::string::npos);
    EXPECT_TRUE(response.find("Test error message") != std::string::npos);
    EXPECT_TRUE(response.find("Technical details here") != std::string::npos);
    EXPECT_TRUE(response.find("Try this solution") != std::string::npos);
    EXPECT_TRUE(response.find("\"param1\": \"value1\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"param2\": \"value2\"") != std::string::npos);
}

// Test ErrorDetails builder pattern
TEST_F(HttpErrorHandlerTest, ErrorDetailsBuilder) {
    ErrorDetails details("CODE", "Message");
    
    auto& result = details.with_details("Details")
                          .with_suggestion("Suggestion")
                          .with_context("key", "value");
    
    EXPECT_EQ(details.error_code, "CODE");
    EXPECT_EQ(details.user_message, "Message");
    EXPECT_EQ(details.technical_details, "Details");
    EXPECT_EQ(details.suggested_action, "Suggestion");
    EXPECT_EQ(details.context["key"], "value");
    
    // Test that builder returns reference for chaining
    EXPECT_EQ(&result, &details);
}

// Test JSON escaping in error responses
TEST_F(HttpErrorHandlerTest, JsonEscapingInErrors) {
    std::string response = HttpErrorHandler::create_parameter_error(
        "test\"param", "value\"with\"quotes", "format\"with\"quotes");
    
    EXPECT_TRUE(response.find("test\\\"param") != std::string::npos);
    EXPECT_TRUE(response.find("value\\\"with\\\"quotes") != std::string::npos);
    EXPECT_TRUE(response.find("format\\\"with\\\"quotes") != std::string::npos);
}

// Test response structure
TEST_F(HttpErrorHandlerTest, ResponseStructure) {
    std::string response = HttpErrorHandler::create_parameter_error("test", "value", "format");
    
    // Check required fields
    EXPECT_TRUE(response.find("\"error\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"status_code\":") != std::string::npos);
    
    // Check HTTP headers
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(response.find("Connection: close") != std::string::npos);
}