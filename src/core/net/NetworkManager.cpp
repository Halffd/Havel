#include "NetworkManager.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <mutex>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

namespace havel {
namespace net {

// TcpClient::Impl definition
struct TcpClient::Impl {
  NetworkConfig config;
  NetworkCallback callback;
  std::atomic<bool> running{false};
  int socketFd = -1;
  std::thread readThread;

  explicit Impl(const NetworkConfig &cfg) : config(cfg) {}

  bool connect() {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0)
      return false;

    // Set socket options
    int opt = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set timeout
    struct timeval tv;
    tv.tv_sec = config.timeoutMs / 1000;
    tv.tv_usec = (config.timeoutMs % 1000) * 1000;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Connect
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);

    struct hostent *host = gethostbyname(config.host.c_str());
    if (!host) {
      close(socketFd);
      socketFd = -1;
      return false;
    }

    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    if (::connect(socketFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(socketFd);
      socketFd = -1;
      return false;
    }

    return true;
  }

  void readLoop() {
    char buffer[8192];

    while (running.load()) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(socketFd, &readfds);

      struct timeval tv{1, 0}; // 1 second timeout
      int result = select(socketFd + 1, &readfds, nullptr, nullptr, &tv);

      if (result > 0 && FD_ISSET(socketFd, &readfds)) {
        ssize_t bytes = recv(socketFd, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
          buffer[bytes] = '\0';
          if (callback) {
            callback(NetworkEvent(NetworkEventType::DataReceived,
                                  std::string(buffer, bytes)));
          }
        } else if (bytes == 0) {
          // Connection closed
          if (callback) {
            callback(NetworkEvent(NetworkEventType::Disconnected));
          }
          break;
        } else {
          // Error
          if (callback) {
            callback(
                NetworkEvent(NetworkEventType::Error, "", strerror(errno)));
          }
          break;
        }
      }
    }
  }
};

// TcpServer::Impl definition
struct TcpServer::Impl {
  NetworkConfig config;
  NetworkCallback callback;
  std::atomic<bool> running{false};
  int serverFd = -1;
  std::thread acceptThread;
  std::map<int, std::thread> clientThreads;
  std::mutex clientMutex;
  std::atomic<int> nextClientId{1};

  explicit Impl(const NetworkConfig &cfg) : config(cfg) {}

  void acceptLoop() {
    while (running.load()) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(serverFd, &readfds);

      struct timeval tv{1, 0}; // 1 second timeout
      int result = select(serverFd + 1, &readfds, nullptr, nullptr, &tv);

      if (result > 0 && FD_ISSET(serverFd, &readfds)) {
        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd =
            accept(serverFd, (struct sockaddr *)&clientAddr, &clientLen);

        if (clientFd >= 0) {
          int clientId = nextClientId++;

          std::lock_guard<std::mutex> lock(clientMutex);
          clientThreads[clientId] = std::thread([this, clientFd, clientId]() {
            handleClient(clientFd, clientId);
          });

          if (callback) {
            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
            callback(NetworkEvent(NetworkEventType::Connected, clientIp, "",
                                  clientId));
          }
        }
      }
    }
  }

  void handleClient(int clientFd, int clientId) {
    char buffer[8192];

    while (running.load()) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(clientFd, &readfds);

      struct timeval tv{1, 0};
      int result = select(clientFd + 1, &readfds, nullptr, nullptr, &tv);

      if (result > 0 && FD_ISSET(clientFd, &readfds)) {
        ssize_t bytes = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
          buffer[bytes] = '\0';
          if (callback) {
            callback(NetworkEvent(NetworkEventType::DataReceived,
                                  std::string(buffer, bytes), "", clientId));
          }
        } else if (bytes == 0) {
          // Client disconnected
          if (callback) {
            callback(
                NetworkEvent(NetworkEventType::Disconnected, "", "", clientId));
          }
          break;
        } else {
          // Error
          if (callback) {
            callback(NetworkEvent(NetworkEventType::Error, "", strerror(errno),
                                  clientId));
          }
          break;
        }
      }
    }

    close(clientFd);

    std::lock_guard<std::mutex> lock(clientMutex);
    clientThreads.erase(clientId);
  }
};

// UdpSocket::Impl definition
struct UdpSocket::Impl {
  NetworkConfig config;
  NetworkCallback callback;
  std::atomic<bool> running{false};
  int socketFd = -1;
  std::thread receiveThread;

  explicit Impl(const NetworkConfig &cfg) : config(cfg) {}

  void receiveLoop() {
    char buffer[8192];
    struct sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);

