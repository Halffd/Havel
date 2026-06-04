/*
 * Config.c
 *
 * Pure C configuration management with:
 * - TOML-style parsing ([section] key=value, quoted strings, booleans, numbers)
 * - inotify file watching with poll fallback
 * - Debounced save via background thread
 * - Watch callbacks
 * - Thread-safe via pthread_mutex
 */

#include "Config.h"
#include "utils/LoggerC.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Internal hash map (open-addressing, FNV-1a)
// ---------------------------------------------------------------------------

#define CFG_MAP_INIT_CAP 64

typedef struct CfgEntry {
    char* key;
    char* value;
    bool  occupied;
} CfgEntry;

typedef struct CfgMap {
    CfgEntry* entries;
    size_t    cap;
    size_t    len;
} CfgMap;

static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static void cfgMapInit(CfgMap* m) {
    m->cap = CFG_MAP_INIT_CAP;
    m->len = 0;
    m->entries = (CfgEntry*)calloc(m->cap, sizeof(CfgEntry));
}

static void cfgMapFree(CfgMap* m) {
    for (size_t i = 0; i < m->cap; i++) {
        free(m->entries[i].key);
        free(m->entries[i].value);
    }
    free(m->entries);
    m->entries = NULL;
    m->cap = m->len = 0;
}

static size_t cfgMapFindSlot(const CfgMap* m, const char* key) {
    size_t idx = fnv1a(key) & (m->cap - 1);
    for (size_t i = 0; i < m->cap; i++) {
        size_t pos = (idx + i) & (m->cap - 1);
        if (!m->entries[pos].occupied || strcmp(m->entries[pos].key, key) == 0)
            return pos;
    }
    return (size_t)-1;
}

static void cfgMapGrow(CfgMap* m) {
    size_t oldCap = m->cap;
    CfgEntry* old = m->entries;
    m->cap = oldCap * 2;
    m->entries = (CfgEntry*)calloc(m->cap, sizeof(CfgEntry));
    m->len = 0;
    for (size_t i = 0; i < oldCap; i++) {
        if (old[i].occupied) {
            size_t slot = cfgMapFindSlot(m, old[i].key);
            m->entries[slot] = old[i];
            m->len++;
        }
    }
    free(old);
}

static void cfgMapSet(CfgMap* m, const char* key, const char* value) {
    if (m->len * 2 >= m->cap) cfgMapGrow(m);
    size_t slot = cfgMapFindSlot(m, key);
    if (slot == (size_t)-1) return;
    if (m->entries[slot].occupied) {
        free(m->entries[slot].value);
        m->entries[slot].value = strdup(value);
    } else {
        m->entries[slot].key = strdup(key);
        m->entries[slot].value = strdup(value);
        m->entries[slot].occupied = true;
        m->len++;
    }
}

static const char* cfgMapGet(const CfgMap* m, const char* key) {
    size_t slot = cfgMapFindSlot(m, key);
    if (slot != (size_t)-1 && m->entries[slot].occupied)
        return m->entries[slot].value;
    return NULL;
}

static bool cfgMapRemove(CfgMap* m, const char* key) {
    size_t slot = cfgMapFindSlot(m, key);
    if (slot == (size_t)-1 || !m->entries[slot].occupied) return false;
    free(m->entries[slot].key);
    free(m->entries[slot].value);
    m->entries[slot].key = NULL;
    m->entries[slot].value = NULL;
    m->entries[slot].occupied = false;
    m->len--;
    return true;
}

// ---------------------------------------------------------------------------
// Trim helper
// ---------------------------------------------------------------------------

