// havel-client - Send scripts to running havel-daemon

// System/standard headers FIRST (global scope)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

// Project headers (may open namespaces)
#include "core/init/HavelDaemon.hpp"
#include "core/util/Env.hpp"

static int connectToDaemon(const std::string& socketPath) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to daemon at " << socketPath << ": " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    return fd;
}

static bool writeAll(int fd, const void* data, size_t len) {
    const char* ptr = static_cast<const char*>(data);
    while (len > 0) {
        ssize_t n = write(fd, ptr, len);
        if (n <= 0) return false;
        ptr += n;
        len -= n;
    }
    return true;
}

static bool readAll(int fd, void* data, size_t len) {
    char* ptr = static_cast<char*>(data);
    while (len > 0) {
        ssize_t n = read(fd, ptr, len);
        if (n <= 0) return false;
        ptr += n;
        len -= n;
    }
    return true;
}

static bool sendRequest(int fd, const std::string& scriptPath, const std::string& scriptContent, const std::vector<std::string>& args) {
    uint32_t len;
    
    // Script path
    len = scriptPath.size();
    if (!writeAll(fd, &len, 4)) return false;
    if (len > 0 && !writeAll(fd, scriptPath.data(), len)) return false;
    
    // Script content
    len = scriptContent.size();
    if (!writeAll(fd, &len, 4)) return false;
    if (len > 0 && !writeAll(fd, scriptContent.data(), len)) return false;
    
    // Args
    len = args.size();
    if (!writeAll(fd, &len, 4)) return false;
    for (const auto& arg : args) {
        uint32_t argLen = arg.size();
        if (!writeAll(fd, &argLen, 4)) return false;
        if (argLen > 0 && !writeAll(fd, arg.data(), argLen)) return false;
    }
    
    return true;
}

static bool readResponse(int fd, int& exitCode, std::string& stdout, std::string& stderr) {
    uint32_t len;
    
    // Exit code
    int32_t code;
    if (!readAll(fd, &code, 4)) return false;
    exitCode = code;
    
    // Stdout
    if (!readAll(fd, &len, 4)) return false;
    stdout.resize(len);
    if (len > 0 && !readAll(fd, &stdout[0], len)) return false;
    
    // Stderr
    if (!readAll(fd, &len, 4)) return false;
    stderr.resize(len);
    if (len > 0 && !readAll(fd, &stderr[0], len)) return false;
    
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: havel-client [--socket PATH] <script.hv> [args...]" << std::endl;
        std::cerr << "       havel-client [--socket PATH] -e \"code\" [args...]" << std::endl;
        return 1;
    }
    
    std::string socketPath = ::havel::Env::temp() + "/havel-daemon.sock";
    std::string scriptPath;
    std::string scriptContent;
    std::vector<std::string> args;
    bool evalMode = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socketPath = argv[++i];
        } else if (arg == "-e" || arg == "--eval") {
            evalMode = true;
            if (i + 1 < argc) {
                scriptContent = argv[++i];
            }
        } else if (evalMode && scriptContent.empty()) {
            scriptContent = arg;
        } else if (scriptPath.empty() && !evalMode) {
            scriptPath = arg;
        } else {
            args.push_back(arg);
        }
    }
    
    if (scriptPath.empty() && scriptContent.empty()) {
        std::cerr << "No script provided" << std::endl;
        return 1;
    }
    
    int fd = connectToDaemon(socketPath);
    if (fd < 0) return 1;
    
    if (!sendRequest(fd, scriptPath, scriptContent, args)) {
        std::cerr << "Failed to send request" << std::endl;
        close(fd);
        return 1;
    }
    
    int exitCode;
    std::string stdout, stderr;
    if (!readResponse(fd, exitCode, stdout, stderr)) {
        std::cerr << "Failed to read response" << std::endl;
        close(fd);
        return 1;
    }
    
    close(fd);
    
    if (!stdout.empty()) std::cout << stdout;
    if (!stderr.empty()) std::cerr << stderr;
    
    return exitCode;
}