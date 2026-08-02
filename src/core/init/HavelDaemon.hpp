#pragma once

#ifdef __cplusplus
#include "havel-lang/compiler/vm/VM.hpp"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

namespace havel {

struct DaemonRequestData {
    std::string scriptPath;
    std::string scriptContent;
    std::vector<std::string> args;
    int clientFd;
};

struct DaemonResponseData {
    int exitCode;
    std::string stdout;
    std::string stderr;
};

class HavelDaemonImpl {
public:
    HavelDaemonImpl();
    ~HavelDaemonImpl();
    bool start(const std::string& socketPath);
    void stop();

private:
    void initVM();
    void preloadModules();
    void acceptLoop();
    bool readRequest(int fd, DaemonRequestData& req);
    void workerLoop();
    void processRequest(const DaemonRequestData& req);
    int executeString(const std::string& source, const std::vector<std::string>& args);
    int executeFile(const std::string& path, const std::vector<std::string>& args);
    void sendResponse(int fd, const DaemonResponseData& resp);

    std::string socketPath_;
    int serverFd_ = -1;
    std::atomic<bool> running_{false};
    compiler::VM* vm_ = nullptr;

    std::thread acceptThread_;
    std::thread workerThread_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<DaemonRequestData> requestQueue_;
};

using HavelDaemon = HavelDaemonImpl;

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for C API - in C++ it's the impl pointer
using HavelDaemonHandle = HavelDaemonImpl*;

HavelDaemonHandle havel_daemon_create();
bool havel_daemon_start(HavelDaemonHandle daemon, const char* socketPath);
void havel_daemon_stop(HavelDaemonHandle daemon);

#ifdef __cplusplus
}
#endif