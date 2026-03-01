#include "lsp/LanguageServer.hpp"
#include "utils/Logger.hpp"
#include <iostream>
#include <csignal>

void signalHandler(int signum) {
  havel::info("LSP: Received signal {}, shutting down", signum);
  std::exit(signum);
}

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  
  // Set up signal handlers
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  
  // Initialize logger (stderr for LSP, don't interfere with stdio)
  havel::Logger::getInstance().initialize(false, 3, false);
  havel::Logger::getInstance().setLogLevel(havel::Logger::LOG_INFO);
  
  havel::info("LSP: Havel Language Server starting");
  
  try {
    havel::lsp::LanguageServer server;
    server.run();
  } catch (const std::exception& e) {
    havel::error("LSP: Fatal error: {}", e.what());
    return 1;
  }
  
  havel::info("LSP: Server shutdown complete");
  return 0;
}
