#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace havel {

struct Logger::Impl {
    std::ofstream logFile;
    std::string currentFilename;  // Track current log filename
    std::string currentDate;      // Track current date for timestamped files
};

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : pImpl(std::make_unique<Impl>())
    , currentLevel(LOG_INFO)
    , consoleOutput(true) {
    // Initialize with default settings
    initialize(true, 3, true);
}

Logger::~Logger() {
    if (pImpl->logFile.is_open()) {
        pImpl->logFile.close();
    }
}

void Logger::initialize(bool useTimestampedFiles, int logMaxPeriod, bool coloredOutput) {
    std::lock_guard<std::mutex> lock(mutex);
    this->useTimestampedFiles = useTimestampedFiles;
    this->logMaxPeriod = logMaxPeriod;
    this->coloredOutput = coloredOutput;

    // Set up the log file based on initialization preferences
    if (useTimestampedFiles) {
        openNewLogFile();  // Create new timestamped file
    } else {
        setLogFile("havel.log");
    }
}

void Logger::initializeWithConfig(bool useTimestamped, int logMaxPeriod, bool colorsEnabled) {
    initialize(useTimestamped, logMaxPeriod, colorsEnabled);
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex);
    if (pImpl->logFile.is_open()) {
        pImpl->logFile.close();
    }
    pImpl->logFile.open(filename, std::ios::app);
    pImpl->currentFilename = filename;
}

void Logger::setLogLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex);
    currentLevel = level;
}

void Logger::setColoredOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    coloredOutput = enabled;
}

void Logger::debug(const std::string& message)   { log(LOG_DEBUG, message); }
void Logger::info(const std::string& message)    { log(LOG_INFO, message); }
void Logger::warning(const std::string& message) { log(LOG_WARNING, message); }
void Logger::error(const std::string& message)   { log(LOG_ERROR, message); }
void Logger::fatal(const std::string& message)   { log(LOG_FATAL, message); }

void Logger::log(Level level, const std::string& message) {
    if (level < currentLevel) return;

    std::lock_guard<std::mutex> lock(mutex);

    // Update the log file if we're using timestamped files and the date has changed
    if (useTimestampedFiles) {
        std::string currentDate = getFormattedTimestamp().substr(0, 10); // YYYY-MM-DD
        if (pImpl->currentDate != currentDate) {
            pImpl->currentDate = currentDate;
            openNewLogFile();
        }
    }

    std::string timestamp = getCurrentTimestamp();
    std::string levelStr = getLevelString(level);
    std::string logMessage = timestamp + " [" + levelStr + "] " + message + "\n";

    // Write to file
    if (pImpl->logFile.is_open()) {
        pImpl->logFile << logMessage;
        pImpl->logFile.flush();
    }

    // Write to console with optional coloring
    if (consoleOutput) {
        if (coloredOutput) {
            std::cout << getColorCode(level) << logMessage << resetColorCode();
        } else {
            std::cout << logMessage;
        }
    }
}

std::string Logger::getLevelString(Level level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default: return "UNKNOWN";
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

std::string Logger::getFormattedTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d_%H-%M-%S");
    return ss.str();
}

std::string Logger::getColorCode(Level level) const {
    if (coloredOutput) {
        auto it = colorCodes.find(level);
        if (it != colorCodes.end()) {
            return it->second;
        }
    }
    return "";
}

std::string Logger::resetColorCode() const {
    if (coloredOutput) {
        return "\033[0m";
    }
    return "";
}

std::string Logger::getLogDirectory() const {
    // Get user's home directory and create .local/share/havel/logs path
    const char* home = std::getenv("HOME");
    if (!home) {
        // Fallback to current directory if HOME is not available
        return "./logs";
    }

    std::filesystem::path logDir(home);
    logDir /= ".local/share/havel/logs";

    // Create directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    return logDir.string();
}

void Logger::openNewLogFile() {
    if (!useTimestampedFiles) return;

    std::string currentDate = getFormattedTimestamp().substr(0, 10); // YYYY-MM-DD
    std::string filename = currentDate + ".log";

    // Create full path
    std::string logDir = getLogDirectory();
    std::string fullFilePath = logDir + "/" + filename;

    // Close current file if open
    if (pImpl->logFile.is_open()) {
        pImpl->logFile.close();
    }

    // Open new file
    pImpl->logFile.open(fullFilePath, std::ios::app);
    pImpl->currentFilename = fullFilePath;
    pImpl->currentDate = currentDate;

    // Clean up old logs after opening new file
    cleanupOldLogs();
}

void Logger::cleanupOldLogs() {
    if (logMaxPeriod <= 0) return; // Skip cleanup if logMaxPeriod is 0 or negative

    std::string logDir = getLogDirectory();
    std::filesystem::path logPath(logDir);

    auto now = std::chrono::system_clock::now();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(logPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                // Extract date from filename (YYYY-MM-DD)
                std::string filename = entry.path().filename().string();
                if (filename.length() >= 10) { // At least YYYY-MM-DD
                    std::string dateStr = filename.substr(0, 10);

                    // Parse the date
                    int year, month, day;
                    char dash1, dash2; // For the hyphens
                    std::istringstream dateStream(dateStr);
                    dateStream >> year >> dash1 >> month >> dash2 >> day;

                    if (dateStream && dash1 == '-' && dash2 == '-') {
                        // Create a time_point for the log file date
                        std::tm tm = {};
                        tm.tm_year = year - 1900; // tm_year is years since 1900
                        tm.tm_mon = month - 1;     // tm_mon is 0-11
                        tm.tm_mday = day;

                        std::time_t logTime = std::mktime(&tm);
                        if (logTime != -1) {
                            auto logTimePoint = std::chrono::system_clock::from_time_t(logTime);

                            // Calculate the difference in days
                            auto diff = std::chrono::duration_cast<std::chrono::hours>(
                                now - logTimePoint).count() / 24;

                            if (diff > logMaxPeriod) {
                                // Delete the old log file
                                std::filesystem::remove(entry.path());
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        // Fail silently to avoid causing issues if we can't clean up logs
        // In a real implementation, you might want to log this to a different stream
    }
}

} // namespace havel
