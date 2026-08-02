// HavelDaemon.cpp — Persistent daemon mode for fast script execution
// Keeps VM alive, accepts scripts over Unix socket

// System/standard headers FIRST (global scope)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstring>

// Project headers (may open namespaces)
#include "core/init/HavelDaemon.hpp"
#include "utils/Logger.hpp"
#include "core/util/Env.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "modules/HostModules.hpp"

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
    HavelDaemonImpl() : running_(false), vm_(nullptr) {}
    ~HavelDaemonImpl() { stop(); }

    bool start(const std::string& socketPath) {
        socketPath_ = socketPath;
        
        // Create Unix socket
        serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (serverFd_ < 0) {
            ::havel::error("Failed to create socket: {}", strerror(errno));
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

        // Remove existing socket file
        unlink(socketPath_.c_str());

        if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ::havel::error("Failed to bind socket: {}", strerror(errno));
            close(serverFd_);
            return false;
        }

        if (listen(serverFd_, 5) < 0) {
            ::havel::error("Failed to listen: {}", strerror(errno));
            close(serverFd_);
            return false;
        }

        running_ = true;
        
        // Initialize VM once
        if (!initVM()) {
            return false;
        }

        // Start accept thread
        acceptThread_ = std::thread(&HavelDaemonImpl::acceptLoop, this);
        workerThread_ = std::thread(&HavelDaemonImpl::workerLoop, this);

        ::havel::info("Havel daemon started on {}", socketPath_);
        return true;
    }

    void stop() {
        running_ = false;
        if (serverFd_ >= 0) {
            close(serverFd_);
            serverFd_ = -1;
        }
        unlink(socketPath_.c_str());
        
        if (acceptThread_.joinable()) acceptThread_.join();
        if (workerThread_.joinable()) workerThread_.join();
        
        if (vm_) {
            delete vm_;
            vm_ = nullptr;
        }
        ::havel::info("Havel daemon stopped");
    }

