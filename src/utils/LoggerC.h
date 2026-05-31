#ifndef HAVEL_LOGGER_C_H
#define HAVEL_LOGGER_C_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HavelLoggerLevel {
    HAVEL_LOG_DEBUG   = 0,
    HAVEL_LOG_INFO    = 1,
    HAVEL_LOG_WARNING = 2,
    HAVEL_LOG_ERROR   = 3,
    HAVEL_LOG_FATAL   = 4
} HavelLoggerLevel;

typedef struct HavelLogger HavelLogger;

HavelLogger* HavelLogger_getInstance(void);

void HavelLogger_initialize(HavelLogger* logger, bool useTimestampedFiles, int logMaxPeriod, bool coloredOutput);
void HavelLogger_initializeWithConfig(HavelLogger* logger, bool useTimestamped, int logMaxPeriod, bool colorsEnabled);
void HavelLogger_setLogFile(HavelLogger* logger, const char* filename);
void HavelLogger_setLogLevel(HavelLogger* logger, HavelLoggerLevel level);
HavelLoggerLevel HavelLogger_getCurrentLevel(const HavelLogger* logger);
void HavelLogger_setColoredOutput(HavelLogger* logger, bool enabled);
void HavelLogger_setMaxHistorySize(HavelLogger* logger, size_t maxSize);
void HavelLogger_setMaxFileSize(HavelLogger* logger, size_t maxBytes);

void HavelLogger_getLogFilePath(const HavelLogger* logger, char* buf, size_t bufSize);

size_t HavelLogger_getHistory(const HavelLogger* logger, char** outEntries, size_t maxEntries, size_t maxLinesPerEntry);
void HavelLogger_freeHistoryEntries(char** entries, size_t count);

void HavelLogger_debug(HavelLogger* logger, const char* message);
void HavelLogger_info(HavelLogger* logger, const char* message);
void HavelLogger_warning(HavelLogger* logger, const char* message);
void HavelLogger_error(HavelLogger* logger, const char* message);
void HavelLogger_fatal(HavelLogger* logger, const char* message);
void HavelLogger_critical(HavelLogger* logger, const char* message);

void HavelLogger_logf(HavelLogger* logger, HavelLoggerLevel level, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

void HavelLogger_debugf(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void HavelLogger_infof(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void HavelLogger_warningf(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void HavelLogger_errorf(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void HavelLogger_fatalf(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

void HavelLogger_criticalf(HavelLogger* logger, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#ifdef __cplusplus
}
#endif

#endif // HAVEL_LOGGER_C_H
