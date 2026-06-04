#pragma once

#include "LoggerC.h"

#include <string>
#include <vector>

#ifdef __cpp_lib_format
#include <format>
#else
#include <fmt/format.h>
#endif

namespace havel {

class Logger {
public:
    enum Level {
        LOG_DEBUG   = HAVEL_LOG_DEBUG,
        LOG_INFO    = HAVEL_LOG_INFO,
        LOG_WARNING = HAVEL_LOG_WARNING,
        LOG_ERROR   = HAVEL_LOG_ERROR,
        LOG_FATAL   = HAVEL_LOG_FATAL
    };

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void initialize(bool useTimestampedFiles = true, int logMaxPeriod = 3, bool coloredOutput = true) {
        HavelLogger_initialize(handle(), useTimestampedFiles, logMaxPeriod, coloredOutput);
    }

    void initializeWithConfig(bool useTimestamped, int logMaxPeriod, bool colorsEnabled) {
        HavelLogger_initializeWithConfig(handle(), useTimestamped, logMaxPeriod, colorsEnabled);
    }

    void setLogFile(const std::string& filename) {
        HavelLogger_setLogFile(handle(), filename.c_str());
    }

    void setLogLevel(Level level) {
        HavelLogger_setLogLevel(handle(), static_cast<HavelLoggerLevel>(level));
    }

    Level getCurrentLevel() const {
        return static_cast<Level>(HavelLogger_getCurrentLevel(handle()));
    }

    void setColoredOutput(bool enabled) {
        HavelLogger_setColoredOutput(handle(), enabled);
    }

    void setMaxHistorySize(size_t maxSize) {
        HavelLogger_setMaxHistorySize(handle(), maxSize);
    }

    void setMaxFileSize(size_t maxBytes) {
        HavelLogger_setMaxFileSize(handle(), maxBytes);
    }

    std::string getLogFilePath() const {
        char buf[4096];
        HavelLogger_getLogFilePath(handle(), buf, sizeof(buf));
        return std::string(buf);
    }

    std::vector<std::string> getHistory(size_t maxLines = 100) const {
        std::vector<char*> entries(maxLines, nullptr);
        size_t count = HavelLogger_getHistory(handle(), entries.data(), maxLines, maxLines);
        std::vector<std::string> result;
        result.reserve(count);
        for (size_t i = 0; i < count; i++) {
            if (entries[i]) {
                result.emplace_back(entries[i]);
                std::free(entries[i]);
            }
        }
        return result;
    }

    void debug(const std::string& message) {
        HavelLogger_debug(handle(), message.c_str());
    }

    void info(const std::string& message) {
        HavelLogger_info(handle(), message.c_str());
    }

    void warning(const std::string& message) {
        HavelLogger_warning(handle(), message.c_str());
    }

    void error(const std::string& message) {
        HavelLogger_error(handle(), message.c_str());
    }

    void fatal(const std::string& message) {
        HavelLogger_fatal(handle(), message.c_str());
    }

    void critical(const std::string& message) {
        HavelLogger_critical(handle(), message.c_str());
    }

    template<typename... Args>
    void debug(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            HavelLogger_debug(handle(), fmt.c_str());
        } else {
            try {
#ifdef __cpp_lib_format
                auto msg = std::vformat(fmt, std::make_format_args(args...));
#else
                auto msg = fmt::vformat(fmt, fmt::make_format_args(args...));
#endif
                HavelLogger_debug(handle(), msg.c_str());
            } catch (const std::exception& e) {
                HavelLogger_errorf(handle(), "Logger format error in debug(): %s | Original format: %s", e.what(), fmt.c_str());
            }
        }
    }

