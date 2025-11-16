#pragma once
#include <string>
#include <mutex>
#include <memory>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <sstream>

// Use std::format if C++20, otherwise fallback to fmt library
#ifdef __cpp_lib_format
    #include <format>
    namespace formatting = std;
#else
    #include <fmt/format.h>
    namespace formatting = fmt;
#endif

namespace havel {

class Logger {
public:
    enum Level { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL };

    static Logger& getInstance();

    // Initialize logger with timestamped file naming and configurations
    void initialize(bool useTimestampedFiles = true,
                   int logMaxPeriod = 3,  // 3 days default
                   bool coloredOutput = true); // Enable colored output by default

    // Initialize with configuration values from Configs system
    void initializeWithConfig(); // Uses Configs::Get() to read settings

    // This method is defined in the .cpp file to avoid circular dependency
    // with ConfigManager.hpp. The implementation includes ConfigManager internally.

    void setLogFile(const std::string& filename);
    void setLogLevel(Level level);
    void setColoredOutput(bool enabled); // Set if console output should be colored

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

    /**
     * Debug logging with printf-style formatting
     * Usage: Logger::debug("Value: {}, Name: {}", 42, "test");
     */
    template<typename... Args>
    void debug(const std::string& format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            log(LOG_DEBUG, format);
        } else {
            try {
#ifdef __cpp_lib_format
                log(LOG_DEBUG, std::vformat(format, std::make_format_args(args...)));
#else
                log(LOG_DEBUG, fmt::format(format, std::forward<Args>(args)...));
#endif
            } catch (const std::exception& e) {
                log(LOG_ERROR, "Logger format error in debug(): " + std::string(e.what()) +
                              " | Original format: " + format);
            }
        }
    }

    template<typename... Args>
    void info(const std::string& format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            log(LOG_INFO, format);
        } else {
            try {
#ifdef __cpp_lib_format
                log(LOG_INFO, std::vformat(format, std::make_format_args(args...)));
#else
                log(LOG_INFO, fmt::format(format, std::forward<Args>(args)...));
#endif
            } catch (const std::exception& e) {
                log(LOG_ERROR, "Logger format error in info(): " + std::string(e.what()) +
                              " | Original format: " + format);
            }
        }
    }

    template<typename... Args>
    void warning(const std::string& format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            log(LOG_WARNING, format);
        } else {
            try {
#ifdef __cpp_lib_format
                log(LOG_WARNING, std::vformat(format, std::make_format_args(args...)));
#else
                log(LOG_WARNING, fmt::format(format, std::forward<Args>(args)...));
#endif
            } catch (const std::exception& e) {
                log(LOG_ERROR, "Logger format error in warning(): " + std::string(e.what()) +
                              " | Original format: " + format);
            }
        }
    }

    template<typename... Args>
    void error(const std::string& format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            log(LOG_ERROR, format);
        } else {
            try {
#ifdef __cpp_lib_format
                log(LOG_ERROR, std::vformat(format, std::make_format_args(args...)));
#else
                log(LOG_ERROR, fmt::format(format, std::forward<Args>(args)...));
#endif
            } catch (const std::exception& e) {
                log(LOG_ERROR, "Logger format error in error(): " + std::string(e.what()) +
                              " | Original format: " + format);
                log(LOG_ERROR, format); // Fallback to unformatted message
            }
        }
    }

    template<typename... Args>
    void fatal(const std::string& format, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            log(LOG_FATAL, format);
        } else {
            try {
#ifdef __cpp_lib_format
                log(LOG_FATAL, std::vformat(format, std::make_format_args(args...)));
#else
                log(LOG_FATAL, fmt::format(format, std::forward<Args>(args)...));
#endif
            } catch (const std::exception& e) {
                log(LOG_ERROR, "Logger format error in fatal(): " + std::string(e.what()) +
                              " | Original format: " + format);
                log(LOG_FATAL, format); // Fallback to unformatted message
            }
        }
    }

private:
    Logger(); // make constructor private
    ~Logger(); // and destructor
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(Level level, const std::string& message);
    std::string getLevelString(Level level);
    std::string getCurrentTimestamp();
    std::string getFormattedTimestamp(); // For use in filename
    std::string getColorCode(Level level) const;
    std::string resetColorCode() const;
    std::string getLogDirectory() const;
    void cleanupOldLogs();
    void openNewLogFile();

    struct Impl;
    std::unique_ptr<Impl> pImpl;
    std::mutex mutex;
    Level currentLevel;
    bool consoleOutput;
    bool useTimestampedFiles = true;
    int logMaxPeriod = 3; // Maximum days to keep logs
    bool coloredOutput = true;

    // Color codes
    std::unordered_map<Level, std::string> colorCodes = {
        {LOG_DEBUG, "\033[36m"},    // Cyan
        {LOG_INFO, "\033[32m"},     // Green
        {LOG_WARNING, "\033[33m"},  // Yellow
        {LOG_ERROR, "\033[31m"},    // Red
        {LOG_FATAL, "\033[35m"}     // Magenta
    };
};

// Move macros outside the class
#define HAVEL_LOG_DEBUG(...) havel::Logger::getInstance().debug(__VA_ARGS__)
#define HAVEL_LOG_INFO(...)  havel::Logger::getInstance().info(__VA_ARGS__)
#define HAVEL_LOG_WARN(...)  havel::Logger::getInstance().warning(__VA_ARGS__)
#define HAVEL_LOG_ERROR(...) havel::Logger::getInstance().error(__VA_ARGS__)
#define HAVEL_LOG_FATAL(...) havel::Logger::getInstance().fatal(__VA_ARGS__)

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
template<typename... Args>
inline void debug(const std::string& format, Args&&... args) {
    Logger::getInstance().debug(format, std::forward<Args>(args)...);
}
template<typename... Args>
inline void info(const std::string& format, Args&&... args) {
    Logger::getInstance().info(format, std::forward<Args>(args)...);
}
template<typename... Args>
inline void warning(const std::string& format, Args&&... args) {
    Logger::getInstance().warning(format, std::forward<Args>(args)...);
}
template<typename... Args>
inline void error(const std::string& format, Args&&... args) {
    Logger::getInstance().error(format, std::forward<Args>(args)...);
}
template<typename... Args>
inline void fatal(const std::string& format, Args&&... args) {
    Logger::getInstance().fatal(format, std::forward<Args>(args)...);
}
template<typename... Args>
inline void warn(const std::string& format, Args&&... args) {
    Logger::getInstance().warning(format, std::forward<Args>(args)...);
}
inline void warn(const std::string& message) {
    Logger::getInstance().warning(message);
}
} // namespace havel