static char* trimDup(const char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    const char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    size_t len = (size_t)(end - s);
    char* r = (char*)malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

// ---------------------------------------------------------------------------
// Watch callback list
// ---------------------------------------------------------------------------

typedef struct CfgWatch {
    char*                      key;
    HavelConfig_WatchCallback  cb;
    void*                      userData;
} CfgWatch;

// ---------------------------------------------------------------------------
// HavelConfig struct
// ---------------------------------------------------------------------------

struct HavelConfig {
    CfgMap       values;
    char*        path;
    CfgWatch*    watchers;
    size_t       watcherCount;
    size_t       watcherCap;
    pthread_mutex_t mutex;

    // File watching
    bool          watching;
    pthread_t     watchThread;
    pthread_mutex_t watchMutex;

    // Debounced save
    bool          savePending;
    pthread_t     saveThread;
    pthread_mutex_t saveMutex;
};

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static HavelConfig g_config;
static bool g_configInit = false;

static void ensureInit(void) {
    if (g_configInit) return;
    cfgMapInit(&g_config.values);
    g_config.path = NULL;
    g_config.watchers = NULL;
    g_config.watcherCount = 0;
    g_config.watcherCap = 0;
    pthread_mutex_init(&g_config.mutex, NULL);
    pthread_mutex_init(&g_config.watchMutex, NULL);
    pthread_mutex_init(&g_config.saveMutex, NULL);
    g_config.watching = false;
    g_config.savePending = false;
    g_configInit = true;
}

HavelConfig* HavelConfig_getInstance(void) {
    ensureInit();
    return &g_config;
}

// ---------------------------------------------------------------------------
// Path management (thread-safe via lazy statics)
// ---------------------------------------------------------------------------

static char* g_configDir = NULL;
static char* g_mainConfig = NULL;
static char* g_hotkeysDir = NULL;
static pthread_mutex_t g_pathMutex = PTHREAD_MUTEX_INITIALIZER;

static char* defaultConfigDir(void) {
    const char* home = getenv("HOME");
    if (home) {
        size_t len = strlen(home) + strlen("/.config/havel/");
        char* buf = (char*)malloc(len + 1);
        snprintf(buf, len + 1, "%s/.config/havel/", home);
        return buf;
    }
    return strdup("./havel/");
}

static char* getPathDir(void) {
    pthread_mutex_lock(&g_pathMutex);
    if (!g_configDir) {
        g_configDir = defaultConfigDir();
        size_t len = strlen(g_configDir) + strlen("havel.cfg");
        g_mainConfig = (char*)malloc(len + 1);
        snprintf(g_mainConfig, len + 1, "%shavel.cfg", g_configDir);
        size_t hlen = strlen(g_configDir) + strlen("hotkeys/");
        g_hotkeysDir = (char*)malloc(hlen + 1);
        snprintf(g_hotkeysDir, hlen + 1, "%shotkeys/", g_configDir);
    }
    pthread_mutex_unlock(&g_pathMutex);
    return g_configDir;
}

void HavelConfig_setConfigPath(const char* dir, const char* basename) {
    pthread_mutex_lock(&g_pathMutex);
    free(g_configDir); g_configDir = NULL;
    free(g_mainConfig); g_mainConfig = NULL;
    free(g_hotkeysDir); g_hotkeysDir = NULL;
    getPathDir();
    size_t dlen = strlen(dir);
    g_configDir = (char*)malloc(dlen + 1);
    memcpy(g_configDir, dir, dlen + 1);
    size_t mlen = dlen + strlen(basename ? basename : "havel.cfg");
    g_mainConfig = (char*)malloc(mlen + 1);
    snprintf(g_mainConfig, mlen + 1, "%s%s", dir, basename ? basename : "havel.cfg");
    size_t hlen = dlen + strlen("hotkeys/");
    g_hotkeysDir = (char*)malloc(hlen + 1);
    snprintf(g_hotkeysDir, hlen + 1, "%shotkeys/", dir);
    pthread_mutex_unlock(&g_pathMutex);
}

void HavelConfig_getConfigDir(char* buf, size_t bufSize) {
    const char* d = getPathDir();
    snprintf(buf, bufSize, "%s", d);
}

void HavelConfig_getConfigFilePath(const char* filename, char* buf, size_t bufSize) {
    if (filename && strchr(filename, '/')) {
        snprintf(buf, bufSize, "%s", filename);
        return;
    }
    const char* d = getPathDir();
    snprintf(buf, bufSize, "%s%s", d, filename ? filename : "havel.cfg");
}

static void mkdirRecursive(const char* path) {
    char* tmp = strdup(path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    free(tmp);
}

void HavelConfig_ensureConfigDir(void) {
    pthread_mutex_lock(&g_pathMutex);
    getPathDir();
    struct stat st;
    if (stat(g_configDir, &st) != 0) mkdirRecursive(g_configDir);
    if (g_hotkeysDir && stat(g_hotkeysDir, &st) != 0) mkdirRecursive(g_hotkeysDir);
    pthread_mutex_unlock(&g_pathMutex);
}

// ---------------------------------------------------------------------------
// TOML parsing
// ---------------------------------------------------------------------------

static void parseTomlLine(char* line, char** section) {
    char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p || *p == '#' || *p == ';') return;

    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) p[--len] = '\0';
    if (!*p) return;

    if (*p == '[' && len >= 2 && p[len - 1] == ']') {
        p[len - 1] = '\0';
        char* sec = p + 1;
        if (*sec == '[' && p[len - 2] == ']') {
            p[len - 2] = '\0';
            sec++;
        }
        free(*section);
        *section = trimDup(sec);
        return;
    }

    char* eq = strchr(p, '=');
    if (!eq) return;
    *eq = '\0';
    char* rawKey = p;
    char* rawVal = eq + 1;

    char* key = trimDup(rawKey);
    char* val = trimDup(rawVal);

    size_t vlen = strlen(val);
    if (vlen >= 2 &&
        ((val[0] == '"' && val[vlen - 1] == '"') ||
         (val[0] == '\'' && val[vlen - 1] == '\''))) {
        memmove(val, val + 1, vlen - 2);
        val[vlen - 2] = '\0';
    }

    pthread_mutex_lock(&g_config.mutex);
    if (*section && **section) {
        size_t klen = strlen(*section) + 1 + strlen(key) + 1;
        char* fullKey = (char*)malloc(klen);
        snprintf(fullKey, klen, "%s.%s", *section, key);
        cfgMapSet(&g_config.values, fullKey, val);
        free(fullKey);
    } else {
        cfgMapSet(&g_config.values, key, val);
    }
    pthread_mutex_unlock(&g_config.mutex);

    free(key);
    free(val);
}