    template<typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            HavelLogger_info(handle(), fmt.c_str());
        } else {
            try {
#ifdef __cpp_lib_format
                auto msg = std::vformat(fmt, std::make_format_args(args...));
#else
                auto msg = fmt::vformat(fmt, fmt::make_format_args(args...));
#endif
                HavelLogger_info(handle(), msg.c_str());
            } catch (const std::exception& e) {
                HavelLogger_errorf(handle(), "Logger format error in info(): %s | Original format: %s", e.what(), fmt.c_str());
            }
        }
    }

    template<typename... Args>
    void warning(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            HavelLogger_warning(handle(), fmt.c_str());
        } else {
            try {
#ifdef __cpp_lib_format
                auto msg = std::vformat(fmt, std::make_format_args(args...));
#else
                auto msg = fmt::vformat(fmt, fmt::make_format_args(args...));
#endif
                HavelLogger_warning(handle(), msg.c_str());
            } catch (const std::exception& e) {
                HavelLogger_errorf(handle(), "Logger format error in warning(): %s | Original format: %s", e.what(), fmt.c_str());
            }
        }
    }

    template<typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            HavelLogger_error(handle(), fmt.c_str());
        } else {
            try {
#ifdef __cpp_lib_format
                auto msg = std::vformat(fmt, std::make_format_args(args...));
#else
                auto msg = fmt::vformat(fmt, fmt::make_format_args(args...));
#endif
                HavelLogger_error(handle(), msg.c_str());
            } catch (const std::exception& e) {
                HavelLogger_errorf(handle(), "Logger format error in error(): %s | Original format: %s", e.what(), fmt.c_str());
                HavelLogger_error(handle(), fmt.c_str());
            }
        }
    }

    template<typename... Args>
    void fatal(const std::string& fmt, Args&&... args) {
        if constexpr (sizeof...(args) == 0) {
            HavelLogger_fatal(handle(), fmt.c_str());
        } else {
            try {
#ifdef __cpp_lib_format
                auto msg = std::vformat(fmt, std::make_format_args(args...));
#else
                auto msg = fmt::vformat(fmt, fmt::make_format_args(args...));
#endif
                HavelLogger_fatal(handle(), msg.c_str());
            } catch (const std::exception& e) {
                HavelLogger_errorf(handle(), "Logger format error in fatal(): %s | Original format: %s", e.what(), fmt.c_str());
                HavelLogger_fatal(handle(), fmt.c_str());
            }
        }
    }

    HavelLogger* cHandle() { return HavelLogger_getInstance(); }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    HavelLogger* handle() const { return HavelLogger_getInstance(); }
};

#undef HAVEL_LOG_DEBUG
#undef HAVEL_LOG_INFO
#undef HAVEL_LOG_WARN
#undef HAVEL_LOG_ERROR
#undef HAVEL_LOG_FATAL
#undef HAVEL_LOG_CRITICAL
#undef HAVEL_LOGF_DEBUG
#undef HAVEL_LOGF_INFO
#undef HAVEL_LOGF_WARN
#undef HAVEL_LOGF_ERROR
#undef HAVEL_LOGF_FATAL
#undef HAVEL_LOGF_CRITICAL
#undef HAVEL_LOGF

#define HAVEL_LOG_DEBUG(...) havel::Logger::getInstance().debug(__VA_ARGS__)
#define HAVEL_LOG_INFO(...) havel::Logger::getInstance().info(__VA_ARGS__)
#define HAVEL_LOG_WARN(...) havel::Logger::getInstance().warning(__VA_ARGS__)
#define HAVEL_LOG_ERROR(...) havel::Logger::getInstance().error(__VA_ARGS__)
#define HAVEL_LOG_FATAL(...) havel::Logger::getInstance().fatal(__VA_ARGS__)
#define HAVEL_LOG_CRITICAL(...) havel::Logger::getInstance().critical(__VA_ARGS__)

#define HAVEL_LOGF_DEBUG(fmt, ...) havel::Logger::getInstance().debug(fmt, __VA_ARGS__)
#define HAVEL_LOGF_INFO(fmt, ...) havel::Logger::getInstance().info(fmt, __VA_ARGS__)
#define HAVEL_LOGF_WARN(fmt, ...) havel::Logger::getInstance().warning(fmt, __VA_ARGS__)
#define HAVEL_LOGF_ERROR(fmt, ...) havel::Logger::getInstance().error(fmt, __VA_ARGS__)
#define HAVEL_LOGF_FATAL(fmt, ...) havel::Logger::getInstance().fatal(fmt, __VA_ARGS__)
#define HAVEL_LOGF_CRITICAL(fmt, ...) havel::Logger::getInstance().critical(fmt, __VA_ARGS__)

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
inline void debug(const std::string& fmt, Args&&... args) {
    Logger::getInstance().debug(fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void info(const std::string& fmt, Args&&... args) {
    Logger::getInstance().info(fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void warning(const std::string& fmt, Args&&... args) {
    Logger::getInstance().warning(fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void error(const std::string& fmt, Args&&... args) {
    Logger::getInstance().error(fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void fatal(const std::string& fmt, Args&&... args) {
    Logger::getInstance().fatal(fmt, std::forward<Args>(args)...);
}
template<typename... Args>
inline void warn(const std::string& fmt, Args&&... args) {
    Logger::getInstance().warning(fmt, std::forward<Args>(args)...);
}
inline void warn(const std::string& message) {
    Logger::getInstance().warning(message);
}
template<typename... Args>
inline void critical(const std::string& fmt, Args&&... args) {
    Logger::getInstance().fatal(fmt, std::forward<Args>(args)...);
}
inline void critical(const std::string& message) {
    Logger::getInstance().fatal(message);
}

} // namespace havel
