/*
 * NetworkService.hpp - Network operations
 *
 * Provides basic network functionality:
 * - HTTP requests
 * - Network connectivity check
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * HttpResponse - HTTP response data
 */
struct HttpResponse {
  int status_code = 0;
  std::string body;
  std::vector<std::string> headers;
  bool success = false;
  std::string error;
};

/**
 * NetworkService - Network operations
 *
 * Provides basic HTTP client functionality.
 */
class NetworkService {
public:
  NetworkService();
  ~NetworkService();

  // ==========================================================================
  // HTTP requests
  // ==========================================================================

  /// GET request
  /// @param url URL to fetch
  /// @param timeout_ms Timeout in milliseconds (default: 30000)
  /// @return HttpResponse
  HttpResponse get(const std::string &url, int timeout_ms = 30000);

  /// POST request
  /// @param url URL to post to
  /// @param data POST data
  /// @param content_type Content-Type header (default: application/json)
  /// @param timeout_ms Timeout in milliseconds
  /// @return HttpResponse
  HttpResponse post(const std::string &url, const std::string &data,
                    const std::string &content_type = "application/json",
                    int timeout_ms = 30000);

  /// Download file to disk
  /// @param url URL to download from
  /// @param path Local file path to save
  /// @param timeout_ms Timeout in milliseconds
  /// @return true on success, false on error
  bool download(const std::string &url, const std::string &path,
                int timeout_ms = 30000);

  // ==========================================================================
  // Connectivity
  // ==========================================================================

  /// Check if network is available
  bool isOnline() const;

  /// Get external IP address
  std::string getExternalIp() const;

private:
  struct Impl;
  Impl *impl_;
};

} // namespace havel::host
