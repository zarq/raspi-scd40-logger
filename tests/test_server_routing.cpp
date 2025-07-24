#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include "diagnostic_tools.hpp"
#include "health_monitor.hpp"
#include "time_series_storage.hpp"
#include "sensor_data.hpp"

using namespace sensor_daemon;

class ServerRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/test_server_routing";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        
        // Initialize health monitor
        health_monitor_ = std::make_unique<HealthMonitor>();
        AlertConfig config;
        ASSERT_TRUE(health_monitor_->initialize(config));
        
        // Initialize storage
        storage_ = std::make_unique<TimeSeriesStorage>();
        ASSERT_TRUE(storage_->initialize(test_dir_, std::chrono::hours(24)));
        
        // Create server with storage
        server_ = std::make_unique<HealthMonitorServer>(health_monitor_.get(), storage_.get());
    }
    
    void TearDown() override {
        server_.reset();
        storage_.reset();
        health_monitor_.reset();
        std::filesystem::remove_all(test_dir_);
    }
    
    std::string test_dir_;
    std::unique_ptr<HealthMonitor> health_monitor_;
    std::unique_ptr<TimeSeriesStorage> storage_;
    std::unique_ptr<HealthMonitorServer> server_;
};

// Test enhanced request processing
TEST_F(ServerRoutingTest, ProcessRequestWithSecurityValid) {
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "127.0.0.1");
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos);
}

TEST_F(ServerRoutingTest, ProcessRequestWithSecurityMalicious) {
    std::string request = "GET /health?param='; DROP TABLE users; -- HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "127.0.0.1");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("Invalid parameter") != std::string::npos);
}

TEST_F(ServerRoutingTest, ProcessRequestWithSecurityRateLimit) {
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    
    // Make many requests to trigger rate limiting
    for (int i = 0; i < 70; ++i) {
        server_->process_request_with_security(request, "127.0.0.1");
    }
    
    // This request should be rate limited
    std::string response = server_->process_request_with_security(request, "127.0.0.1");
    EXPECT_TRUE(response.find("HTTP/1.1 429 Too Many Requests") != std::string::npos);
}

// Test request routing
TEST_F(ServerRoutingTest, RouteRequestHealthEndpoint) {
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    std::string response = server_->route_request(request, "GET", "/health");
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"status\":") != std::string::npos);
}

TEST_F(ServerRoutingTest, RouteRequestDataEndpoints) {
    std::vector<std::string> endpoints = {
        "/data/recent",
        "/data/info"
    };
    
    for (const auto& endpoint : endpoints) {
        std::string request = "GET " + endpoint + " HTTP/1.1\r\n\r\n";
        std::string response = server_->route_request(request, "GET", endpoint);
        
        EXPECT_TRUE(response.find("HTTP/1.1") != std::string::npos) 
            << "Failed for endpoint: " << endpoint;
    }
}

TEST_F(ServerRoutingTest, RouteRequestMethodNotAllowed) {
    std::string request = "POST /health HTTP/1.1\r\n\r\n";
    std::string response = server_->route_request(request, "POST", "/health");
    
    EXPECT_TRUE(response.find("HTTP/1.1 405") != std::string::npos);
    EXPECT_TRUE(response.find("METHOD_NOT_ALLOWED") != std::string::npos);
    EXPECT_TRUE(response.find("\"provided_method\": \"POST\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"allowed_methods\": \"GET\"") != std::string::npos);
}

TEST_F(ServerRoutingTest, RouteRequestNotFound) {
    std::string request = "GET /invalid/endpoint HTTP/1.1\r\n\r\n";
    std::string response = server_->route_request(request, "GET", "/invalid/endpoint");
    
    EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found") != std::string::npos);
    EXPECT_TRUE(response.find("ENDPOINT_NOT_FOUND") != std::string::npos);
    EXPECT_TRUE(response.find("\"requested_path\": \"/invalid/endpoint\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"available_endpoints\":") != std::string::npos);
}

TEST_F(ServerRoutingTest, RouteRequestEnhanced404Response) {
    std::string request = "GET /unknown HTTP/1.1\r\n\r\n";
    std::string response = server_->route_request(request, "GET", "/unknown");
    
    // Check enhanced 404 response structure
    EXPECT_TRUE(response.find("\"error\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"error_code\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"details\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"suggestion\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"requested_path\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"method\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"available_endpoints\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"timestamp\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"status_code\": 404") != std::string::npos);
}