    while (running.load()) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(socketFd, &readfds);

      struct timeval tv{1, 0};
      int result = select(socketFd + 1, &readfds, nullptr, nullptr, &tv);

      if (result > 0 && FD_ISSET(socketFd, &readfds)) {
        ssize_t bytes = recvfrom(socketFd, buffer, sizeof(buffer) - 1, 0,
                                 (struct sockaddr *)&senderAddr, &senderLen);
        if (bytes > 0) {
          buffer[bytes] = '\0';
          char senderIp[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, INET_ADDRSTRLEN);
          std::string data = std::string(buffer, bytes) + " from " + senderIp +
                             ":" + std::to_string(ntohs(senderAddr.sin_port));

          if (callback) {
            callback(NetworkEvent(NetworkEventType::DataReceived, data));
          }
        }
      }
    }
  }
};

// HttpClient::Impl definition
struct HttpClient::Impl {
  NetworkConfig config;
  NetworkCallback callback;
  std::map<std::string, std::string> defaultHeaders;
  std::string userAgent = "Havel-HttpClient/1.0";

  explicit Impl(const NetworkConfig &cfg) : config(cfg) {
    defaultHeaders["User-Agent"] = userAgent;
    defaultHeaders["Connection"] = "close";
  }

  std::string buildRequest(HttpClient::Method method, const std::string &path,
                           const std::string &body,
                           const std::map<std::string, std::string> &headers) {
    std::string request;

    // Request line
    switch (method) {
    case HttpClient::Method::GET:
      request += "GET ";
      break;
    case HttpClient::Method::POST:
      request += "POST ";
      break;
    case HttpClient::Method::PUT:
      request += "PUT ";
      break;
    case HttpClient::Method::DELETE:
      request += "DELETE ";
      break;
    case HttpClient::Method::HEAD:
      request += "HEAD ";
      break;
    case HttpClient::Method::OPTIONS:
      request += "OPTIONS ";
      break;
    }

    request += path + " HTTP/1.1\r\n";

    // Headers
    for (const auto &[key, value] : defaultHeaders) {
      request += key + ": " + value + "\r\n";
    }

    for (const auto &[key, value] : headers) {
      request += key + ": " + value + "\r\n";
    }

    // Content-Length for POST/PUT
    if ((method == HttpClient::Method::POST ||
         method == HttpClient::Method::PUT) &&
        !body.empty()) {
      request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    }

    request += "\r\n";

    // Body
    if (!body.empty()) {
      request += body;
    }

    return request;
  }

  HttpClient::HttpResponse parseResponse(const std::string &response) {
    HttpClient::HttpResponse result;

    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
      result.error = "Invalid response format";
      return result;
    }

    std::string headerSection = response.substr(0, headerEnd);
    std::string bodySection = response.substr(headerEnd + 4);

    // Parse status line
    size_t firstLineEnd = headerSection.find("\r\n");
    std::string statusLine = headerSection.substr(0, firstLineEnd);

    // Extract status code
    size_t space1 = statusLine.find(' ');
    size_t space2 = statusLine.find(' ', space1 + 1);
    if (space1 != std::string::npos && space2 != std::string::npos) {
      result.statusCode =
          std::stoi(statusLine.substr(space1 + 1, space2 - space1 - 1));
      result.statusText = statusLine.substr(space2 + 1);
    }

    // Parse headers
    std::string headers = headerSection.substr(firstLineEnd + 2);
    size_t pos = 0;
    while (pos < headers.length()) {
      size_t lineEnd = headers.find("\r\n", pos);
      if (lineEnd == std::string::npos)
        break;

      std::string line = headers.substr(pos, lineEnd - pos);
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        result.headers[key] = value;
      }

      pos = lineEnd + 2;
    }

    result.body = bodySection;
    return result;
  }
};

// NetworkManager::Impl definition
struct NetworkManager::Impl {
  std::map<int, std::unique_ptr<NetworkComponent>> components;
  std::atomic<int> nextComponentId{1};
  NetworkCallback globalCallback;
  NetworkStats stats;
  std::mutex componentsMutex;

  Impl() { stats.startTime = std::chrono::steady_clock::now(); }
};

// TcpClient Implementation
TcpClient::TcpClient(const NetworkConfig &config)
    : pImpl(std::make_unique<Impl>(config)) {}

TcpClient::~TcpClient() { stop(); }