// ---------------------------------------------------------------------------
// Load / Save / Reload
// ---------------------------------------------------------------------------

void HavelConfig_load(HavelConfig* cfg, const char* filename) {
    (void)cfg;
    char fpath[4096];
    HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));

    pthread_mutex_lock(&g_config.mutex);
    free(g_config.path);
    g_config.path = strdup(fpath);
    pthread_mutex_unlock(&g_config.mutex);

    FILE* f = fopen(fpath, "r");
    if (!f) {
        HAVEL_LOGF_ERROR("Could not open config file: %s", fpath);
        return;
    }

    char* section = NULL;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        parseTomlLine(line, &section);
    }
    fclose(f);
    free(section);
}

static void writeTomlValue(FILE* f, const char* value) {
    if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        fputs(value, f);
        return;
    }
    char* end;
    strtod(value, &end);
    if (*end == '\0' && end != value) {
        fputs(value, f);
        return;
    }
    size_t vlen = strlen(value);
    if (vlen >= 2 &&
        ((value[0] == '"' && value[vlen - 1] == '"') ||
         (value[0] == '\'' && value[vlen - 1] == '\''))) {
        fputs(value, f);
        return;
    }
    if (vlen > 0 && (value[0] == '[' || value[0] == '{')) {
        fputs(value, f);
        return;
    }
    fputc('"', f);
    fputs(value, f);
    fputc('"', f);
}

