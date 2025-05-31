#pragma once
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>

namespace havel {

class SocketServer {
public:
    SocketServer();
    SocketServer(int port) : port(port) {}
    virtual ~SocketServer();
    
    bool Start(int port = 0);
    void Stop();
    bool IsRunning() const;
    
protected:
    virtual void HandleCommand(const std::string& cmd) = 0;
    
private:
    void ServerThread();
    
    std::thread serverThread;
    std::atomic<bool> running{false};
    int socketFd = -1;
    int port = 0;

    void RunServer() {
        int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) return;

        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/hv2.sock", sizeof(addr.sun_path)-1);

        unlink(addr.sun_path);
        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sockfd);
            return;
        }

        listen(sockfd, 5);

        while (running) {
            int client = accept(sockfd, nullptr, nullptr);
            if (client < 0) continue;

            char buffer[256];
            ssize_t count = read(client, buffer, sizeof(buffer)-1);
            if (count > 0) {
                buffer[count] = '\0';
                HandleCommand(buffer);
            }
            close(client);
        }
        close(sockfd);
    }
};

} // namespace havel 