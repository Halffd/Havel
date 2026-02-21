#include "HttpModule.hpp"
#include "../utils/Logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace havel {

// Static callback for curl write
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

// Static callback for curl header
static size_t HeaderCallback(void* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
    
    std::string headerStr((char*)buffer, size * nitems);
    
    // Skip status line
    if (headerStr.find("HTTP/") == 0) {
        return size * nitems;
    }
    
    // Parse header
    size_t colonPos = headerStr.find(':');
    if (colonPos != std::string::npos) {
        std::string key = headerStr.substr(0, colonPos);
        std::string value = headerStr.substr(colonPos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        
        // Convert key to lowercase
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        
        (*headers)[key] = value;
    }
    
    return size * nitems;
}

// Static callback for file write
static size_t FileWriteCallback(void* buffer, size_t size, size_t nmemb, void* userdata) {
    std::ofstream* file = static_cast<std::ofstream*>(userdata);
    file->write((char*)buffer, size * nmemb);
    return size * nmemb;
}

// Global HTTP instance
static std::unique_ptr<HttpModule> g_httpInstance;

HttpModule& getHttp() {
    if (!g_httpInstance) {
        g_httpInstance = std::make_unique<HttpModule>();
    }
    return *g_httpInstance;
}

// Initialize curl on first use
static bool curlInitialized = false;
static void ensureCurlInitialized() {
    if (!curlInitialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInitialized = true;
    }
}

HttpResponse HttpModule::get(const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers) {
    return request("GET", url, "", headers);
}

HttpResponse HttpModule::post(const std::string& url,
                               const std::string& data,
                               const std::unordered_map<std::string, std::string>& headers) {
    return request("POST", url, data, headers);
}

HttpResponse HttpModule::put(const std::string& url,
                              const std::string& data,
                              const std::unordered_map<std::string, std::string>& headers) {
    return request("PUT", url, data, headers);
}

HttpResponse HttpModule::del(const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers) {
    return request("DELETE", url, "", headers);
}

HttpResponse HttpModule::patch(const std::string& url,
                                const std::string& data,
                                const std::unordered_map<std::string, std::string>& headers) {
    return request("PATCH", url, data, headers);
}

HttpResponse HttpModule::request(const std::string& method,
                                  const std::string& url,
                                  const std::string& data,
                                  const std::unordered_map<std::string, std::string>& headers) {
    ensureCurlInitialized();
    
    HttpResponse response;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL";
        error("HttpModule: {}", response.error);
        return response;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    
    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs / 3);
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    
    // Set headers
    struct curl_slist* headerList = nullptr;
    
    // Add default headers
    for (const auto& [key, value] : defaultHeaders) {
        std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    
    // Add request-specific headers
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    
    // Set Content-Type if not specified and has data
    bool hasContentType = false;
    for (const auto& [key, value] : headers) {
        if (key == "content-type" || key == "Content-Type") {
            hasContentType = true;
            break;
        }
    }
    if (!hasContentType && !data.empty()) {
        headerList = curl_slist_append(headerList, "Content-Type: application/json");
    }
    
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Set request body for POST/PUT/PATCH
    if (!data.empty() && (method == "POST" || method == "PUT" || method == "PATCH")) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }
    
    // Capture response body
    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    
    // Capture response headers
    std::unordered_map<std::string, std::string> responseHeaders;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
        error("HttpModule: {} - {}", method, response.error);
    } else {
        // Get status code
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        
        // Get status text (if available)
        char* contentType = nullptr;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
        if (contentType) {
            response.statusText = contentType;
        }
        
        response.body = responseBody;
        response.headers = responseHeaders;
        
        debug("HttpModule: {} {} -> {}", method, url, response.statusCode);
    }
    
    // Cleanup
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    
    return response;
}

bool HttpModule::download(const std::string& url, const std::string& path) {
    ensureCurlInitialized();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        error("HttpModule: Failed to initialize CURL for download");
        return false;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs * 2); // Longer timeout for downloads
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs / 3);
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    
    // Open file for writing
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error("HttpModule: Failed to open file for download: {}", path);
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Write directly to file
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    file.close();
    
    bool success = (res == CURLE_OK);
    if (!success) {
        error("HttpModule: Download failed - {}", curl_easy_strerror(res));
        // Remove partial file
        std::remove(path.c_str());
    } else {
        info("HttpModule: Downloaded {} -> {}", url, path);
    }
    
    curl_easy_cleanup(curl);
    return success;
}

HttpResponse HttpModule::upload(const std::string& url,
                                 const std::string& filePath,
                                 const std::unordered_map<std::string, std::string>& headers) {
    ensureCurlInitialized();
    
    HttpResponse response;
    
    // Check if file exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        response.error = "File not found: " + filePath;
        error("HttpModule: {}", response.error);
        return response;
    }
    file.close();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize CURL";
        error("HttpModule: {}", response.error);
        return response;
    }
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs * 3); // Longer timeout for uploads
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs / 3);
    
    // Set upload
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
    // Set headers
    struct curl_slist* headerList = nullptr;
    
    // Add default headers
    for (const auto& [key, value] : defaultHeaders) {
        std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    
    // Add request-specific headers
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        headerList = curl_slist_append(headerList, header.c_str());
    }
    
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Open file for reading
    file.open(filePath, std::ios::binary | std::ios::in);
    if (!file) {
        response.error = "Failed to open file for upload";
        curl_easy_cleanup(curl);
        return response;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fileSize);
    curl_easy_setopt(curl, CURLOPT_READDATA, &file);
    
    // Capture response
    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    file.close();
    
    if (res != CURLE_OK) {
        response.error = std::string("CURL error: ") + curl_easy_strerror(res);
        error("HttpModule: Upload failed - {}", response.error);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
        response.body = responseBody;
        
        info("HttpModule: Uploaded {} -> {} (status: {})", filePath, url, response.statusCode);
    }
    
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    
    return response;
}

void HttpModule::setDefaultHeader(const std::string& key, const std::string& value) {
    defaultHeaders[key] = value;
}

void HttpModule::clearDefaultHeaders() {
    defaultHeaders.clear();
}

std::string HttpModule::buildQueryString(const std::unordered_map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) oss << "&";
        oss << urlEncode(key) << "=" << urlEncode(value);
        first = false;
    }
    
    return oss.str();
}

std::string HttpModule::urlEncode(const std::string& str) {
    std::ostringstream oss;
    
    for (char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else if (c == ' ') {
            oss << '+';
        } else {
            oss << '%' << std::uppercase << std::hex << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    
    return oss.str();
}

} // namespace havel
