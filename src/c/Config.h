#ifndef HAVEL_CONFIG_C_H
#define HAVEL_CONFIG_C_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque config handle (singleton via HavelConfig_getInstance)
typedef struct HavelConfig HavelConfig;

// Get the singleton config instance
HavelConfig *HavelConfig_getInstance(void);

// Path management
void HavelConfig_setConfigPath(const char *dir, const char *basename);
void HavelConfig_getConfigDir(char *buf, size_t bufSize);
void HavelConfig_getConfigFilePath(const char *filename, char *buf, size_t bufSize);
void HavelConfig_ensureConfigDir(void);

// File operations
void HavelConfig_load(HavelConfig *cfg, const char *filename);
void HavelConfig_save(HavelConfig *cfg, const char *filename);
void HavelConfig_reload(HavelConfig *cfg);
void HavelConfig_requestSave(HavelConfig *cfg);
void HavelConfig_forceSave(HavelConfig *cfg);
void HavelConfig_ensureConfigFile(HavelConfig *cfg, const char *filename);

// ---------------------------------------------------------------------------
// Save reporting, diffs, backups, validation
//
// The save pipeline is: serialize -> compare against what is on disk -> if
// different, rotate a backup and write atomically (temp + fsync + rename).
// A truncated or half-written config file is therefore not a reachable state,
// and a save that changes nothing does not touch the file or a backup slot.
// ---------------------------------------------------------------------------

// Outcome of the most recent HavelConfig_save / HavelConfig_saveWithReport.
typedef struct {
  bool written;      // false when disk already matched (no-op save)
  bool recovered;    // true when the previous file was bad and a backup was used
  size_t added;      // keys present in memory, absent on disk
  size_t removed;    // keys on disk that the save dropped
  size_t changed;    // keys whose value differs
  size_t duplicates; // duplicate [Section] headers seen while reading disk
  size_t malformed;  // unparseable lines seen while reading disk
  char backup[512];  // backup file created by this save ("" if none)
} HavelConfigSaveReport;

// Save and report what changed. `out` may be NULL.
void HavelConfig_saveWithReport(HavelConfig *cfg, const char *filename, HavelConfigSaveReport *out);

// Compare in-memory config against what is on disk without writing anything.
void HavelConfig_diffToDisk(HavelConfig *cfg, HavelConfigSaveReport *out);

// Validate a config file: 0 when clean, otherwise the number of problems.
int HavelConfig_validateFile(const char *filename, size_t *duplicateSections,
                             size_t *malformedLines);

// List backups newest-first. Caller frees with HavelConfig_freeBackups.
size_t HavelConfig_listBackups(const char *filename, char ***outPaths);

// Restore backup `index` (0 = newest) into the live config file.
bool HavelConfig_restoreBackup(HavelConfig *cfg, const char *filename, size_t index);

void HavelConfig_freeBackups(char **paths, size_t count);

// Key-value access
const char *HavelConfig_getString(const HavelConfig *cfg, const char *key, const char *defaultVal);
bool HavelConfig_getBool(const HavelConfig *cfg, const char *key, bool defaultVal);
int HavelConfig_getInt(const HavelConfig *cfg, const char *key, int defaultVal);
double HavelConfig_getDouble(const HavelConfig *cfg, const char *key, double defaultVal);

void HavelConfig_setString(HavelConfig *cfg, const char *key, const char *value, bool save);
void HavelConfig_setBool(HavelConfig *cfg, const char *key, bool value, bool save);
void HavelConfig_setInt(HavelConfig *cfg, const char *key, int value, bool save);
void HavelConfig_setDouble(HavelConfig *cfg, const char *key, double value, bool save);

bool HavelConfig_has(const HavelConfig *cfg, const char *key);
bool HavelConfig_remove(HavelConfig *cfg, const char *key);

// Key enumeration - caller frees each entry and the array with HavelConfig_freeKeys
size_t HavelConfig_getAllKeys(const HavelConfig *cfg, char ***outKeys);
void HavelConfig_freeKeys(char **keys, size_t count);

// Config as key=value pairs - caller frees with HavelConfig_freeKeys
size_t HavelConfig_getConfigs(const HavelConfig *cfg, char ***outEntries);
void HavelConfig_freeEntries(char **entries, size_t count);

// Path getter - returns pointer valid until next config operation
const char *HavelConfig_getPath(const HavelConfig *cfg);
void HavelConfig_setPath(const char *newPath);

// Convenience getters
bool HavelConfig_getVerboseKeyLogging(const HavelConfig *cfg);
bool HavelConfig_getVerboseWindowLogging(const HavelConfig *cfg);
bool HavelConfig_getVerboseConditionLogging(const HavelConfig *cfg);

// File watching
void HavelConfig_startFileWatching(HavelConfig *cfg, const char *filename);
void HavelConfig_stopFileWatching(HavelConfig *cfg);

// Watch callback - receives key and user data
typedef void (*HavelConfig_WatchCallback)(const char *key, const char *value, void *userData);
void HavelConfig_watch(HavelConfig *cfg, const char *key, HavelConfig_WatchCallback cb,
                       void *userData);

// Print all config to stdout
void HavelConfig_print(const HavelConfig *cfg);

#ifdef __cplusplus
}
#endif

// C convenience macros
#define HAVEL_CFG_GET_STR(key, def) HavelConfig_getString(HavelConfig_getInstance(), key, def)
#define HAVEL_CFG_GET_BOOL(key, def) HavelConfig_getBool(HavelConfig_getInstance(), key, def)
#define HAVEL_CFG_GET_INT(key, def) HavelConfig_getInt(HavelConfig_getInstance(), key, def)
#define HAVEL_CFG_GET_DBL(key, def) HavelConfig_getDouble(HavelConfig_getInstance(), key, def)
#define HAVEL_CFG_SET_STR(key, val, s) HavelConfig_setString(HavelConfig_getInstance(), key, val, s)
#define HAVEL_CFG_SET_BOOL(key, val, s) HavelConfig_setBool(HavelConfig_getInstance(), key, val, s)
#define HAVEL_CFG_SET_INT(key, val, s) HavelConfig_setInt(HavelConfig_getInstance(), key, val, s)
#define HAVEL_CFG_SET_DBL(key, val, s) HavelConfig_setDouble(HavelConfig_getInstance(), key, val, s)

#endif // HAVEL_CONFIG_C_H
