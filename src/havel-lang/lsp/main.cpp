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

  std::cerr << "LSP: Starting..." << std::endl;

  try {
    havel::lsp::LanguageServer server;
    server.run();
  } catch (const std::exception& e) {
    std::cerr << "LSP: Fatal error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
