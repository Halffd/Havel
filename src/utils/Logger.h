#pragma once
#include <string>
#include <fstream>
#include <mutex>

namespace havel {

class Logger {
public:
    enum Level { DEBUG, INFO, WARNING, ERROR, FATAL };
    
    Logger(const std::string& filename = "hvc.log");
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
    Level logLevel = INFO;
    std::mutex logMutex;
};

} // namespace H

// Global logger instance
extern havel::Logger lo;

class LoggerPaths {
public:
    static void EnsureLogDir();
};
