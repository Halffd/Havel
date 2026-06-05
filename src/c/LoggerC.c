#include "LoggerC.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#define HAVEL_LOG_BUF_SIZE 8192
#define HAVEL_PATH_BUF_SIZE 4096
#define HAVEL_HISTORY_DEFAULT_MAX 200
#define HAVEL_FILE_SIZE_DEFAULT (10 * 1024 * 1024)
#define HAVEL_FLUSH_INTERVAL 64

typedef struct {
    FILE* logFile;
    char  currentFilename[HAVEL_PATH_BUF_SIZE];
    char  currentDate[11];
} LoggerImpl;

struct HavelLogger {
    LoggerImpl      impl;
    pthread_mutex_t mutex;
    char**          history;
    size_t          historyCount;
    size_t          historyCapacity;
    HavelLoggerLevel currentLevel;
    bool            consoleOutput;
    bool            useTimestampedFiles;
    int             logMaxPeriod;
    bool            coloredOutput;
    size_t          maxHistorySize;
    size_t          maxFileSize;
    size_t          currentFileSize;
    size_t          flushCounter;
    bool            destroying;
};

static const char* kColorDebug   = "\033[36m";
static const char* kColorInfo    = "\033[32m";
static const char* kColorWarning = "\033[33m";
static const char* kColorError   = "\033[31m";
static const char* kColorFatal   = "\033[35m";
static const char* kColorReset   = "\033[0m";

static const char* levelColor(HavelLoggerLevel level) {
    switch (level) {
        case HAVEL_LOG_DEBUG:   return kColorDebug;
        case HAVEL_LOG_INFO:    return kColorInfo;
        case HAVEL_LOG_WARNING: return kColorWarning;
        case HAVEL_LOG_ERROR:   return kColorError;
        case HAVEL_LOG_FATAL:   return kColorFatal;
        default: return "";
    }
}

static const char* levelString(HavelLoggerLevel level) {
    switch (level) {
        case HAVEL_LOG_DEBUG:   return "DEBUG";
        case HAVEL_LOG_INFO:    return "INFO";
        case HAVEL_LOG_WARNING: return "WARNING";
        case HAVEL_LOG_ERROR:   return "ERROR";
        case HAVEL_LOG_FATAL:   return "FATAL";
        default: return "UNKNOWN";
    }
}

static void getCurrentTimestamp(char* buf, size_t bufSize) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    int ms = (int)(ts.tv_nsec / 1000000);
    snprintf(buf, bufSize, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms);
}

static void getFormattedTimestamp(char* buf, size_t bufSize) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    snprintf(buf, bufSize, "%04d-%02d-%02d_%02d-%02d-%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
}

static void getLogDirectory(char* buf, size_t bufSize) {
    const char* home = getenv("HOME");
    if (!home) {
        snprintf(buf, bufSize, "./logs");
        mkdir("./logs", 0755);
        return;
    }
    snprintf(buf, bufSize, "%s/.local/share/havel/logs", home);
    size_t len = strlen(buf);
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    mkdir(buf, 0755);
}

static void historyPush(HavelLogger* logger, const char* entry) {
    if (logger->historyCount >= logger->historyCapacity) {
        size_t newCap = logger->historyCapacity == 0 ? 64 : logger->historyCapacity * 2;
        char** newHist = (char**)realloc(logger->history, newCap * sizeof(char*));
        if (!newHist) return;
        logger->history = newHist;
        logger->historyCapacity = newCap;
    }
    logger->history[logger->historyCount++] = strdup(entry);
    while (logger->historyCount > logger->maxHistorySize) {
        free(logger->history[0]);
        memmove(logger->history, logger->history + 1, (logger->historyCount - 1) * sizeof(char*));
        logger->historyCount--;
    }
}

static void cleanupOldLogs(HavelLogger* logger) {
    if (logger->logMaxPeriod <= 0) return;

    char logDir[HAVEL_PATH_BUF_SIZE];
    getLogDirectory(logDir, sizeof(logDir));

    DIR* dir = opendir(logDir);
    if (!dir) return;

    time_t now = time(NULL);

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len < 14 || strcmp(name + len - 4, ".log") != 0) continue;

        int year, month, day;
        if (sscanf(name, "%d-%d-%d", &year, &month, &day) != 3) continue;

        struct tm fileTm;
        memset(&fileTm, 0, sizeof(fileTm));
        fileTm.tm_year = year - 1900;
        fileTm.tm_mon = month - 1;
        fileTm.tm_mday = day;
        fileTm.tm_hour = 12;
        time_t fileTime = mktime(&fileTm);
        if (fileTime == (time_t)-1) continue;

        double diffDays = difftime(now, fileTime) / 86400.0;
        if (diffDays > (double)logger->logMaxPeriod) {
            char fullPath[HAVEL_PATH_BUF_SIZE];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", logDir, name);
            unlink(fullPath);
        }
    }
    closedir(dir);
}

