// havel-daemon main entry point

// System/standard headers FIRST (global scope)
#include <signal.h>
#include <iostream>
#include <string>

// Project headers (may open namespaces)
#include "core/init/HavelDaemon.hpp"
#include "utils/Logger.hpp"
#include "core/util/Env.hpp"

static havel::HavelDaemonHandle g_daemon = nullptr;

void signalHandler(int sig) {
    if (g_daemon) {
        std::cout << "\nShutting down daemon..." << std::endl;
        havel_daemon_stop(g_daemon);
        g_daemon = nullptr;
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    ::havel::Logger::getInstance().initialize();
    
    std::string socketPath = ::havel::Env::temp() + "/havel-daemon.sock";
    
    if (argc > 1) {
        socketPath = argv[1];
    }
    
    std::cout << "Starting Havel daemon on " << socketPath << std::endl;
    
    g_daemon = havel_daemon_create();
    if (!g_daemon) {
        std::cerr << "Failed to create daemon" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    if (!havel_daemon_start(g_daemon, socketPath.c_str())) {
        std::cerr << "Failed to start daemon on " << socketPath << std::endl;
        havel_daemon_stop(g_daemon);
        return 1;
    }
    
    std::cout << "Daemon running on " << socketPath << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // Wait for signal
    while (g_daemon) {
        sleep(1);
    }
    
    return 0;
}