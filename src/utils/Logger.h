#pragma once
#include <string>
#include <fstream>
#include <mutex>

namespace havel {

class Logger {
public:
    enum Level { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL };
    
    Logger(const std::string& filename = "havel.log");
    ~Logger();
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);
    
    void setLevel(Level level);
    
private:
    void log(Level level, const std::string& message);
    
    std::ofstream logFile;
    Level logLevel = LOG_INFO;
    std::mutex logMutex;
};

} // namespace havel

// Global logger instance
extern havel::Logger lo;

class LoggerPaths {
public:
    static void EnsureLogDir();
};