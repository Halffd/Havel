#include <functional>
#include <stdexcept>
/*
 * NetworkService.cpp - Network operations using libcurl
 */
#include "NetworkService.hpp"

#include <curl/curl.h>
#include <cstring>

namespace havel::host {

// Callback for curl to write data to string
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *userp) {
  size_t total_size = size * nmemb;
  userp->append((char *)contents, total_size);
  return total_size;
}

// Callback for curl to write data to file
static size_t WriteFileCallback(void *contents, size_t size, size_t nmemb,
                                FILE *userp) {
  return fwrite(contents, size, nmemb, userp);
}

struct NetworkService::Impl {
  CURL *curl = nullptr;

  Impl() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
      throw std::runtime_error("Failed to initialize libcurl");
    }
  }

  ~Impl() {
    if (curl) {
      curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
  }

  HttpResponse make_request(const std::string &url, const std::string &method,
                            const std::string &post_data = "",
                            const std::string &content_type = "",
                            int timeout_ms = 30000) {
    HttpResponse response;

    if (!curl) {
      response.success = false;
      response.error = "curl not initialized";
      return response;
    }

    curl_easy_reset(curl);

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    // Set follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Response body
    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    // Response headers
    struct curl_slist *headers = nullptr;
    if (!content_type.empty()) {
      headers = curl_slist_append(headers,
                                  ("Content-Type: " + content_type).c_str());
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    // POST data if provided
    if (method == "POST" && !post_data.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    } else if (method == "GET") {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    // Get status code
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    response.status_code = static_cast<int>(response_code);

    // Clean up headers
    if (headers) {
      curl_slist_free_all(headers);
    }

    // Check result
    if (res != CURLE_OK) {
      response.success = false;
      response.error = curl_easy_strerror(res);
      return response;
    }

    response.body = response_body;
    response.success = (response_code >= 200 && response_code < 300);
    return response;
  }

  bool is_online() {
    // Try to connect to a well-known URL
    HttpResponse response = make_request("https://www.google.com", "GET", "", "",
                                         5000);
    return response.success;
  }

  std::string get_external_ip() {
    HttpResponse response =
        make_request("https://api.ipify.org", "GET", "", "", 5000);
    if (response.success) {
      return response.body;
    }
    return "";
  }
};

NetworkService::NetworkService() : impl_(new Impl()) {}

NetworkService::~NetworkService() { delete impl_; }

HttpResponse NetworkService::get(const std::string &url, int timeout_ms) {
  return impl_->make_request(url, "GET", "", "", timeout_ms);
}

HttpResponse NetworkService::post(const std::string &url,
                                  const std::string &data,
                                  const std::string &content_type,
                                  int timeout_ms) {
  return impl_->make_request(url, "POST", data, content_type, timeout_ms);
}

bool NetworkService::download(const std::string &url, const std::string &path,
                              int timeout_ms) {
  if (!impl_->curl) {
    return false;
  }

  curl_easy_reset(impl_->curl);

  // Set URL
  curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());

  // Set timeout
  curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT_MS, timeout_ms);

  // Set follow redirects
  curl_easy_setopt(impl_->curl, CURLOPT_FOLLOWLOCATION, 1L);

  // Open file for writing
  FILE *fp = fopen(path.c_str(), "wb");
  if (!fp) {
    return false;
  }

  // Write response to file
  curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
  curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, fp);

  // Perform download
  CURLcode res = curl_easy_perform(impl_->curl);

  // Close file
  fclose(fp);

  // Clean up on failure
  if (res != CURLE_OK) {
    remove(path.c_str());
    return false;
  }

  return true;
}

bool NetworkService::isOnline() const { return impl_->is_online(); }

std::string NetworkService::getExternalIp() const {
  return impl_->get_external_ip();
}

} // namespace havel::host