static int cmpStr(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

void HavelConfig_save(HavelConfig* cfg, const char* filename) {
    (void)cfg;
    char fpath[4096];
    if (filename && *filename) {
        HavelConfig_getConfigFilePath(filename, fpath, sizeof(fpath));
    } else {
        pthread_mutex_lock(&g_config.mutex);
        if (!g_config.path || !*g_config.path) {
            pthread_mutex_unlock(&g_config.mutex);
            return;
        }
        snprintf(fpath, sizeof(fpath), "%s", g_config.path);
        pthread_mutex_unlock(&g_config.mutex);
    }

    HavelConfig_ensureConfigDir();

    FILE* f = fopen(fpath, "w");
    if (!f) {
        HAVEL_LOGF_ERROR("Could not save config file: %s", fpath);
        return;
    }

    pthread_mutex_lock(&g_config.mutex);

    // Collect all keys, sort them
    size_t nKeys = g_config.values.len;
    char** keys = (char**)malloc(nKeys * sizeof(char*));
    size_t ki = 0;
    for (size_t i = 0; i < g_config.values.cap; i++) {
        if (g_config.values.entries[i].occupied)
            keys[ki++] = g_config.values.entries[i].key;
    }
    qsort(keys, ki, sizeof(char*), cmpStr);

    // Group by section
    const char* lastSection = "";
    for (size_t i = 0; i < ki; i++) {
        const char* key = keys[i];
        const char* val = cfgMapGet(&g_config.values, key);
        if (!val) continue;

        const char* dot = strchr(key, '.');
        if (dot) {
            size_t secLen = (size_t)(dot - key);
            if (secLen != strlen(lastSection) || strncmp(key, lastSection, secLen) != 0) {
                if (i > 0) fputc('\n', f);
                fputc('[', f);
                fwrite(key, 1, secLen, f);
                fputs("]\n", f);
                lastSection = key;
            }
            fputs(dot + 1, f);
            fputs(" = ", f);
            writeTomlValue(f, val);
            fputc('\n', f);
        } else {
            fputs(key, f);
            fputs(" = ", f);
            writeTomlValue(f, val);
            fputc('\n', f);
            lastSection = "";
        }
    }

    pthread_mutex_unlock(&g_config.mutex);
    free(keys);
    fclose(f);
}

void HavelConfig_reload(HavelConfig* cfg) {
    pthread_mutex_lock(&g_config.mutex);
    char* p = g_config.path ? strdup(g_config.path) : NULL;
    pthread_mutex_unlock(&g_config.mutex);

    if (!p || !*p) { free(p); return; }

    FILE* f = fopen(p, "r");
    if (!f) { free(p); return; }

    CfgMap newValues;
    cfgMapInit(&newValues);

    char* section = NULL;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char* sp = line;
        while (*sp && isspace((unsigned char)*sp)) sp++;
        if (!*sp || *sp == '#' || *sp == ';') continue;

        size_t len = strlen(sp);
        while (len > 0 && isspace((unsigned char)sp[len - 1])) sp[--len] = '\0';
        if (!*sp) continue;

        if (*sp == '[' && len >= 2 && sp[len - 1] == ']') {
            sp[len - 1] = '\0';
            char* sec = sp + 1;
            if (*sec == '[' && sp[len - 2] == ']') { sp[len - 2] = '\0'; sec++; }
            free(section);
            section = trimDup(sec);
            continue;
        }

        char* eq = strchr(sp, '=');
        if (!eq) continue;
        *eq = '\0';
        char* rawKey = sp;
        char* rawVal = eq + 1;
        char* key = trimDup(rawKey);
        char* val = trimDup(rawVal);
        size_t vlen = strlen(val);
        if (vlen >= 2 &&
            ((val[0] == '"' && val[vlen - 1] == '"') ||
             (val[0] == '\'' && val[vlen - 1] == '\''))) {
            memmove(val, val + 1, vlen - 2);
            val[vlen - 2] = '\0';
        }

        if (section && *section) {
            size_t klen = strlen(section) + 1 + strlen(key) + 1;
            char* fullKey = (char*)malloc(klen);
            snprintf(fullKey, klen, "%s.%s", section, key);
            cfgMapSet(&newValues, fullKey, val);
            free(fullKey);
        } else {
            cfgMapSet(&newValues, key, val);
        }
        free(key);
        free(val);
    }
    fclose(f);
    free(section);

    // Notify watchers of changed values, then swap
    pthread_mutex_lock(&g_config.mutex);
    for (size_t w = 0; w < g_config.watcherCount; w++) {
        CfgWatch* cw = &g_config.watchers[w];
        for (size_t i = 0; i < newValues.cap; i++) {
            if (!newValues.entries[i].occupied) continue;
            const char* nk = newValues.entries[i].key;
            const char* nv = newValues.entries[i].value;
            const char* ov = cfgMapGet(&g_config.values, nk);
            if (!ov || strcmp(ov, nv) != 0) {
                if (strcmp(cw->key, nk) == 0 || cw->key[0] == '\0') {
                    cw->cb(nk, nv, cw->userData);
                }
            }
        }
    }
    cfgMapFree(&g_config.values);
    g_config.values = newValues;
    pthread_mutex_unlock(&g_config.mutex);
    free(p);
}

// ---------------------------------------------------------------------------
// Debounced save
// ---------------------------------------------------------------------------

