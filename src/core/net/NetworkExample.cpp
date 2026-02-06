#include "NetworkManager.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace havel::net;

void exampleTcpClient() {
  std::cout << "=== TCP Client Example ===" << std::endl;

  NetworkConfig config;
  config.host = "httpbin.org";
  config.port = 80;
  config.timeoutMs = 5000;

  int clientId = NetworkManager::getInstance().createTcpClient(config);
  auto *client =
      NetworkManager::getInstance().getComponentAs<TcpClient>(clientId);

  if (client) {
    client->setCallback([](const NetworkEvent &event) {
      switch (event.type) {
      case NetworkEventType::Connected:
        std::cout << "Connected to server" << std::endl;
        break;
      case NetworkEventType::Disconnected:
        std::cout << "Disconnected from server" << std::endl;
        break;
      case NetworkEventType::DataReceived:
        std::cout << "Received: " << event.data << std::endl;
        break;
      case NetworkEventType::Error:
        std::cout << "Error: " << event.error << std::endl;
        break;
      default:
        break;
      }
    });

    if (client->start()) {
      std::cout << "TCP Client started" << std::endl;

      // Send HTTP GET request
      std::string request =
          "GET /get HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
      client->send(request);

      // Wait for response
      std::this_thread::sleep_for(std::chrono::seconds(2));

      client->stop();
    } else {
      std::cout << "Failed to start TCP client" << std::endl;
    }

    NetworkManager::getInstance().destroyComponent(clientId);
  }
}

void exampleTcpServer() {
  std::cout << "\n=== TCP Server Example ===" << std::endl;

  NetworkConfig config;
  config.port = 8888;

  int serverId = NetworkManager::getInstance().createTcpServer(config);
  auto *server =
      NetworkManager::getInstance().getComponentAs<TcpServer>(serverId);

  if (server) {
    server->setCallback([](const NetworkEvent &event) {
      switch (event.type) {
      case NetworkEventType::Connected:
        std::cout << "Client connected: " << event.socketId << std::endl;
        break;
      case NetworkEventType::Disconnected:
        std::cout << "Client disconnected: " << event.socketId << std::endl;
        break;
      case NetworkEventType::DataReceived:
        std::cout << "Received from client " << event.socketId << ": "
                  << event.data << std::endl;
        // Echo back to client
        // In a real implementation, you would store client sockets
        break;
      case NetworkEventType::Error:
        std::cout << "Error: " << event.error << std::endl;
        break;
      default:
        break;
      }
    });

    if (server->start()) {
      std::cout << "TCP Server started on port " << config.port << std::endl;
      std::cout << "Connect with: telnet localhost " << config.port
                << std::endl;
      std::cout << "Server will run for 10 seconds..." << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(10));

      server->stop();
      std::cout << "TCP Server stopped" << std::endl;
    } else {
      std::cout << "Failed to start TCP server" << std::endl;
    }

    NetworkManager::getInstance().destroyComponent(serverId);
  }
}

void exampleUdpSocket() {
  std::cout << "\n=== UDP Socket Example ===" << std::endl;

  NetworkConfig config;
  config.port = 9999;

  int udpId = NetworkManager::getInstance().createUdpSocket(config);
  auto *udp = NetworkManager::getInstance().getComponentAs<UdpSocket>(udpId);

  if (udp) {
    udp->setCallback([](const NetworkEvent &event) {
      if (event.type == NetworkEventType::DataReceived) {
        std::cout << "UDP received: " << event.data << std::endl;
      }
    });

    if (udp->start()) {
      if (udp->bind()) {
        std::cout << "UDP Socket bound to port " << config.port << std::endl;

        // Send a test message
        udp->sendTo("Hello from Havel UDP!", "127.0.0.1", config.port);
        std::cout << "Sent UDP message to localhost:" << config.port
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(2));
      } else {
        std::cout << "Failed to bind UDP socket" << std::endl;
      }

      udp->stop();
    } else {
      std::cout << "Failed to start UDP socket" << std::endl;
    }

    NetworkManager::getInstance().destroyComponent(udpId);
  }
}

void exampleHttpClient() {
  std::cout << "\n=== HTTP Client Example ===" << std::endl;

  NetworkConfig config;
  config.host = "httpbin.org";
  config.port = 80;
  config.timeoutMs = 5000;

  int httpId = NetworkManager::getInstance().createHttpClient(config);
  auto *http = NetworkManager::getInstance().getComponentAs<HttpClient>(httpId);

  if (http) {
    // Make a GET request
    auto response = http->get("/get");

    if (response.error.empty()) {
      std::cout << "HTTP Response:" << std::endl;
      std::cout << "Status: " << response.statusCode << " "
                << response.statusText << std::endl;
      std::cout << "Headers: " << response.headers.size() << std::endl;
      std::cout << "Body: " << response.body.substr(0, 200)
                << (response.body.length() > 200 ? "..." : "") << std::endl;
    } else {
      std::cout << "HTTP Error: " << response.error << std::endl;
    }

    // Make a POST request
    auto postResponse = http->post("/post", "Hello from Havel HTTP Client!");

    if (postResponse.error.empty()) {
      std::cout << "POST Response: " << postResponse.statusCode << " "
                << postResponse.statusText << std::endl;
    } else {
      std::cout << "POST Error: " << postResponse.error << std::endl;
    }

    NetworkManager::getInstance().destroyComponent(httpId);
  }
}

void exampleNetworkUtilities() {
  std::cout << "\n=== Network Utilities Example ===" << std::endl;

  // Check if port is open
  bool portOpen = NetworkManager::isPortOpen("google.com", 80);
  std::cout << "Google.com port 80 is " << (portOpen ? "open" : "closed")
            << std::endl;

  // Get local IP addresses
  auto localIps = NetworkManager::getLocalIpAddresses();
  std::cout << "Local IP addresses:" << std::endl;
  for (const auto &ip : localIps) {
    std::cout << "  " << ip << std::endl;
  }

  // Validate IP and hostname
  std::cout << "127.0.0.1 is valid IP: "
            << NetworkManager::isValidIpAddress("127.0.0.1") << std::endl;
  std::cout << "google.com is valid hostname: "
            << NetworkManager::isValidHostname("google.com") << std::endl;

  // Network statistics
  auto &manager = NetworkManager::getInstance();
  manager.resetStats();
  auto stats = manager.getStats();
  std::cout << "Network Stats:" << std::endl;
  std::cout << "  Total connections: " << stats.totalConnections << std::endl;
  std::cout << "  Active connections: " << stats.activeConnections << std::endl;
  std::cout << "  Bytes sent: " << stats.bytesSent << std::endl;
  std::cout << "  Bytes received: " << stats.bytesReceived << std::endl;
}

int main() {
  std::cout << "Havel Network Module Examples" << std::endl;
  std::cout << "=========================" << std::endl;

  try {
    exampleTcpClient();
    exampleTcpServer();
    exampleUdpSocket();
    exampleHttpClient();
    exampleNetworkUtilities();

    std::cout << "\nAll examples completed successfully!" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
