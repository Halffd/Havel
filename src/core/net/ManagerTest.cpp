#include "NetworkManager.hpp"
#include <iostream>

using namespace havel::net;

int main() {
  std::cout << "NetworkManager Test" << std::endl;

  // Test singleton
  auto &manager = NetworkManager::getInstance();
  std::cout << "NetworkManager singleton works" << std::endl;

  // Test basic configuration
  NetworkConfig config;
  config.host = "127.0.0.1";
  config.port = 12347;
  config.timeoutMs = 1000;

  std::cout << "Configuration: host=" << config.host << " port=" << config.port
            << std::endl;

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

  // Test port checking
  bool portOpen = NetworkManager::isPortOpen("127.0.0.1", 12347);
  std::cout << "Port 12347 is " << (portOpen ? "open" : "closed") << std::endl;

  std::cout << "NetworkManager test completed successfully!" << std::endl;

  return 0;
}
