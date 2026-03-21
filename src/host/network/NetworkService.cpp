/*
 * NetworkService.cpp
 *
 * Network service implementation (stub).
 */
#include "NetworkService.hpp"
// NetworkService is a stub - full implementation requires libcurl

namespace havel::host {

NetworkService::NetworkService() {
}

NetworkService::~NetworkService() {
}

HttpResponse NetworkService::get(const std::string& url,
                                  const std::map<std::string, std::string>& headers,
                                  int timeoutMs) {
    (void)url; (void)headers; (void)timeoutMs;
    HttpResponse response;
    response.statusCode = 501;  // Not Implemented
    response.error = "NetworkService not implemented (requires libcurl)";
    return response;
}

HttpResponse NetworkService::post(const std::string& url,
                                   const std::string& body,
                                   const std::map<std::string, std::string>& headers,
                                   int timeoutMs) {
    (void)url; (void)body; (void)headers; (void)timeoutMs;
    HttpResponse response;
    response.statusCode = 501;
    response.error = "NetworkService not implemented (requires libcurl)";
    return response;
}

HttpResponse NetworkService::put(const std::string& url,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  int timeoutMs) {
    (void)url; (void)body; (void)headers; (void)timeoutMs;
    HttpResponse response;
    response.statusCode = 501;
    response.error = "NetworkService not implemented (requires libcurl)";
    return response;
}

HttpResponse NetworkService::del(const std::string& url,
                                  const std::map<std::string, std::string>& headers,
                                  int timeoutMs) {
    (void)url; (void)headers; (void)timeoutMs;
    HttpResponse response;
    response.statusCode = 501;
    response.error = "NetworkService not implemented (requires libcurl)";
    return response;
}

bool NetworkService::download(const std::string& url, const std::string& outputPath) {
    (void)url; (void)outputPath;
    return false;
}

HttpResponse NetworkService::upload(const std::string& url, const std::string& filePath) {
    (void)url; (void)filePath;
    HttpResponse response;
    response.statusCode = 501;
    response.error = "NetworkService not implemented (requires libcurl)";
    return response;
}

void NetworkService::setDefaultTimeout(int timeoutMs) {
    m_defaultTimeout = timeoutMs;
}

int NetworkService::getDefaultTimeout() const {
    return m_defaultTimeout;
}

void NetworkService::setUserAgent(const std::string& userAgent) {
    m_userAgent = userAgent;
}

std::string NetworkService::getUserAgent() const {
    return m_userAgent;
}

} // namespace havel::host
