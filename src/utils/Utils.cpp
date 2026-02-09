#include "Utils.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <linux/limits.h> // For PATH_MAX
#include <pwd.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace havel {

// Timer management
static std::unordered_map<int, std::unique_ptr<std::thread>> timerThreads;
static std::unordered_map<int, std::atomic<bool>> timerFlags;
static std::atomic<int> nextTimerId{1};
static std::mutex timerMutex;

int setTimeout(const std::function<void()> &callback, int delayMs) {
  std::lock_guard<std::mutex> lock(timerMutex);
  int timerId = nextTimerId++;

  timerThreads[timerId] =
      std::make_unique<std::thread>([callback, delayMs, timerId]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        if (timerFlags[timerId].load()) {
          callback();
        }
        // Clean up
        std::lock_guard<std::mutex> innerLock(timerMutex);
        timerThreads.erase(timerId);
        timerFlags.erase(timerId);
      });

  timerFlags[timerId] = true;
  return timerId;
}

int setInterval(const std::function<void()> &callback, int intervalMs) {
  std::lock_guard<std::mutex> lock(timerMutex);
  int timerId = nextTimerId++;

  timerFlags[timerId] = true;
  timerThreads[timerId] =
      std::make_unique<std::thread>([callback, intervalMs, timerId]() {
        while (timerFlags[timerId].load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
          if (timerFlags[timerId].load()) {
            callback();
          }
        }
        // Clean up when stopped
        std::lock_guard<std::mutex> innerLock(timerMutex);
        timerThreads.erase(timerId);
        timerFlags.erase(timerId);
      });

  return timerId;
}

void stopInterval(int timerId) {
  std::lock_guard<std::mutex> lock(timerMutex);
  auto it = timerFlags.find(timerId);
  if (it != timerFlags.end()) {
    it->second = false;
  }
}

std::string ToLower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::string ToUpper(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

void Trim(std::string &str) {
  // Trim leading spaces
  str.erase(str.begin(),
            std::find_if(str.begin(), str.end(),
                         [](unsigned char ch) { return !std::isspace(ch); }));

  // Trim trailing spaces
  str.erase(std::find_if(str.rbegin(), str.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            str.end());
}

std::string TrimCopy(const std::string &str) {
  std::string result = str;
  Trim(result);
  return result;
}

void RemoveChars(std::string &str, const std::string &chars) {
  str.erase(std::remove_if(str.begin(), str.end(),
                           [&chars](char c) {
                             return chars.find(c) != std::string::npos;
                           }),
            str.end());
}

std::string GetExecutablePath() {
#ifdef __linux__
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
#else
  return "";
#endif
}

std::string GetExecutableDir() {
  return std::filesystem::path(GetExecutablePath()).parent_path().string();
}

std::string GetCurrentDir() { return std::filesystem::current_path().string(); }

bool IsElevated() {
#ifdef __linux__
  return geteuid() == 0;
#else
  return false;
#endif
}

void ElevateProcess() {
#ifdef __linux__
  if (!IsElevated()) {
    // Relaunch with sudo
    std::string path = GetExecutablePath();
    execl("/usr/bin/sudo", "sudo", path.c_str(), nullptr);
  }
#endif
}

void SetProcessPriority(int priority) {
#ifdef __linux__
  nice(priority);
#endif
}

} // namespace havel