static void* saveThreadFunc(void* arg) {
    HavelConfig* cfg = (HavelConfig*)arg;
    while (cfg->savePending) {
        struct timespec ts = {0, 500000000L};
        nanosleep(&ts, NULL);
        if (cfg->savePending) {
            cfg->savePending = false;
            HavelConfig_save(cfg, NULL);
        }
    }
    return NULL;
}

void HavelConfig_requestSave(HavelConfig* cfg) {
    cfg->savePending = true;
    pthread_mutex_lock(&cfg->saveMutex);
    if (!cfg->saveThread) {
        pthread_create(&cfg->saveThread, NULL, saveThreadFunc, cfg);
        pthread_detach(cfg->saveThread);
    }
    pthread_mutex_unlock(&cfg->saveMutex);
}

void HavelConfig_forceSave(HavelConfig* cfg) {
    cfg->savePending = false;
    HavelConfig_save(cfg, NULL);
}

// ---------------------------------------------------------------------------
// Ensure config file with defaults
// ---------------------------------------------------------------------------

void HavelConfig_ensureConfigFile(HavelConfig* cfg, const char* filename) {
    char fpath[4096];
    HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));

    pthread_mutex_lock(&cfg->mutex);
    free(cfg->path);
    cfg->path = strdup(fpath);
    pthread_mutex_unlock(&cfg->mutex);

    HavelConfig_ensureConfigDir();

    struct stat st;
    if (stat(fpath, &st) == 0) return;

    FILE* f = fopen(fpath, "w");
    if (!f) {
        HAVEL_LOGF_ERROR("Could not create config file: %s", fpath);
        return;
    }
    fputs("[Debug]\n", f);
    fputs("VerboseKeyLogging = false\n", f);
    fputs("VerboseWindowLogging = false\n", f);
    fputs("VerboseConditionLogging = false\n", f);
    fputs("\n[Compiler]\n", f);
    fputs("JIT = true\n", f);
    fputs("\n[General]\n", f);
    fputs("GamingApps = \"steam_app_default\"\n", f);
    fputs("DefaultBrightness = 1.0\n", f);
    fputs("StartupBrightness = 1.0\n", f);
    fputs("StartupGamma = 1000\n", f);
    fputs("BrightnessAmount = 0.05\n", f);
    fputs("GammaAmount = 50\n", f);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Key-value access
// ---------------------------------------------------------------------------

const char* HavelConfig_getString(const HavelConfig* cfg, const char* key, const char* defaultVal) {
    (void)cfg;
    pthread_mutex_lock((pthread_mutex_t*)&g_config.mutex);
    const char* v = cfgMapGet(&g_config.values, key);
    pthread_mutex_unlock((pthread_mutex_t*)&g_config.mutex);
    return v ? v : (defaultVal ? defaultVal : "");
}

bool HavelConfig_getBool(const HavelConfig* cfg, const char* key, bool defaultVal) {
    const char* v = HavelConfig_getString(cfg, key, NULL);
    if (!v) return defaultVal;
    if (strcasecmp(v, "true") == 0 || strcasecmp(v, "yes") == 0 ||
        strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) return true;
    if (strcasecmp(v, "false") == 0 || strcasecmp(v, "no") == 0 ||
        strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0) return false;
    return defaultVal;
}

int HavelConfig_getInt(const HavelConfig* cfg, const char* key, int defaultVal) {
    const char* v = HavelConfig_getString(cfg, key, NULL);
    if (!v) return defaultVal;
    char* end;
    long n = strtol(v, &end, 10);
    return (*end == '\0') ? (int)n : defaultVal;
}

double HavelConfig_getDouble(const HavelConfig* cfg, const char* key, double defaultVal) {
    const char* v = HavelConfig_getString(cfg, key, NULL);
    if (!v) return defaultVal;
    char* end;
    double d = strtod(v, &end);
    return (*end == '\0') ? d : defaultVal;
}

static void notifyWatchers(HavelConfig* cfg, const char* key, const char* value) {
    for (size_t i = 0; i < cfg->watcherCount; i++) {
        CfgWatch* cw = &cfg->watchers[i];
        if (strcmp(cw->key, key) == 0 || cw->key[0] == '\0') {
            cw->cb(key, value, cw->userData);
        }
    }
}

void HavelConfig_setString(HavelConfig* cfg, const char* key, const char* value, bool save) {
    pthread_mutex_lock(&cfg->mutex);
    cfgMapSet(&cfg->values, key, value);
    notifyWatchers(cfg, key, value);
    pthread_mutex_unlock(&cfg->mutex);
    if (save) HavelConfig_requestSave(cfg);
}

