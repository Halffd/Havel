#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

/**
 * HTTP Response structure
 */
struct HttpResponse {
    int statusCode = 0;
    std::string statusText;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string error;
    
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

/**
 * HttpModule - HTTP client for REST API calls
 * 
 * Provides HTTP client functionality:
 * - http.get(url, headers) - GET request
 * - http.post(url, data, headers) - POST request
 * - http.put(url, data, headers) - PUT request
 * - http.del(url, headers) - DELETE request
 * - http.patch(url, data, headers) - PATCH request
 * - http.download(url, path) - Download file
 * - http.upload(url, path, headers) - Upload file
 */
class HttpModule {
public:
    HttpModule() = default;
    ~HttpModule() = default;

    // GET request
    HttpResponse get(const std::string& url, 
                     const std::unordered_map<std::string, std::string>& headers = {});
    
    // POST request
    HttpResponse post(const std::string& url, 
                      const std::string& data = "",
                      const std::unordered_map<std::string, std::string>& headers = {});
    
    // PUT request
    HttpResponse put(const std::string& url, 
                     const std::string& data = "",
                     const std::unordered_map<std::string, std::string>& headers = {});
    
    // DELETE request
    HttpResponse del(const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers = {});
    
    // PATCH request
    HttpResponse patch(const std::string& url,
                       const std::string& data = "",
                       const std::unordered_map<std::string, std::string>& headers = {});
    
    // Download file
    bool download(const std::string& url, const std::string& path);
    
    // Upload file (multipart/form-data)
    HttpResponse upload(const std::string& url,
                        const std::string& filePath,
                        const std::unordered_map<std::string, std::string>& headers = {});

    // Set default timeout (milliseconds)
    void setTimeout(int ms) { timeoutMs = ms; }
    int getTimeout() const { return timeoutMs; }

    // Set default headers
    void setDefaultHeader(const std::string& key, const std::string& value);
    void clearDefaultHeaders();

private:
    HttpResponse request(const std::string& method,
                         const std::string& url,
                         const std::string& data = "",
                         const std::unordered_map<std::string, std::string>& headers = {});
    
    std::string buildQueryString(const std::unordered_map<std::string, std::string>& params);
    std::string urlEncode(const std::string& str);

    int timeoutMs = 30000; // 30 seconds default
    std::unordered_map<std::string, std::string> defaultHeaders;
};

// Global HTTP instance (singleton pattern for interpreter access)
HttpModule& getHttp();

} // namespace havel
