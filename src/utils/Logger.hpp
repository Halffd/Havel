#pragma once
#include <string>
#include <mutex>
#include <memory>
#include <fstream>

namespace havel {

class Logger {
public:
    enum Level { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL };

    static Logger& getInstance();

    void setLogFile(const std::string& filename);
    void setLogLevel(Level level);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

private:
    Logger(); // make constructor private
    ~Logger(); // and destructor
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(Level level, const std::string& message);
    std::string getLevelString(Level level);
    std::string getCurrentTimestamp();

    struct Impl;
    std::unique_ptr<Impl> pImpl;
    std::mutex mutex;
    Level currentLevel;
    bool consoleOutput;
};

inline void log(const std::string& message) {
    Logger::getInstance().info(message);
}
inline void debug(const std::string& message) {
    Logger::getInstance().debug(message);
}
inline void info(const std::string& message) {
    Logger::getInstance().info(message);
}
inline void warning(const std::string& message) {
    Logger::getInstance().warning(message);
}
inline void error(const std::string& message) {
    Logger::getInstance().error(message);
}
inline void fatal(const std::string& message) {
    Logger::getInstance().fatal(message);
}

} // namespace havel
