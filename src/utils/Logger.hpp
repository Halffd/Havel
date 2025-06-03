#pragma once

#include <string>
#include <mutex>
#include <memory>

namespace havel {

class Logger {
public:
    enum class Level {
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARNING,
        LOG_ERROR,
        LOG_FATAL
    };

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setLogFile(const std::string& filename);
    void setLogLevel(Level level);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(Level level, const std::string& message);
    std::string getLevelString(Level level);
    std::string getCurrentTimestamp();

    struct Impl;
    std::unique_ptr<Impl> pImpl;
    Level currentLevel;
    std::mutex mutex;
    bool consoleOutput;
};

} // namespace havel

// Global logger instance declaration
extern havel::Logger& lo; 