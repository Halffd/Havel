#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <netinet/in.h>
#include <unistd.h>

namespace havel {

class Server {
public:
    using MessageHandler = std::function<void(const std::string&)>;

    explicit Server(int port = 8888)
        : port(port), running(false) {}

    ~Server() { stop(); }

    bool start(MessageHandler handler) {
        if (running) return false;
        running = true;
        serverThread = std::thread([this, handler]() { run(handler); });
        return true;
    }

    void stop() {
        if (!running) return;
        running = false;
        if (serverThread.joinable())
            serverThread.join();
    }

    bool isRunning() const noexcept { return running; }

private:
    int port;
    std::atomic<bool> running;
    std::thread serverThread;

    void run(const MessageHandler &handler) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket");
            running = false;
            return;
        }

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(sockfd);
            running = false;
            return;
        }

        if (listen(sockfd, 8) < 0) {
            perror("listen");
            close(sockfd);
            running = false;
            return;
        }

        printf("Havel::Server listening on port %d\n", port);

        while (running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            timeval tv { 1, 0 }; // 1 sec timeout
            int res = select(sockfd + 1, &fds, nullptr, nullptr, &tv);
            if (res <= 0) continue;

            int client = accept(sockfd, nullptr, nullptr);
            if (client < 0) continue;

            std::thread([client, handler]() {
                char buffer[2048];
                ssize_t n = read(client, buffer, sizeof(buffer) - 1);
                if (n > 0) {
                    buffer[n] = '\0';
                    std::string msg(buffer);
                    handler(msg);
                }
                close(client);
            }).detach();
        }

        close(sockfd);
        printf("Havel::Server stopped\n");
    }
};

} // namespace Havel