static void openNewLogFile(HavelLogger* logger) {
    if (!logger->useTimestampedFiles) return;

    char currentDate[11];
    getFormattedTimestamp(currentDate, sizeof(currentDate));
    currentDate[10] = '\0';

    char logDir[HAVEL_PATH_BUF_SIZE];
    getLogDirectory(logDir, sizeof(logDir));

    char fullPath[HAVEL_PATH_BUF_SIZE];
    snprintf(fullPath, sizeof(fullPath), "%s/%s.log", logDir, currentDate);

    if (logger->impl.logFile) {
        fclose(logger->impl.logFile);
        logger->impl.logFile = NULL;
    }

    logger->impl.logFile = fopen(fullPath, "a");
    strncpy(logger->impl.currentFilename, fullPath, HAVEL_PATH_BUF_SIZE - 1);
    logger->impl.currentFilename[HAVEL_PATH_BUF_SIZE - 1] = '\0';
    strncpy(logger->impl.currentDate, currentDate, 10);
    logger->impl.currentDate[10] = '\0';
    logger->currentFileSize = 0;

    if (logger->impl.logFile) {
        fseek(logger->impl.logFile, 0, SEEK_END);
        long sz = ftell(logger->impl.logFile);
        if (sz > 0) logger->currentFileSize = (size_t)sz;
    }

    cleanupOldLogs(logger);
}

static void havel_log(HavelLogger* logger, HavelLoggerLevel level, const char* message) {
    if (logger->destroying) return;
    if (level < logger->currentLevel) return;

    pthread_mutex_lock(&logger->mutex);

    if (logger->useTimestampedFiles) {
        char currentDate[11];
        getFormattedTimestamp(currentDate, sizeof(currentDate));
        currentDate[10] = '\0';
        if (memcmp(logger->impl.currentDate, currentDate, 10) != 0) {
            memcpy(logger->impl.currentDate, currentDate, 10);
            openNewLogFile(logger);
        }
    }

    char timestamp[64];
    getCurrentTimestamp(timestamp, sizeof(timestamp));

    char logMessage[HAVEL_LOG_BUF_SIZE];
    snprintf(logMessage, sizeof(logMessage), "%s [%s] %s\n", timestamp, levelString(level), message);

    historyPush(logger, logMessage);

    if (logger->impl.logFile) {
        if (logger->maxFileSize > 0 && logger->currentFileSize >= logger->maxFileSize) {
            fclose(logger->impl.logFile);
            char rotated[HAVEL_PATH_BUF_SIZE];
            snprintf(rotated, sizeof(rotated), "%s.1", logger->impl.currentFilename);
            rename(logger->impl.currentFilename, rotated);
            logger->impl.logFile = fopen(logger->impl.currentFilename, "a");
            logger->currentFileSize = 0;
        }
        size_t msgLen = strlen(logMessage);
        fwrite(logMessage, 1, msgLen, logger->impl.logFile);
        logger->currentFileSize += msgLen;
        logger->flushCounter++;
        if (logger->flushCounter % HAVEL_FLUSH_INTERVAL == 0) {
            fflush(logger->impl.logFile);
        }
    }

    if (logger->consoleOutput) {
        if (logger->coloredOutput) {
            fprintf(stdout, "%s%s%s", levelColor(level), logMessage, kColorReset);
        } else {
            fputs(logMessage, stdout);
        }
    }

    pthread_mutex_unlock(&logger->mutex);
}

static HavelLogger* g_instance = NULL;

static void destroyLogger(void) {
    if (!g_instance) return;
    g_instance->destroying = true;
    if (g_instance->impl.logFile) {
        fclose(g_instance->impl.logFile);
        g_instance->impl.logFile = NULL;
    }
    for (size_t i = 0; i < g_instance->historyCount; i++) {
        free(g_instance->history[i]);
    }
    free(g_instance->history);
    pthread_mutex_destroy(&g_instance->mutex);
    free(g_instance);
    g_instance = NULL;
}

HavelLogger* HavelLogger_getInstance(void) {
    if (g_instance) return g_instance;

    HavelLogger* logger = (HavelLogger*)calloc(1, sizeof(HavelLogger));
    if (!logger) return NULL;

    pthread_mutex_init(&logger->mutex, NULL);
    logger->currentLevel = HAVEL_LOG_INFO;
    logger->consoleOutput = true;
    logger->useTimestampedFiles = true;
    logger->logMaxPeriod = 3;
    logger->coloredOutput = true;
    logger->maxHistorySize = HAVEL_HISTORY_DEFAULT_MAX;
    logger->maxFileSize = HAVEL_FILE_SIZE_DEFAULT;
    logger->history = NULL;
    logger->historyCount = 0;
    logger->historyCapacity = 0;
    logger->flushCounter = 0;
    logger->destroying = false;

    HavelLogger_initialize(logger, true, 3, true);

    g_instance = logger;
    atexit(destroyLogger);

    return g_instance;
}