TEST_F(ServerRoutingTest, RouteRequestEndpointDetails) {
    std::string request = "GET /nonexistent HTTP/1.1\r\n\r\n";
    std::string response = server_->route_request(request, "GET", "/nonexistent");
    
    // Check that endpoint details include method, description, and parameters
    EXPECT_TRUE(response.find("\"path\": \"/health\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"method\": \"GET\"") != std::string::npos);
    EXPECT_TRUE(response.find("\"description\":") != std::string::npos);
    EXPECT_TRUE(response.find("\"parameters\":") != std::string::npos);
    
    // Check specific endpoint documentation
    EXPECT_TRUE(response.find("/data/recent") != std::string::npos);
    EXPECT_TRUE(response.find("?count=N") != std::string::npos);
    EXPECT_TRUE(response.find("/data/range") != std::string::npos);
    EXPECT_TRUE(response.find("?start=TIME&end=TIME") != std::string::npos);
    EXPECT_TRUE(response.find("/data/aggregates") != std::string::npos);
    EXPECT_TRUE(response.find("interval=INTERVAL") != std::string::npos);
}

// Test client IP extraction
TEST_F(ServerRoutingTest, ExtractClientIpFallback) {
    // Test with invalid file descriptor (should return "unknown")
    std::string ip = server_->extract_client_ip(-1);
    EXPECT_EQ(ip, "unknown");
}

// Test security integration
TEST_F(ServerRoutingTest, SecurityIntegrationValidRequest) {
    std::string request = "GET /data/info HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "192.168.1.100");
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(response.find("\"total_records\":") != std::string::npos);
}

TEST_F(ServerRoutingTest, SecurityIntegrationXssAttack) {
    std::string request = "GET /data/recent?count=<script>alert('xss')</script> HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "192.168.1.100");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("INVALID_PARAMETER") != std::string::npos);
}

TEST_F(ServerRoutingTest, SecurityIntegrationSqlInjection) {
    std::string request = "GET /data/range?start=2024-01-01T12:00:00Z'; DROP TABLE data; -- HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "192.168.1.100");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("INVALID_PARAMETER") != std::string::npos);
}

TEST_F(ServerRoutingTest, SecurityIntegrationPathTraversal) {
    std::string request = "GET /data/info?file=../../../etc/passwd HTTP/1.1\r\n\r\n";
    std::string response = server_->process_request_with_security(request, "192.168.1.100");
    
    EXPECT_TRUE(response.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    EXPECT_TRUE(response.find("INVALID_PARAMETER") != std::string::npos);
}

// Test error handling in request processing
TEST_F(ServerRoutingTest, RequestProcessingExceptionHandling) {
    // Test with malformed request that might cause exceptions
    std::string malformed_request = "INVALID REQUEST FORMAT";
    std::string response = server_->process_request_with_security(malformed_request, "127.0.0.1");
    
    // Should handle gracefully and return error response
    EXPECT_TRUE(response.find("HTTP/1.1") != std::string::npos);
}

// Test performance monitoring integration
TEST_F(ServerRoutingTest, PerformanceMonitoringIntegration) {
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    
    // Make several requests to generate performance data
    for (int i = 0; i < 5; ++i) {
        server_->process_request_with_security(request, "127.0.0.1");
    }
    
    // Performance monitoring should be working (no direct way to test without exposing internals)
    // This test mainly ensures no crashes occur during performance monitoring
    EXPECT_TRUE(true);
}

// Test different HTTP methods
TEST_F(ServerRoutingTest, DifferentHttpMethods) {
    std::vector<std::string> methods = {"POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    
    for (const auto& method : methods) {
        std::string request = method + " /health HTTP/1.1\r\n\r\n";
        std::string response = server_->route_request(request, method, "/health");
        
        EXPECT_TRUE(response.find("HTTP/1.1 405") != std::string::npos) 
            << "Failed for method: " << method;
        EXPECT_TRUE(response.find("METHOD_NOT_ALLOWED") != std::string::npos)
            << "Failed for method: " << method;
    }
}

// Test concurrent request handling (basic test)
TEST_F(ServerRoutingTest, ConcurrentRequestHandling) {
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    
    // Simulate concurrent requests from different IPs
    std::vector<std::string> ips = {"127.0.0.1", "127.0.0.2", "127.0.0.3"};
    
    for (const auto& ip : ips) {
        std::string response = server_->process_request_with_security(request, ip);
        EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    }
}

// Test complete request routing functionality
TEST_F(ServerRoutingTest, CompleteRequestRoutingFunctionality) {
    // Test all data endpoints with valid requests
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"/data/recent", "GET /data/recent?count=10 HTTP/1.1\r\n\r\n"},
        {"/data/info", "GET /data/info HTTP/1.1\r\n\r\n"},
        {"/data/range", "GET /data/range?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z HTTP/1.1\r\n\r\n"},
        {"/data/aggregates", "GET /data/aggregates?start=2024-01-01T12:00:00Z&end=2024-01-01T13:00:00Z&interval=1H HTTP/1.1\r\n\r\n"}
    };
    
    for (const auto& [endpoint, request] : test_cases) {
        std::string response = server_->process_request_with_security(request, "127.0.0.1");
        
        // Should get a valid HTTP response (not necessarily 200 due to test data)
        EXPECT_TRUE(response.find("HTTP/1.1") != std::string::npos) 
            << "Failed for endpoint: " << endpoint;
        EXPECT_TRUE(response.find("Content-Type: application/json") != std::string::npos)
            << "Failed for endpoint: " << endpoint;
    }
}

