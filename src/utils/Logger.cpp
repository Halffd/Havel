#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <fstream>

// Define the global logger instance
havel::Logger& lo = havel::Logger::getInstance();

namespace havel {

struct Logger::Impl {
    std::ofstream logFile;
};

Logger::Logger() 
    : pImpl(std::make_unique<Impl>())
    , currentLevel(Level::LOG_INFO)
    , consoleOutput(true) {
}

Logger::~Logger() = default;

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex);
    if (pImpl->logFile.is_open()) {
        pImpl->logFile.close();
    }
    pImpl->logFile.open(filename, std::ios::app);
}

void Logger::setLogLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex);
    currentLevel = level;
}

void Logger::debug(const std::string& message) {
    log(Level::LOG_DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(Level::LOG_INFO, message);
}

void Logger::warning(const std::string& message) {
    log(Level::LOG_WARNING, message);
}

void Logger::error(const std::string& message) {
    log(Level::LOG_ERROR, message);
}

void Logger::fatal(const std::string& message) {
    log(Level::LOG_FATAL, message);
}

void Logger::log(Level level, const std::string& message) {
    if (level < currentLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    std::string timestamp = getCurrentTimestamp();
    std::string levelStr = getLevelString(level);
    std::string logMessage = timestamp + " [" + levelStr + "] " + message + "\n";

    if (pImpl->logFile.is_open()) {
        pImpl->logFile << logMessage;
        pImpl->logFile.flush();
    }

    if (consoleOutput) {
        std::cout << logMessage;
    }
}

std::string Logger::getLevelString(Level level) {
    switch (level) {
        case Level::LOG_DEBUG:   return "DEBUG";
        case Level::LOG_INFO:    return "INFO";
        case Level::LOG_WARNING: return "WARNING";
        case Level::LOG_ERROR:   return "ERROR";
        case Level::LOG_FATAL:   return "FATAL";
        default:            return "UNKNOWN";
    }
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

} // namespace havel