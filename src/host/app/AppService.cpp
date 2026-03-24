/*
 * AppService.cpp - Application lifecycle and system info
 */
#include "AppService.hpp"

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/utsname.h>
#include <fstream>
#include <sstream>
#include <pwd.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

namespace havel::host {

struct AppService::Impl {
  std::string appName = "Havel";
  std::string appVersion = "1.0.0";

  std::string get_app_dir() const {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
      result[count] = '\0';
      std::string path(result);
      // Return directory part
      size_t pos = path.rfind('/');
      if (pos != std::string::npos) {
        return path.substr(0, pos);
      }
      return path;
    }
    return ".";
  }

  std::string get_os() const {
#ifdef __linux__
    // Try to read from /etc/os-release
    std::ifstream file("/etc/os-release");
    if (file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        if (line.find("PRETTY_NAME=") == 0) {
          // Extract value between quotes
          size_t start = line.find('"');
          size_t end = line.rfind('"');
          if (start != std::string::npos && end != std::string::npos &&
              end > start) {
            return line.substr(start + 1, end - start - 1);
          }
        }
      }
    }

    // Fallback to uname
    struct utsname buffer;
    if (uname(&buffer) == 0) {
      return std::string(buffer.sysname) + " " + buffer.release;
    }
    return "Linux";
#else
    return "Unknown";
#endif
  }

  std::string get_hostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
      return std::string(hostname);
    }
    return "unknown";
  }

  std::string get_username() const {
    const char *user = std::getenv("USER");
    if (user) {
      return std::string(user);
    }
    user = std::getenv("LOGNAME");
    if (user) {
      return std::string(user);
    }
    return "unknown";
  }

  std::string get_home_dir() const {
    const char *home = std::getenv("HOME");
    if (home) {
      return std::string(home);
    }
    return std::string(getpwuid(getuid())->pw_dir);
  }

  int get_cpu_cores() const {
#ifdef _SC_NPROCESSORS_ONLN
    return sysconf(_SC_NPROCESSORS_ONLN);
#else
    return 1;
#endif
  }

  uint64_t get_total_memory() const {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
      return info.totalram * info.mem_unit;
    }
#endif
    return 0;
  }
};

AppService::AppService() : impl_(new Impl()) {}

AppService::~AppService() { delete impl_; }

std::string AppService::getAppName() const { return impl_->appName; }

std::string AppService::getAppVersion() const { return impl_->appVersion; }

std::string AppService::getAppDir() const { return impl_->get_app_dir(); }

SystemInfo AppService::getSystemInfo() const {
  SystemInfo info;
  info.os = impl_->get_os();
  info.osVersion = "";  // Could parse from os string
  info.hostname = impl_->get_hostname();
  info.username = impl_->get_username();
  info.homeDir = impl_->get_home_dir();
  info.cpuCores = impl_->get_cpu_cores();
  info.totalMemory = impl_->get_total_memory();
  return info;
}

std::string AppService::getOS() const { return impl_->get_os(); }

std::string AppService::getHostname() const { return impl_->get_hostname(); }

std::string AppService::getUsername() const { return impl_->get_username(); }

std::string AppService::getHomeDir() const { return impl_->get_home_dir(); }

int AppService::getCpuCores() const { return impl_->get_cpu_cores(); }

uint64_t AppService::getTotalMemory() const { return impl_->get_total_memory(); }

std::string AppService::getEnv(const std::string &name) const {
  const char *value = std::getenv(name.c_str());
  return value ? std::string(value) : "";
}

bool AppService::setEnv(const std::string &name, const std::string &value) {
  return setenv(name.c_str(), value.c_str(), 1) == 0;
}

std::vector<std::string> AppService::getEnvVars() const {
  std::vector<std::string> vars;
  extern char **environ;
  for (char **env = environ; *env; ++env) {
    vars.push_back(std::string(*env));
  }
  return vars;
}

bool AppService::openUrl(const std::string &url) {
  // Use xdg-open on Linux
  std::string cmd = "xdg-open '" + url + "' 2>/dev/null &";
  return system(cmd.c_str()) == 0;
}

bool AppService::openFile(const std::string &path) {
  std::string cmd = "xdg-open '" + path + "' 2>/dev/null &";
  return system(cmd.c_str()) == 0;
}

bool AppService::showInFolder(const std::string &path) {
  std::string cmd = "xdg-open '" + path + "' 2>/dev/null &";
  return system(cmd.c_str()) == 0;
}

bool AppService::copyToClipboard(const std::string &text) {
  // Use xclip or xsel
  std::string cmd = "echo '" + text + "' | xclip -selection clipboard 2>/dev/null";
  return system(cmd.c_str()) == 0;
}

std::string AppService::getClipboardText() const {
  // Use xclip to get clipboard content
  std::string cmd = "xclip -selection clipboard -o 2>/dev/null";
  std::array<char, 128> buffer;
  std::string result;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }
  pclose(pipe);
  return result;
}

} // namespace havel::host