void HavelConfig_setBool(HavelConfig* cfg, const char* key, bool value, bool save) {
    HavelConfig_setString(cfg, key, value ? "true" : "false", save);
}

void HavelConfig_setInt(HavelConfig* cfg, const char* key, int value, bool save) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    HavelConfig_setString(cfg, key, buf, save);
}

void HavelConfig_setDouble(HavelConfig* cfg, const char* key, double value, bool save) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", value);
    HavelConfig_setString(cfg, key, buf, save);
}

bool HavelConfig_has(const HavelConfig* cfg, const char* key) {
    pthread_mutex_lock((pthread_mutex_t*)&g_config.mutex);
    bool found = cfgMapGet(&g_config.values, key) != NULL;
    pthread_mutex_unlock((pthread_mutex_t*)&g_config.mutex);
    return found;
}

bool HavelConfig_remove(HavelConfig* cfg, const char* key) {
    pthread_mutex_lock(&cfg->mutex);
    bool removed = cfgMapRemove(&cfg->values, key);
    pthread_mutex_unlock(&cfg->mutex);
    if (removed) HavelConfig_requestSave(cfg);
    return removed;
}

// ---------------------------------------------------------------------------
// Key enumeration
// ---------------------------------------------------------------------------

size_t HavelConfig_getAllKeys(const HavelConfig* cfg, char*** outKeys) {
    pthread_mutex_lock((pthread_mutex_t*)&g_config.mutex);
    size_t n = g_config.values.len;
    char** arr = (char**)malloc(n * sizeof(char*));
    size_t ki = 0;
    for (size_t i = 0; i < g_config.values.cap; i++) {
        if (g_config.values.entries[i].occupied)
            arr[ki++] = strdup(g_config.values.entries[i].key);
    }
    pthread_mutex_unlock((pthread_mutex_t*)&g_config.mutex);
    *outKeys = arr;
    return ki;
}

void HavelConfig_freeKeys(char** keys, size_t count) {
    for (size_t i = 0; i < count; i++) free(keys[i]);
    free(keys);
}

size_t HavelConfig_getConfigs(const HavelConfig* cfg, char*** outEntries) {
    pthread_mutex_lock((pthread_mutex_t*)&g_config.mutex);
    size_t n = g_config.values.len;
    char** arr = (char**)malloc(n * sizeof(char*));
    size_t ki = 0;
    for (size_t i = 0; i < g_config.values.cap; i++) {
        if (g_config.values.entries[i].occupied) {
            size_t len = strlen(g_config.values.entries[i].key) +
                         1 + strlen(g_config.values.entries[i].value) + 1;
            char* entry = (char*)malloc(len);
            snprintf(entry, len, "%s=%s",
                     g_config.values.entries[i].key,
                     g_config.values.entries[i].value);
            arr[ki++] = entry;
        }
    }
    pthread_mutex_unlock((pthread_mutex_t*)&g_config.mutex);
    *outEntries = arr;
    return ki;
}

void HavelConfig_freeEntries(char** entries, size_t count) {
    for (size_t i = 0; i < count; i++) free(entries[i]);
    free(entries);
}

// ---------------------------------------------------------------------------
// Path getters
// ---------------------------------------------------------------------------

const char* HavelConfig_getPath(const HavelConfig* cfg) {
    (void)cfg;
    return g_config.path ? g_config.path : "";
}

void HavelConfig_setPath(const char* newPath) {
    const char* lastSlash = strrchr(newPath, '/');
    if (lastSlash) {
        size_t dlen = (size_t)(lastSlash - newPath) + 1;
        char* dir = (char*)malloc(dlen + 1);
        memcpy(dir, newPath, dlen);
        dir[dlen] = '\0';
        HavelConfig_setConfigPath(dir, lastSlash + 1);
        free(dir);
    } else {
        HavelConfig_setConfigPath("./", newPath);
    }
}

// ---------------------------------------------------------------------------
// Convenience getters
// ---------------------------------------------------------------------------

bool HavelConfig_getVerboseKeyLogging(const HavelConfig* cfg) {
    return HavelConfig_getBool(cfg, "Debug.VerboseKeyLogging", false);
}