void HavelLogger_initialize(HavelLogger* logger, bool useTimestampedFiles, int logMaxPeriod, bool coloredOutput) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    logger->useTimestampedFiles = useTimestampedFiles;
    logger->logMaxPeriod = logMaxPeriod;
    logger->coloredOutput = coloredOutput;
    if (useTimestampedFiles) {
        openNewLogFile(logger);
    } else {
        HavelLogger_setLogFile(logger, "havel.log");
    }
    pthread_mutex_unlock(&logger->mutex);
}

void HavelLogger_initializeWithConfig(HavelLogger* logger, bool useTimestamped, int logMaxPeriod, bool colorsEnabled) {
    HavelLogger_initialize(logger, useTimestamped, logMaxPeriod, colorsEnabled);
}

void HavelLogger_setLogFile(HavelLogger* logger, const char* filename) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    if (logger->impl.logFile) {
        fclose(logger->impl.logFile);
        logger->impl.logFile = NULL;
    }
    logger->impl.logFile = fopen(filename, "a");
    strncpy(logger->impl.currentFilename, filename, HAVEL_PATH_BUF_SIZE - 1);
    logger->impl.currentFilename[HAVEL_PATH_BUF_SIZE - 1] = '\0';
    logger->currentFileSize = 0;
    if (logger->impl.logFile) {
        fseek(logger->impl.logFile, 0, SEEK_END);
        long sz = ftell(logger->impl.logFile);
        if (sz > 0) logger->currentFileSize = (size_t)sz;
    }
    pthread_mutex_unlock(&logger->mutex);
}

void HavelLogger_setLogLevel(HavelLogger* logger, HavelLoggerLevel level) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    logger->currentLevel = level;
    pthread_mutex_unlock(&logger->mutex);
}

HavelLoggerLevel HavelLogger_getCurrentLevel(const HavelLogger* logger) {
    if (!logger) return HAVEL_LOG_INFO;
    return logger->currentLevel;
}

void HavelLogger_setColoredOutput(HavelLogger* logger, bool enabled) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    logger->coloredOutput = enabled;
    pthread_mutex_unlock(&logger->mutex);
}

void HavelLogger_setMaxHistorySize(HavelLogger* logger, size_t maxSize) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    logger->maxHistorySize = maxSize;
    while (logger->historyCount > logger->maxHistorySize) {
        free(logger->history[0]);
        memmove(logger->history, logger->history + 1, (logger->historyCount - 1) * sizeof(char*));
        logger->historyCount--;
    }
    pthread_mutex_unlock(&logger->mutex);
}

void HavelLogger_setMaxFileSize(HavelLogger* logger, size_t maxBytes) {
    if (!logger) return;
    pthread_mutex_lock(&logger->mutex);
    logger->maxFileSize = maxBytes;
    pthread_mutex_unlock(&logger->mutex);
}

void HavelLogger_getLogFilePath(const HavelLogger* logger, char* buf, size_t bufSize) {
    if (!logger || !buf) return;
    strncpy(buf, logger->impl.currentFilename, bufSize - 1);
    buf[bufSize - 1] = '\0';
}

size_t HavelLogger_getHistory(const HavelLogger* logger, char** outEntries, size_t maxEntries, size_t maxLinesPerEntry) {
    if (!logger || !outEntries) return 0;
    size_t start = logger->historyCount > maxLinesPerEntry ? logger->historyCount - maxLinesPerEntry : 0;
    size_t count = 0;
    for (size_t i = start; i < logger->historyCount && count < maxEntries; i++, count++) {
        outEntries[count] = strdup(logger->history[i]);
    }
    return count;
}

void HavelLogger_freeHistoryEntries(char** entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; i++) {
        free(entries[i]);
    }
}

void HavelLogger_debug(HavelLogger* logger, const char* message) {
    havel_log(logger, HAVEL_LOG_DEBUG, message);
}

void HavelLogger_info(HavelLogger* logger, const char* message) {
    havel_log(logger, HAVEL_LOG_INFO, message);
}

void HavelLogger_warning(HavelLogger* logger, const char* message) {
    havel_log(logger, HAVEL_LOG_WARNING, message);
}

void HavelLogger_error(HavelLogger* logger, const char* message) {
    havel_log(logger, HAVEL_LOG_ERROR, message);
}

void HavelLogger_fatal(HavelLogger* logger, const char* message) {
    havel_log(logger, HAVEL_LOG_FATAL, message);
}

void HavelLogger_critical(HavelLogger* logger, const char* message) {
    HavelLogger_fatal(logger, message);
}

void HavelLogger_logf(HavelLogger* logger, HavelLoggerLevel level, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, level, buf);
}

void HavelLogger_debugf(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_DEBUG, buf);
}

void HavelLogger_infof(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_INFO, buf);
}

void HavelLogger_warningf(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_WARNING, buf);
}

void HavelLogger_errorf(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_ERROR, buf);
}

void HavelLogger_fatalf(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_FATAL, buf);
}

void HavelLogger_criticalf(HavelLogger* logger, const char* format, ...) {
    char buf[HAVEL_LOG_BUF_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    havel_log(logger, HAVEL_LOG_FATAL, buf);
}
