#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace havel {
namespace net {

// Forward declarations
class TcpClient;
class TcpServer;
class UdpSocket;
class HttpClient;
class NetworkEvent;

/**
 * Network event types for callbacks
 */
enum class NetworkEventType {
    Connected,
    Disconnected,
    DataReceived,
    DataSent,
    Error,
    Timeout
};

/**
 * Network event structure
 */
struct NetworkEvent {
    NetworkEventType type;
    std::string data;
    std::string error;
    int socketId;
    std::chrono::steady_clock::time_point timestamp;
    
    NetworkEvent(NetworkEventType t, const std::string& d = "", const std::string& err = "", int id = -1)
        : type(t), data(d), error(err), socketId(id), timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * Network event callback type
 */
using NetworkCallback = std::function<void(const NetworkEvent&)>;

/**
 * Network configuration
 */
struct NetworkConfig {
    std::string host = "localhost";
    int port = 8080;
    int timeoutMs = 5000;
    int maxRetries = 3;
    bool keepAlive = true;
    bool reuseAddr = true;
    int bufferSize = 8192;
};

/**
 * Abstract base class for network components
 */
class NetworkComponent {
public:
    virtual ~NetworkComponent() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual void setCallback(NetworkCallback callback) = 0;
};

/**
 * TCP Client class for outgoing connections
 */
class TcpClient : public NetworkComponent {
public:
    explicit TcpClient(const NetworkConfig& config = NetworkConfig{});
    ~TcpClient() override;
    
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    void setCallback(NetworkCallback callback) override;
    
    bool connect();
    void disconnect();
    bool send(const std::string& data);
    bool sendAsync(const std::string& data);
    
    void setHost(const std::string& host);
    void setPort(int port);
    std::string getHost() const;
    int getPort() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * TCP Server class for incoming connections
 */
class TcpServer : public NetworkComponent {
public:
    explicit TcpServer(const NetworkConfig& config = NetworkConfig{});
    ~TcpServer() override;
    
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    void setCallback(NetworkCallback callback) override;
    
    void broadcast(const std::string& data);
    void sendToClient(int clientId, const std::string& data);
    void disconnectClient(int clientId);
    
    int getConnectedClientCount() const;
    std::vector<int> getConnectedClientIds() const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * UDP Socket class for UDP communication
 */
class UdpSocket : public NetworkComponent {
public:
    explicit UdpSocket(const NetworkConfig& config = NetworkConfig{});
    ~UdpSocket() override;
    
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    void setCallback(NetworkCallback callback) override;
    
    bool bind();
    bool sendTo(const std::string& data, const std::string& host, int port);
    bool sendBroadcast(const std::string& data, int port);
    
    void setMulticastTTL(int ttl);
    bool joinMulticastGroup(const std::string& group);
    bool leaveMulticastGroup(const std::string& group);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * HTTP Client class for HTTP requests
 */
class HttpClient : public NetworkComponent {
public:
    enum class Method {
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS
    };
    
    struct HttpResponse {
        int statusCode = 0;
        std::string statusText;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string error;
    };
    
    explicit HttpClient(const NetworkConfig& config = NetworkConfig{});
    ~HttpClient() override;
    
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    void setCallback(NetworkCallback callback) override;
    
    HttpResponse request(Method method, const std::string& path, 
                        const std::string& body = "", 
                        const std::map<std::string, std::string>& headers = {});
    
    HttpResponse get(const std::string& path);
    HttpResponse post(const std::string& path, const std::string& body = "");
    HttpResponse put(const std::string& path, const std::string& body = "");
    HttpResponse del(const std::string& path);
    
    void setDefaultHeader(const std::string& name, const std::string& value);
    void setUserAgent(const std::string& userAgent);
    void setTimeout(int timeoutMs);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * Network Manager - Central management for all network operations
 */
class NetworkManager {
public:
    static NetworkManager& getInstance();
    
    // Component management
    int createTcpClient(const NetworkConfig& config = NetworkConfig{});
    int createTcpServer(const NetworkConfig& config = NetworkConfig{});
    int createUdpSocket(const NetworkConfig& config = NetworkConfig{});
    int createHttpClient(const NetworkConfig& config = NetworkConfig{});
    
    bool destroyComponent(int componentId);
    NetworkComponent* getComponent(int componentId);
    
    // Template methods for type-safe access
    template<typename T>
    T* getComponentAs(int componentId);
    
    // Global configuration
    void setGlobalTimeout(int timeoutMs);
    void setGlobalCallback(NetworkCallback callback);
    
    // Utility methods
    static bool isPortOpen(const std::string& host, int port, int timeoutMs = 1000);
    static std::string getLocalIpAddress();
    static std::vector<std::string> getLocalIpAddresses();
    static bool isValidIpAddress(const std::string& ip);
    static bool isValidHostname(const std::string& hostname);
    
    // Network statistics
    struct NetworkStats {
        int totalConnections = 0;
        int activeConnections = 0;
        long bytesSent = 0;
        long bytesReceived = 0;
        std::chrono::steady_clock::time_point startTime;
    };
    
    NetworkStats getStats() const;
    void resetStats();
    
private:
    NetworkManager() = default;
    ~NetworkManager() = default;
    
    struct Impl;
    std::unique_ptr<Impl> pImpl;
    
    // Delete copy constructor and assignment operator
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;
};

// Template implementation
template<typename T>
T* NetworkManager::getComponentAs(int componentId) {
    auto* component = getComponent(componentId);
    return dynamic_cast<T*>(component);
}

} // namespace net
} // namespace havel