bool TcpClient::start() {
  if (pImpl->running.load())
    return false;

  if (!pImpl->connect()) {
    if (pImpl->callback) {
      pImpl->callback(
          NetworkEvent(NetworkEventType::Error, "", "Failed to connect"));
    }
    return false;
  }

  pImpl->running.store(true);
  pImpl->readThread = std::thread([this]() { pImpl->readLoop(); });

  if (pImpl->callback) {
    pImpl->callback(NetworkEvent(NetworkEventType::Connected));
  }

  return true;
}

void TcpClient::stop() {
  if (!pImpl->running.load())
    return;

  pImpl->running.store(false);

  if (pImpl->readThread.joinable()) {
    pImpl->readThread.join();
  }

  if (pImpl->socketFd >= 0) {
    close(pImpl->socketFd);
    pImpl->socketFd = -1;
  }
}

bool TcpClient::isRunning() const { return pImpl->running.load(); }

void TcpClient::setCallback(NetworkCallback callback) {
  pImpl->callback = callback;
}

bool TcpClient::connect() { return pImpl->connect(); }

void TcpClient::disconnect() { stop(); }

bool TcpClient::send(const std::string &data) {
  if (pImpl->socketFd < 0)
    return false;

  ssize_t sent = ::send(pImpl->socketFd, data.c_str(), data.length(), 0);
  if (sent > 0 && pImpl->callback) {
    pImpl->callback(
        NetworkEvent(NetworkEventType::DataSent, std::string(data, sent)));
  }
  return sent == static_cast<ssize_t>(data.length());
}

bool TcpClient::sendAsync(const std::string &data) {
  return std::thread([this, data]() { send(data); }).detach(), true;
}

void TcpClient::setHost(const std::string &host) { pImpl->config.host = host; }

void TcpClient::setPort(int port) { pImpl->config.port = port; }

std::string TcpClient::getHost() const { return pImpl->config.host; }

int TcpClient::getPort() const { return pImpl->config.port; }

