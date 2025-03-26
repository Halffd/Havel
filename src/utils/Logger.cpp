#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <sstream>

// Define the global logger instance
H::Logger& lo = H::Logger::getInstance();

namespace H {

Logger::Logger() : currentLevel(Level::INFO), consoleOutput(true) {
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile.close();
    }
    logFile.open(filename, std::ios::app);
}

void Logger::setLogLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex);
    currentLevel = level;
}

void Logger::debug(const std::string& message) {
    log(Level::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(Level::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(Level::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(Level::ERROR, message);
}

void Logger::fatal(const std::string& message) {
    log(Level::FATAL, message);
}

void Logger::log(Level level, const std::string& message) {
    if (level < currentLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    std::string timestamp = getCurrentTimestamp();
    std::string levelStr = getLevelString(level);
    std::string logMessage = timestamp + " [" + levelStr + "] " + message + "\n";

    if (logFile.is_open()) {
        logFile << logMessage;
        logFile.flush();
    }

    if (consoleOutput) {
        std::cout << logMessage;
    }
}

std::string Logger::getLevelString(Level level) {
    switch (level) {
        case Level::DEBUG:   return "DEBUG";
        case Level::INFO:    return "INFO";
        case Level::WARNING: return "WARNING";
        case Level::ERROR:   return "ERROR";
        case Level::FATAL:   return "FATAL";
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

} // namespace H