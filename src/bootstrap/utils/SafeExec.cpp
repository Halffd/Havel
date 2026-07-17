#include "SafeExec.hpp"
#include "Logger.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#endif

namespace havel::utils {

#ifndef _WIN32

bool execDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;

    pid_t pid = fork();
    if (pid < 0) {
        ::havel::error("execDetached: fork() failed: {}", strerror(errno));
        return false;
    }
    if (pid > 0) {
        return true;
    }

    close(0);
    close(1);
    close(2);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    setsid();

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) {
        cargv.push_back(const_cast<char*>(a.c_str()));
    }
    cargv.push_back(nullptr);

    execvp(cargv[0], cargv.data());
    _exit(127);
}

std::optional<ExecResult> execSync(const std::vector<std::string>& argv) {
    if (argv.empty()) return std::nullopt;

    int pipe_out[2] = {-1, -1};
    int pipe_err[2] = {-1, -1};
    if (pipe(pipe_out) != 0 || pipe(pipe_err) != 0) {
        ::havel::error("execSync: pipe() failed: {}", strerror(errno));
        if (pipe_out[0] >= 0) { close(pipe_out[0]); close(pipe_out[1]); }
        if (pipe_err[0] >= 0) { close(pipe_err[0]); close(pipe_err[1]); }
        return std::nullopt;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_out[0]); close(pipe_out[1]);
        close(pipe_err[0]); close(pipe_err[1]);
        ::havel::error("execSync: fork() failed: {}", strerror(errno));
        return std::nullopt;
    }

    if (pid == 0) {
        close(pipe_out[0]);
        close(pipe_err[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_err[1], STDERR_FILENO);
        close(pipe_out[1]);
        close(pipe_err[1]);

        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) {
            cargv.push_back(const_cast<char*>(a.c_str()));
        }
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        _exit(127);
    }

    close(pipe_out[1]);
    close(pipe_err[1]);

    ExecResult result;
    char buf[4096];
    ssize_t n;

    while ((n = read(pipe_out[0], buf, sizeof(buf))) > 0) {
        result.stdout_output.append(buf, n);
    }
    close(pipe_out[0]);

    while ((n = read(pipe_err[0], buf, sizeof(buf))) > 0) {
        result.stderr_output.append(buf, n);
    }
    close(pipe_err[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            ::havel::error("execSync: waitpid() failed: {}", strerror(errno));
            return result;
        }
    }

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = -WTERMSIG(status);
    }

    return result;
}

std::optional<std::string> execCapture(const std::vector<std::string>& argv) {
    auto result = execSync(argv);
    if (!result) return std::nullopt;
    return result->stdout_output;
}

std::vector<pid_t> findProcessesByName(const std::string& name) {
    std::vector<pid_t> pids;
    DIR* dir = opendir("/proc");
    if (!dir) return pids;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        char* endp = nullptr;
        long pid_long = strtol(entry->d_name, &endp, 10);
        if (*endp != '\0' || pid_long <= 0) continue;

        std::string comm_path = "/proc/";
        comm_path += entry->d_name;
        comm_path += "/comm";

        std::ifstream f(comm_path);
        if (!f.is_open()) continue;

        std::string comm;
        if (std::getline(f, comm)) {
            size_t nl = comm.find('\n');
            if (nl != std::string::npos) comm.erase(nl);
            if (comm == name) {
                pids.push_back(static_cast<pid_t>(pid_long));
                continue;
            }
        }
        f.close();

        std::string cmdline_path = "/proc/";
        cmdline_path += entry->d_name;
        cmdline_path += "/cmdline";

        std::ifstream cf(cmdline_path, std::ios::binary);
        if (!cf.is_open()) continue;

        std::string cmdline;
        if (std::getline(cf, cmdline, '\0')) {
            size_t slash = cmdline.rfind('/');
            std::string basename = (slash != std::string::npos)
                ? cmdline.substr(slash + 1) : cmdline;
            if (basename == name) {
                pids.push_back(static_cast<pid_t>(pid_long));
            }
        }
    }
    closedir(dir);
    return pids;
}

bool processExistsByName(const std::string& name) {
    return !findProcessesByName(name).empty();
}

#else // _WIN32

bool execDetached(const std::vector<std::string>&) { return false; }
std::optional<ExecResult> execSync(const std::vector<std::string>&) { return std::nullopt; }
std::optional<std::string> execCapture(const std::vector<std::string>&) { return std::nullopt; }
std::vector<pid_t> findProcessesByName(const std::string&) { return {}; }
bool processExistsByName(const std::string&) { return false; }

#endif

} // namespace havel::utils