// TcpServer Implementation
TcpServer::TcpServer(const NetworkConfig &config)
    : pImpl(std::make_unique<Impl>(config)) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
  if (pImpl->running.load())
    return false;

  pImpl->serverFd = socket(AF_INET, SOCK_STREAM, 0);
  if (pImpl->serverFd < 0)
    return false;

  int opt = 1;
  setsockopt(pImpl->serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(pImpl->config.port);

  if (bind(pImpl->serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(pImpl->serverFd);
    pImpl->serverFd = -1;
    return false;
  }

  if (listen(pImpl->serverFd, 10) < 0) {
    close(pImpl->serverFd);
    pImpl->serverFd = -1;
    return false;
  }

  pImpl->running.store(true);
  pImpl->acceptThread = std::thread([this]() { pImpl->acceptLoop(); });

  return true;
}

void TcpServer::stop() {
  if (!pImpl->running.load())
    return;

  pImpl->running.store(false);

  if (pImpl->acceptThread.joinable()) {
    pImpl->acceptThread.join();
  }

  // Close all client connections
  {
    std::lock_guard<std::mutex> lock(pImpl->clientMutex);
    for (auto &[clientId, thread] : pImpl->clientThreads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    pImpl->clientThreads.clear();
  }

  if (pImpl->serverFd >= 0) {
    close(pImpl->serverFd);
    pImpl->serverFd = -1;
  }
}

bool TcpServer::isRunning() const { return pImpl->running.load(); }

void TcpServer::setCallback(NetworkCallback callback) {
  pImpl->callback = callback;
}

void TcpServer::broadcast(const std::string &data) {
  std::lock_guard<std::mutex> lock(pImpl->clientMutex);
  for (const auto &[clientId, thread] : pImpl->clientThreads) {
    // Send to all clients (implementation would need client socket storage)
  }
}

void TcpServer::sendToClient(int clientId, const std::string &data) {
  // Implementation would need client socket storage
}

void TcpServer::disconnectClient(int clientId) {
  // Implementation would need client socket storage
}

int TcpServer::getConnectedClientCount() const {
  std::lock_guard<std::mutex> lock(pImpl->clientMutex);
  return pImpl->clientThreads.size();
}

std::vector<int> TcpServer::getConnectedClientIds() const {
  std::lock_guard<std::mutex> lock(pImpl->clientMutex);
  std::vector<int> ids;
  for (const auto &[clientId, thread] : pImpl->clientThreads) {
    ids.push_back(clientId);
  }
  return ids;
}

// UdpSocket Implementation
UdpSocket::UdpSocket(const NetworkConfig &config)
    : pImpl(std::make_unique<Impl>(config)) {}

UdpSocket::~UdpSocket() { stop(); }

bool UdpSocket::start() {
  if (pImpl->running.load())
    return false;

  pImpl->socketFd = socket(AF_INET, SOCK_DGRAM, 0);
  if (pImpl->socketFd < 0)
    return false;

  int opt = 1;
  setsockopt(pImpl->socketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  pImpl->running.store(true);
  pImpl->receiveThread = std::thread([this]() { pImpl->receiveLoop(); });

  return true;
}

void UdpSocket::stop() {
  if (!pImpl->running.load())
    return;

  pImpl->running.store(false);

  if (pImpl->receiveThread.joinable()) {
    pImpl->receiveThread.join();
  }

  if (pImpl->socketFd >= 0) {
    close(pImpl->socketFd);
    pImpl->socketFd = -1;
  }
}

bool UdpSocket::isRunning() const { return pImpl->running.load(); }

void UdpSocket::setCallback(NetworkCallback callback) {
  pImpl->callback = callback;
}

bool UdpSocket::bind() {
  if (pImpl->socketFd < 0)
    return false;

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(pImpl->config.port);

  return ::bind(pImpl->socketFd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
}

bool UdpSocket::sendTo(const std::string &data, const std::string &host,
                       int port) {
  if (pImpl->socketFd < 0)
    return false;

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  struct hostent *hostent = gethostbyname(host.c_str());
  if (!hostent)
    return false;

  memcpy(&addr.sin_addr, hostent->h_addr, hostent->h_length);

  ssize_t sent = sendto(pImpl->socketFd, data.c_str(), data.length(), 0,
                        (struct sockaddr *)&addr, sizeof(addr));

  return sent == static_cast<ssize_t>(data.length());
}

bool UdpSocket::sendBroadcast(const std::string &data, int port) {
  if (pImpl->socketFd < 0)
    return false;

  int broadcast = 1;
  setsockopt(pImpl->socketFd, SOL_SOCKET, SO_BROADCAST, &broadcast,
             sizeof(broadcast));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_BROADCAST;
  addr.sin_port = htons(port);

  ssize_t sent = sendto(pImpl->socketFd, data.c_str(), data.length(), 0,
                        (struct sockaddr *)&addr, sizeof(addr));

  return sent == static_cast<ssize_t>(data.length());
}

void UdpSocket::setMulticastTTL(int ttl) {
  if (pImpl->socketFd >= 0) {
    setsockopt(pImpl->socketFd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
               sizeof(ttl));
  }
}

bool UdpSocket::joinMulticastGroup(const std::string &group) {
  if (pImpl->socketFd < 0)
    return false;

  struct ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = inet_addr(group.c_str());
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);

  return setsockopt(pImpl->socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                    sizeof(mreq)) == 0;
}

bool UdpSocket::leaveMulticastGroup(const std::string &group) {
  if (pImpl->socketFd < 0)
    return false;

  struct ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = inet_addr(group.c_str());
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);

  return setsockopt(pImpl->socketFd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq,
                    sizeof(mreq)) == 0;
}

// HttpClient Implementation
HttpClient::HttpClient(const NetworkConfig &config)
    : pImpl(std::make_unique<Impl>(config)) {}

HttpClient::~HttpClient() { stop(); }

bool HttpClient::start() {
  return true; // HTTP client doesn't need persistent connection
}

void HttpClient::stop() {
  // HTTP client doesn't need persistent connection
}

bool HttpClient::isRunning() const { return true; }

void HttpClient::setCallback(NetworkCallback callback) {
  pImpl->callback = callback;
}

HttpClient::HttpResponse
HttpClient::request(Method method, const std::string &path,
                    const std::string &body,
                    const std::map<std::string, std::string> &headers) {
  HttpResponse response;

  // Create TCP client
  TcpClient client(pImpl->config);
  client.setCallback(pImpl->callback);

  if (!client.start()) {
    response.error = "Failed to connect";
    return response;
  }

  // Build and send request
  std::string request = pImpl->buildRequest(method, path, body, headers);
  if (!client.send(request)) {
    response.error = "Failed to send request";
    client.stop();
    return response;
  }

  // Wait for response (simplified - in real implementation would wait for
  // proper response)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  client.stop();

  // Parse response (simplified - would need proper response collection)
  response.statusCode = 200;
  response.statusText = "OK";

  return response;
}

HttpClient::HttpResponse HttpClient::get(const std::string &path) {
  return request(Method::GET, path);
}

HttpClient::HttpResponse HttpClient::post(const std::string &path,
                                          const std::string &body) {
  return request(Method::POST, path, body);
}

HttpClient::HttpResponse HttpClient::put(const std::string &path,
                                         const std::string &body) {
  return request(Method::PUT, path, body);
}

HttpClient::HttpResponse HttpClient::del(const std::string &path) {
  return request(Method::DELETE, path);
}

void HttpClient::setDefaultHeader(const std::string &name,
                                  const std::string &value) {
  pImpl->defaultHeaders[name] = value;
}

void HttpClient::setUserAgent(const std::string &userAgent) {
  pImpl->userAgent = userAgent;
  pImpl->defaultHeaders["User-Agent"] = userAgent;
}

void HttpClient::setTimeout(int timeoutMs) {
  pImpl->config.timeoutMs = timeoutMs;
}

// NetworkManager Implementation
NetworkManager &NetworkManager::getInstance() {
  static NetworkManager instance;
  return instance;
}

int NetworkManager::createTcpClient(const NetworkConfig &config) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  int id = pImpl->nextComponentId++;
  pImpl->components[id] = std::make_unique<TcpClient>(config);

  if (pImpl->globalCallback) {
    pImpl->components[id]->setCallback(pImpl->globalCallback);
  }

  pImpl->stats.totalConnections++;
  return id;
}

int NetworkManager::createTcpServer(const NetworkConfig &config) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  int id = pImpl->nextComponentId++;
  pImpl->components[id] = std::make_unique<TcpServer>(config);

  if (pImpl->globalCallback) {
    pImpl->components[id]->setCallback(pImpl->globalCallback);
  }

  pImpl->stats.totalConnections++;
  return id;
}

