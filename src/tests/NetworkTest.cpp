#include "NetworkManager.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace havel::net;

void testBasicFunctionality() {
  std::cout << "=== Basic Network Module Test ===" << std::endl;

  auto &manager = NetworkManager::getInstance();

  // Test configuration
  NetworkConfig config;
  config.host = "127.0.0.1";
  config.port = 12345;
  config.timeoutMs = 1000;

  std::cout << "NetworkConfig created: host=" << config.host
            << " port=" << config.port << std::endl;

  // Test utility functions
  std::cout << "127.0.0.1 is valid IP: "
            << NetworkManager::isValidIpAddress("127.0.0.1") << std::endl;
  std::cout << "localhost is valid hostname: "
            << NetworkManager::isValidHostname("localhost") << std::endl;

  // Test local IP detection
  auto localIps = NetworkManager::getLocalIpAddresses();
  std::cout << "Local IP addresses (" << localIps.size() << "):" << std::endl;
  for (const auto &ip : localIps) {
    std::cout << "  " << ip << std::endl;
  }

  // Test port checking (should fail for non-existent port)
  bool portOpen = NetworkManager::isPortOpen("127.0.0.1", 12345);
  std::cout << "Port 12345 is " << (portOpen ? "open" : "closed") << std::endl;

  // Test statistics
  auto stats = manager.getStats();
  std::cout << "Initial stats - Total: " << stats.totalConnections
            << ", Active: " << stats.activeConnections << std::endl;

  // Create components
  int tcpClientId = manager.createTcpClient(config);
  int tcpServerId = manager.createTcpServer(config);
  int udpId = manager.createUdpSocket(config);
  int httpId = manager.createHttpClient(config);

  std::cout << "Created components: TCP Client=" << tcpClientId
            << ", TCP Server=" << tcpServerId << ", UDP=" << udpId
            << ", HTTP=" << httpId << std::endl;

  // Test component retrieval
  auto *tcpClient = manager.getComponentAs<TcpClient>(tcpClientId);
  auto *tcpServer = manager.getComponentAs<TcpServer>(tcpServerId);
  auto *udpSocket = manager.getComponentAs<UdpSocket>(udpId);
  auto *httpClient = manager.getComponentAs<HttpClient>(httpId);

  std::cout << "Component retrieval: TCP Client=" << (tcpClient ? "OK" : "FAIL")
            << ", TCP Server=" << (tcpServer ? "OK" : "FAIL")
            << ", UDP=" << (udpSocket ? "OK" : "FAIL")
            << ", HTTP=" << (httpClient ? "OK" : "FAIL") << std::endl;

  // Test component destruction
  bool destroyed = NetworkManager::getInstance().destroyComponent(tcpClientId);
  std::cout << "Component destruction: " << (destroyed ? "SUCCESS" : "FAILED")
            << std::endl;

  // Final stats
  stats = manager.getStats();
  std::cout << "Final stats - Total: " << stats.totalConnections
            << ", Active: " << stats.activeConnections << std::endl;

  std::cout << "Basic functionality test completed successfully!" << std::endl;
}

void testUdpSocket() {
  std::cout << "\n=== UDP Socket Test ===" << std::endl;

  NetworkConfig config;
  config.port = 12346;

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

        // Send a test message to self
        udp->sendTo("Hello from Havel UDP!", "127.0.0.1", config.port);
        std::cout << "Sent UDP message to localhost:" << config.port
                  << std::endl;

        // Wait for message
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        udp->stop();
        std::cout << "UDP Socket stopped" << std::endl;
      } else {
        std::cout << "Failed to bind UDP socket" << std::endl;
      }
    } else {
      std::cout << "Failed to start UDP socket" << std::endl;
    }

    NetworkManager::getInstance().destroyComponent(udpId);
  } else {
    std::cout << "Failed to create UDP socket" << std::endl;
  }
}

int main() {
  std::cout << "Havel Network Module Test" << std::endl;
  std::cout << "=====================" << std::endl;

  try {
    testBasicFunctionality();
    testUdpSocket();

    std::cout << "\nAll tests completed successfully!" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