bool HavelConfig_getVerboseWindowLogging(const HavelConfig* cfg) {
    return HavelConfig_getBool(cfg, "Debug.VerboseWindowLogging", false);
}

bool HavelConfig_getVerboseConditionLogging(const HavelConfig* cfg) {
    return HavelConfig_getBool(cfg, "Debug.VerboseConditionLogging", false);
}

// ---------------------------------------------------------------------------
// File watching (inotify + poll fallback)
// ---------------------------------------------------------------------------

static time_t getLastModified(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) return st.st_mtime;
    return 0;
}

static void* watchThreadFunc(void* arg) {
    HavelConfig* cfg = (HavelConfig*)arg;

    pthread_mutex_lock(&cfg->mutex);
    char* fpath = cfg->path ? strdup(cfg->path) : NULL;
    pthread_mutex_unlock(&cfg->mutex);

    if (!fpath) return NULL;

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1) {
        HAVEL_LOG_WARN("inotify_init failed, falling back to polling");
        time_t lastMod = getLastModified(fpath);
        while (cfg->watching) {
            struct timespec ts = {1, 0};
            nanosleep(&ts, NULL);
            time_t curMod = getLastModified(fpath);
            if (curMod > lastMod) {
                lastMod = curMod;
                HavelConfig_reload(cfg);
            }
        }
        free(fpath);
        return NULL;
    }

    int wd = inotify_add_watch(fd, fpath, IN_MODIFY | IN_CLOSE_WRITE);
    if (wd == -1) {
        HAVEL_LOG_ERROR("inotify_add_watch failed");
        close(fd);
        free(fpath);
        return NULL;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (cfg->watching) {
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            char buf[4096] __attribute__((aligned(8)));
            int len = (int)read(fd, buf, sizeof(buf));
            if (len > 0) {
                HavelConfig_reload(cfg);
            }
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);
    free(fpath);
    return NULL;
}

void HavelConfig_startFileWatching(HavelConfig* cfg, const char* filename) {
    pthread_mutex_lock(&cfg->watchMutex);
    if (cfg->watching) {
        pthread_mutex_unlock(&cfg->watchMutex);
        return;
    }
    HavelConfig_ensureConfigFile(cfg, filename ? filename : "havel.cfg");
    cfg->watching = true;
    pthread_create(&cfg->watchThread, NULL, watchThreadFunc, cfg);
    pthread_mutex_unlock(&cfg->watchMutex);
}

void HavelConfig_stopFileWatching(HavelConfig* cfg) {
    pthread_mutex_lock(&cfg->watchMutex);
    cfg->watching = false;
    if (cfg->watchThread) {
        pthread_join(cfg->watchThread, NULL);
        cfg->watchThread = 0;
    }
    pthread_mutex_unlock(&cfg->watchMutex);
}

// ---------------------------------------------------------------------------
// Watch callbacks
// ---------------------------------------------------------------------------

void HavelConfig_watch(HavelConfig* cfg, const char* key, HavelConfig_WatchCallback cb, void* userData) {
    pthread_mutex_lock(&cfg->mutex);
    if (cfg->watcherCount >= cfg->watcherCap) {
        size_t newCap = cfg->watcherCap ? cfg->watcherCap * 2 : 8;
        cfg->watchers = (CfgWatch*)realloc(cfg->watchers, newCap * sizeof(CfgWatch));
        cfg->watcherCap = newCap;
    }
    cfg->watchers[cfg->watcherCount].key = strdup(key ? key : "");
    cfg->watchers[cfg->watcherCount].cb = cb;
    cfg->watchers[cfg->watcherCount].userData = userData;
    cfg->watcherCount++;
    pthread_mutex_unlock(&cfg->mutex);
}

// ---------------------------------------------------------------------------
// Print
// ---------------------------------------------------------------------------

void HavelConfig_print(const HavelConfig* cfg) {
    (void)cfg;
    printf("=== Config Values ===\n");
    pthread_mutex_lock((pthread_mutex_t*)&g_config.mutex);
    for (size_t i = 0; i < g_config.values.cap; i++) {
        if (g_config.values.entries[i].occupied) {
            printf("%s = %s\n", g_config.values.entries[i].key, g_config.values.entries[i].value);
        }
    }
    pthread_mutex_unlock((pthread_mutex_t*)&g_config.mutex);
    printf("====================\n");
}