int NetworkManager::createUdpSocket(const NetworkConfig &config) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  int id = pImpl->nextComponentId++;
  pImpl->components[id] = std::make_unique<UdpSocket>(config);

  if (pImpl->globalCallback) {
    pImpl->components[id]->setCallback(pImpl->globalCallback);
  }

  pImpl->stats.totalConnections++;
  return id;
}

int NetworkManager::createHttpClient(const NetworkConfig &config) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  int id = pImpl->nextComponentId++;
  pImpl->components[id] = std::make_unique<HttpClient>(config);

  if (pImpl->globalCallback) {
    pImpl->components[id]->setCallback(pImpl->globalCallback);
  }

  pImpl->stats.totalConnections++;
  return id;
}

bool NetworkManager::destroyComponent(int componentId) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  auto it = pImpl->components.find(componentId);
  if (it != pImpl->components.end()) {
    it->second->stop();
    pImpl->components.erase(it);
    pImpl->stats.activeConnections--;
    return true;
  }
  return false;
}

NetworkComponent *NetworkManager::getComponent(int componentId) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  auto it = pImpl->components.find(componentId);
  return it != pImpl->components.end() ? it->second.get() : nullptr;
}

void NetworkManager::setGlobalTimeout(int timeoutMs) {
  // Would need to update all components
}

void NetworkManager::setGlobalCallback(NetworkCallback callback) {
  std::lock_guard<std::mutex> lock(pImpl->componentsMutex);
  pImpl->globalCallback = callback;

  for (auto &[id, component] : pImpl->components) {
    component->setCallback(callback);
  }
}

bool NetworkManager::isPortOpen(const std::string &host, int port,
                                int timeoutMs) {
  TcpClient client;
  client.setHost(host);
  client.setPort(port);
  return client.connect();
}

std::string NetworkManager::getLocalIpAddress() {
  auto ips = getLocalIpAddresses();
  return ips.empty() ? "127.0.0.1" : ips[0];
}

std::vector<std::string> NetworkManager::getLocalIpAddresses() {
  std::vector<std::string> ips;

  struct ifaddrs *ifaddrs_ptr, *ifa;
  if (getifaddrs(&ifaddrs_ptr) == -1)
    return ips;

  for (ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr)
      continue;
    if (ifa->ifa_addr->sa_family != AF_INET)
      continue;

    struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);

    std::string ipStr(ip);
    if (ipStr != "127.0.0.1") {
      ips.push_back(ipStr);
    }
  }

  freeifaddrs(ifaddrs_ptr);
  return ips;
}

bool NetworkManager::isValidIpAddress(const std::string &ip) {
  struct sockaddr_in sa;
  return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
}

bool NetworkManager::isValidHostname(const std::string &hostname) {
  struct hostent *hostent = gethostbyname(hostname.c_str());
  return hostent != nullptr;
}

NetworkManager::NetworkStats NetworkManager::getStats() const {
  return pImpl->stats;
}

void NetworkManager::resetStats() {
  pImpl->stats = NetworkStats{};
  pImpl->stats.startTime = std::chrono::steady_clock::now();
}

} // namespace net
} // namespace havel
