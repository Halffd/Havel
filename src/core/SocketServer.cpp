#include "SocketServer.hpp"
#include <iostream>
#include <cstring>

namespace havel {

SocketServer::SocketServer() : port(0) {
}

SocketServer::~SocketServer() {
    Stop();
}

bool SocketServer::Start(int requestedPort) {
    if (running) {
        return false; // Already running
    }
    
    if (requestedPort > 0) {
        port = requestedPort;
    }
    
    running = true;
    serverThread = std::thread(&SocketServer::RunServer, this);
    return true;
}

void SocketServer::Stop() {
    if (!running) {
        return;
    }
    
    running = false;
    
    // Close socket to interrupt accept() call
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
    
    // Join thread if it's running
    if (serverThread.joinable()) {
        serverThread.join();
    }
}

bool SocketServer::IsRunning() const {
    return running;
}

void SocketServer::ServerThread() {
    RunServer();
}

} // namespace H 