private:
    bool initVM() {
        try {
            vm_ = new havel::compiler::VM();
            
            // Initialize runtime modules (pure stdlib)
            ::havel::runtime::registerPureStdLib(*vm_);
            ::havel::stdlib::registerHostModules(*vm_);
            
            // Pre-load common modules into VM cache
            // This avoids loading them on first script execution
            preloadModules();
            
            ::havel::info("VM initialized and modules preloaded");
            return true;
        } catch (const std::exception& e) {
            ::havel::error("VM init failed: {}", e.what());
            return false;
        }
    }

    void preloadModules() {
        // The modules are loaded on-demand via VM's module loader
        // But we can trigger loading of core modules here
        if (vm_) {
            // Core modules that almost every script uses
            static const char* coreModules[] = {
                "std/math", "std/string", "std/array", "std/object",
                "std/fs", "std/process", "std/shell", "std/json"
            };
            
            for (const char* mod : coreModules) {
                try {
                    vm_->loadModule(mod);
                } catch (...) {
                    // Ignore preload failures
                }
            }
        }
    }

    void acceptLoop() {
        while (running_) {
            struct sockaddr_un clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(serverFd_, (struct sockaddr*)&clientAddr, &clientLen);
            
            if (clientFd < 0) {
                if (running_) {
                    ::havel::warn("Accept failed: {}", strerror(errno));
                }
                continue;
            }
            
            // Read request
            DaemonRequestData req;
            req.clientFd = clientFd;
            
            // Read protocol: [4-byte script path len][script path][4-byte content len][content][4-byte args count][args...]
            if (!readRequest(clientFd, req)) {
                close(clientFd);
                continue;
            }
            
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                requestQueue_.push(std::move(req));
            }
            queueCv_.notify_one();
        }
    }

    bool readRequest(int fd, DaemonRequestData& req) {
        uint32_t len;
        
        // Script path
        if (read(fd, &len, 4) != 4) return false;
        if (len > 4096) return false;
        req.scriptPath.resize(len);
        if (read(fd, &req.scriptPath[0], len) != (ssize_t)len) return false;
        
        // Script content
        if (read(fd, &len, 4) != 4) return false;
        if (len > 1024 * 1024) return false; // 1MB limit
        req.scriptContent.resize(len);
        if (len > 0 && read(fd, &req.scriptContent[0], len) != (ssize_t)len) return false;
        
        // Args
        if (read(fd, &len, 4) != 4) return false;
        if (len > 64) return false;
        req.args.resize(len);
        for (uint32_t i = 0; i < len; ++i) {
            if (read(fd, &len, 4) != 4) return false;
            if (len > 4096) return false;
            req.args[i].resize(len);
            if (read(fd, &req.args[i][0], len) != (ssize_t)len) return false;
        }
        
        return true;
    }

    void workerLoop() {
        while (running_) {
            DaemonRequestData req;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait(lock, [this] { return !requestQueue_.empty() || !running_; });
                if (!running_ && requestQueue_.empty()) break;
                if (requestQueue_.empty()) continue;
                req = std::move(requestQueue_.front());
                requestQueue_.pop();
            }
            
            processRequest(req);
        }
    }

    void processRequest(const DaemonRequestData& req) {
        DaemonResponseData resp;
        resp.exitCode = 0;
        
        // Capture stdout/stderr
        int stdoutPipe[2], stderrPipe[2];
        pipe(stdoutPipe);
        pipe(stderrPipe);
        
        int savedStdout = dup(STDOUT_FILENO);
        int savedStderr = dup(STDERR_FILENO);
        
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        
        // Execute script in VM
        try {
            if (!req.scriptContent.empty()) {
                // Execute from string
                resp.exitCode = executeString(req.scriptContent, req.args);
            } else if (!req.scriptPath.empty()) {
                // Execute from file
                resp.exitCode = executeFile(req.scriptPath, req.args);
            } else {
                resp.exitCode = 1;
                resp.stderr = "No script provided";
            }
        } catch (const std::exception& e) {
            resp.exitCode = 1;
            resp.stderr = std::string("Exception: ") + e.what();
        }
        
        // Restore stdout/stderr
        dup2(savedStdout, STDOUT_FILENO);
        dup2(savedStderr, STDERR_FILENO);
        close(savedStdout);
        close(savedStderr);
        
        // Read captured output
        char buf[4096];
        ssize_t n;
        while ((n = read(stdoutPipe[0], buf, sizeof(buf))) > 0) {
            resp.stdout.append(buf, n);
        }
        while ((n = read(stderrPipe[0], buf, sizeof(buf))) > 0) {
            resp.stderr.append(buf, n);
        }
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        
        // Send response
        sendResponse(req.clientFd, resp);
        close(req.clientFd);
    }

    int executeString(const std::string& source, const std::vector<std::string>& args) {
        if (!vm_) return 1;
        
        // Compile
        parser::Parser parser{{}};
        std::unique_ptr<ast::Program> program;
        try {
            program = parser.produceAST(source);
        } catch (const ::havel::LexError& e) {
            ::havel::error("Lexer error: {}", e.what());
            return 1;
        } catch (const ::havel::parser::ParseError& e) {
            ::havel::error("Parse error: {}", e.what());
            return 1;
        }
        
        if (!program || parser.hasErrors()) {
            for (const auto& err : parser.getErrors())
                ::havel::error("Parse error: {}", err.message);
            return 1;
        }
        
        ByteCompiler compiler;
        std::shared_ptr<BytecodeChunk> chunk;
        try {
            chunk = std::shared_ptr<BytecodeChunk>(compiler.compile(*program).release());
        } catch (const std::exception& e) {
            ::havel::error("Compilation error: {}", e.what());
            return 1;
        }
        
        // Execute
        return vm_->executePersistent(chunk->getFunction("__main__"), args);
    }

    int executeFile(const std::string& path, const std::vector<std::string>& args) {
        // Load and execute module
        if (!vm_) return 1;
        
        try {
            Value result = vm_->loadModule(path);
            // If module has __main__, execute it
            auto chunk = vm_->getModuleChunk(path);
            if (chunk) {
                return vm_->executePersistent(chunk->getFunction("__main__"), args);
            }
            return 0;
        } catch (const std::exception& e) {
            ::havel::error("Module execution error: {}", e.what());
            return 1;
        }
    }

    void sendResponse(int fd, const DaemonResponseData& resp) {
        uint32_t len;
        
        // Exit code
        write(fd, &resp.exitCode, 4);
        
        // Stdout
        len = resp.stdout.size();
        write(fd, &len, 4);
        if (len > 0) write(fd, resp.stdout.data(), len);
        
        // Stderr
        len = resp.stderr.size();
        write(fd, &len, 4);
        if (len > 0) write(fd, resp.stderr.data(), len);
    }

    std::string socketPath_;
    int serverFd_ = -1;
    std::atomic<bool> running_{false};
    havel::compiler::VM* vm_ = nullptr;
    
    std::thread acceptThread_;
    std::thread workerThread_;
    
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<DaemonRequestData> requestQueue_;
};

// C API
extern "C" {

HavelDaemonHandle* havel_daemon_create() {
    return new HavelDaemonImpl();
}

bool havel_daemon_start(HavelDaemonHandle* daemon, const char* socketPath) {
    if (!daemon || !socketPath) return false;
    return static_cast<HavelDaemonImpl*>(daemon)->start(socketPath);
}

void havel_daemon_stop(HavelDaemonHandle* daemon) {
    if (daemon) {
        static_cast<HavelDaemonImpl*>(daemon)->stop();
        delete daemon;
    }
}

} // extern "C"

} // namespace havel
