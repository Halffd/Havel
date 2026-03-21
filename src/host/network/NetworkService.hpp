/*
 * NetworkService.hpp
 *
 * Network service for HTTP requests.
 * Provides HTTP client functionality.
 * 
 * Uses libcurl internally.
 */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace havel::host {

/**
 * HTTP response structure
 */
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = false;
    std::string error;
};

/**
 * NetworkService - HTTP client
 * 
 * Provides:
 * - GET/POST/PUT/DELETE requests
 * - Custom headers
 * - Request/response bodies
 * - Timeout configuration
 */
class NetworkService {
public:
    NetworkService();
    ~NetworkService();

    // =========================================================================
    // HTTP methods
    // =========================================================================

    /// Send GET request
    /// @param url URL to request
    /// @param headers Optional headers
    /// @param timeoutMs Timeout in milliseconds (0 = no timeout)
    /// @return HTTP response
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {},
                     int timeoutMs = 30000);

    /// Send POST request
    /// @param url URL to request
    /// @param body Request body
    /// @param headers Optional headers
    /// @param timeoutMs Timeout in milliseconds
    /// @return HTTP response
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::map<std::string, std::string>& headers = {},
                      int timeoutMs = 30000);

    /// Send PUT request
    HttpResponse put(const std::string& url,
                     const std::string& body,
                     const std::map<std::string, std::string>& headers = {},
                     int timeoutMs = 30000);

    /// Send DELETE request
    HttpResponse del(const std::string& url,
                     const std::map<std::string, std::string>& headers = {},
                     int timeoutMs = 30000);

    // =========================================================================
    // Download/Upload
    // =========================================================================

    /// Download file from URL
    /// @param url URL to download from
    /// @param outputPath Local path to save file
    /// @return true if successful
    bool download(const std::string& url, const std::string& outputPath);

    /// Upload file to URL
    /// @param url URL to upload to
    /// @param filePath Local file to upload
    /// @return HTTP response
    HttpResponse upload(const std::string& url, const std::string& filePath);

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set default timeout
    void setDefaultTimeout(int timeoutMs);

    /// Get default timeout
    int getDefaultTimeout() const;

    /// Set user agent
    void setUserAgent(const std::string& userAgent);

    /// Get user agent
    std::string getUserAgent() const;

private:
    int m_defaultTimeout = 30000;
    std::string m_userAgent = "Havel/1.0";
};

} // namespace havel::host