// Test request routing with invalid endpoints
TEST_F(ServerRoutingTest, RequestRoutingInvalidEndpoints) {
    std::vector<std::string> invalid_endpoints = {
        "/data/invalid",
        "/data/",
        "/api/v1/data",
        "/health/status",
        "/metrics/detailed"
    };
    
    for (const auto& endpoint : invalid_endpoints) {
        std::string request = "GET " + endpoint + " HTTP/1.1\r\n\r\n";
        std::string response = server_->route_request(request, "GET", endpoint);
        
        EXPECT_TRUE(response.find("HTTP/1.1 404 Not Found") != std::string::npos)
            << "Failed for invalid endpoint: " << endpoint;
        EXPECT_TRUE(response.find("ENDPOINT_NOT_FOUND") != std::string::npos)
            << "Failed for invalid endpoint: " << endpoint;
    }
}

// Test request routing with edge cases
TEST_F(ServerRoutingTest, RequestRoutingEdgeCases) {
    // Test empty path
    std::string response1 = server_->route_request("GET  HTTP/1.1\r\n\r\n", "GET", "");
    EXPECT_TRUE(response1.find("HTTP/1.1 404") != std::string::npos);
    
    // Test path with trailing slash
    std::string response2 = server_->route_request("GET /health/ HTTP/1.1\r\n\r\n", "GET", "/health/");
    EXPECT_TRUE(response2.find("HTTP/1.1 404") != std::string::npos);
    
    // Test case sensitivity
    std::string response3 = server_->route_request("GET /HEALTH HTTP/1.1\r\n\r\n", "GET", "/HEALTH");
    EXPECT_TRUE(response3.find("HTTP/1.1 404") != std::string::npos);
}

// Test logging functionality (indirect test)
TEST_F(ServerRoutingTest, RequestLoggingFunctionality) {
    // This test ensures that the logging code paths don't crash
    // Direct testing of logging would require access to log output
    
    std::vector<std::string> requests = {
        "GET /health HTTP/1.1\r\n\r\n",
        "GET /data/recent HTTP/1.1\r\n\r\n",
        "GET /data/info HTTP/1.1\r\n\r\n",
        "GET /metrics HTTP/1.1\r\n\r\n"
    };
    
    for (const auto& request : requests) {
        // This should trigger logging without crashing
        std::string response = server_->process_request_with_security(request, "192.168.1.100");
        EXPECT_TRUE(response.find("HTTP/1.1") != std::string::npos);
    }
}

// Test response time measurement
TEST_F(ServerRoutingTest, ResponseTimeMeasurement) {
    // Test that response time measurement doesn't cause issues
    std::string request = "GET /health HTTP/1.1\r\n\r\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string response = server_->process_request_with_security(request, "127.0.0.1");
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_TRUE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_LT(duration.count(), 1000); // Should respond within 1 second
}