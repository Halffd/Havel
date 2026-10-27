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
#include "LoggerC.h"

#include "havel_cplat.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>

#if HAVEL_PLATFORM_POSIX
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Internal hash map (open-addressing, FNV-1a)
// ---------------------------------------------------------------------------

#define CFG_MAP_INIT_CAP 64

typedef struct CfgEntry {
  char *key;
  char *value;
  bool occupied;
} CfgEntry;

typedef struct CfgMap {
  CfgEntry *entries;
  size_t cap;
  size_t len;
} CfgMap;

static uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

static void cfgMapInit(CfgMap *m) {
  m->cap = CFG_MAP_INIT_CAP;
  m->len = 0;
  m->entries = (CfgEntry *)calloc(m->cap, sizeof(CfgEntry));
}

static void cfgMapFree(CfgMap *m) {
  for (size_t i = 0; i < m->cap; i++) {
    free(m->entries[i].key);
    free(m->entries[i].value);
  }
  free(m->entries);
  m->entries = NULL;
  m->cap = m->len = 0;
}

static size_t cfgMapFindSlot(const CfgMap *m, const char *key) {
  size_t idx = fnv1a(key) & (m->cap - 1);
  for (size_t i = 0; i < m->cap; i++) {
    size_t pos = (idx + i) & (m->cap - 1);
    if (!m->entries[pos].occupied || strcmp(m->entries[pos].key, key) == 0)
      return pos;
  }
  return (size_t)-1;
}

static void cfgMapGrow(CfgMap *m) {
  size_t oldCap = m->cap;
  CfgEntry *old = m->entries;
  m->cap = oldCap * 2;
  m->entries = (CfgEntry *)calloc(m->cap, sizeof(CfgEntry));
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

static void cfgMapSet(CfgMap *m, const char *key, const char *value) {
  if (m->len * 2 >= m->cap)
    cfgMapGrow(m);
  size_t slot = cfgMapFindSlot(m, key);
  if (slot == (size_t)-1)
    return;
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

static const char *cfgMapGet(const CfgMap *m, const char *key) {
  size_t slot = cfgMapFindSlot(m, key);
  if (slot != (size_t)-1 && m->entries[slot].occupied)
    return m->entries[slot].value;
  return NULL;
}

static bool cfgMapRemove(CfgMap *m, const char *key) {
  size_t slot = cfgMapFindSlot(m, key);
  if (slot == (size_t)-1 || !m->entries[slot].occupied)
    return false;
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

static char *trimDup(const char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  const char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1]))
    end--;
  size_t len = (size_t)(end - s);
  char *r = (char *)malloc(len + 1);
  memcpy(r, s, len);
  r[len] = '\0';
  return r;
}

// ---------------------------------------------------------------------------
// Watch callback list
// ---------------------------------------------------------------------------

typedef struct CfgWatch {
  char *key;
  HavelConfig_WatchCallback cb;
  void *userData;
} CfgWatch;

// ---------------------------------------------------------------------------
// HavelConfig struct
// ---------------------------------------------------------------------------

struct HavelConfig {
  CfgMap values;
  char *path;
  CfgWatch *watchers;
  size_t watcherCount;
  size_t watcherCap;
  havel_mutex_t mutex;

  // File watching
  bool watching;
  havel_thread_t watchThread;
  havel_mutex_t watchMutex;

  // Debounced save
  bool savePending;
  havel_thread_t saveThread;
  havel_mutex_t saveMutex;
};

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static HavelConfig g_config;
static bool g_configInit = false;

static void ensureInit(void) {
  if (g_configInit)
    return;
  cfgMapInit(&g_config.values);
  g_config.path = NULL;
  g_config.watchers = NULL;
  g_config.watcherCount = 0;
  g_config.watcherCap = 0;
  havel_mutex_init(&g_config.mutex);
  havel_mutex_init(&g_config.watchMutex);
  havel_mutex_init(&g_config.saveMutex);
  g_config.watching = false;
  g_config.savePending = false;
  g_configInit = true;
}

HavelConfig *HavelConfig_getInstance(void) {
  ensureInit();
  return &g_config;
}

// ---------------------------------------------------------------------------
// Path management (thread-safe via lazy statics)
// ---------------------------------------------------------------------------

static char *g_configDir = NULL;
static char *g_mainConfig = NULL;
static char *g_hotkeysDir = NULL;
static havel_mutex_t g_pathMutex;

/* The path mutex has to be recursive: mkdirRecursive() below walks a path and
   re-enters the directory helpers that take this same lock.

   POSIX gets this ready from a constructor attribute, which runs before main().
   MSVC has no such attribute and nothing runs before main on Windows, so the
   init is lazy and idempotent instead. CRITICAL_SECTION is recursive exactly
   like PTHREAD_MUTEX_RECURSIVE, which is why the two need no other
   difference. */
#if HAVEL_PLATFORM_WINDOWS
static void init_pathMutex(void) {
  InitializeCriticalSection(&g_pathMutex);
}
static havel_once_t g_pathMutexOnce = HAVEL_ONCE_INIT;
#define PATH_MUTEX_LOCK()                                                                          \
  do {                                                                                             \
    havel_once_call(&g_pathMutexOnce, init_pathMutex);                                             \
    havel_mutex_lock(&g_pathMutex);                                                                \
  } while (0)
#define PATH_MUTEX_UNLOCK() havel_mutex_unlock(&g_pathMutex)
#else
static void init_pathMutex(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  /* havel_mutex_init() takes no attributes and would give a NORMAL mutex,
     which deadlocks the moment mkdirRecursive re-enters. Pass attr here. */
  pthread_mutex_init(&g_pathMutex, &attr);
  pthread_mutexattr_destroy(&attr);
}
static void __attribute__((constructor)) init_pathMutexCtor(void) {
  init_pathMutex();
}
#define PATH_MUTEX_LOCK() havel_mutex_lock(&g_pathMutex)
#define PATH_MUTEX_UNLOCK() havel_mutex_unlock(&g_pathMutex)
#endif

static char *defaultConfigDir(void) {
  const char *home = getenv("HOME");
  if (home) {
    size_t len = strlen(home) + strlen("/.config/havel/");
    char *buf = (char *)malloc(len + 1);
    snprintf(buf, len + 1, "%s/.config/havel/", home);
    return buf;
  }
  return strdup("./havel/");
}

static char *getPathDir(void) {
  PATH_MUTEX_LOCK();
  if (!g_configDir) {
    g_configDir = defaultConfigDir();
    size_t len = strlen(g_configDir) + strlen("havel.cfg");
    g_mainConfig = (char *)malloc(len + 1);
    snprintf(g_mainConfig, len + 1, "%shavel.cfg", g_configDir);
    size_t hlen = strlen(g_configDir) + strlen("hotkeys/");
    g_hotkeysDir = (char *)malloc(hlen + 1);
    snprintf(g_hotkeysDir, hlen + 1, "%shotkeys/", g_configDir);
  }
  PATH_MUTEX_UNLOCK();
  return g_configDir;
}

void HavelConfig_setConfigPath(const char *dir, const char *basename) {
  PATH_MUTEX_LOCK();
  free(g_configDir);
  g_configDir = NULL;
  free(g_mainConfig);
  g_mainConfig = NULL;
  free(g_hotkeysDir);
  g_hotkeysDir = NULL;
  getPathDir();
  size_t dlen = strlen(dir);
  g_configDir = (char *)malloc(dlen + 1);
  memcpy(g_configDir, dir, dlen + 1);
  size_t mlen = dlen + strlen(basename ? basename : "havel.cfg");
  g_mainConfig = (char *)malloc(mlen + 1);
  snprintf(g_mainConfig, mlen + 1, "%s%s", dir, basename ? basename : "havel.cfg");
  size_t hlen = dlen + strlen("hotkeys/");
  g_hotkeysDir = (char *)malloc(hlen + 1);
  snprintf(g_hotkeysDir, hlen + 1, "%shotkeys/", dir);
  PATH_MUTEX_UNLOCK();
}

void HavelConfig_getConfigDir(char *buf, size_t bufSize) {
  const char *d = getPathDir();
  snprintf(buf, bufSize, "%s", d);
}

void HavelConfig_getConfigFilePath(const char *filename, char *buf, size_t bufSize) {
  if (filename && strchr(filename, '/')) {
    snprintf(buf, bufSize, "%s", filename);
    return;
  }
  const char *d = getPathDir();
  snprintf(buf, bufSize, "%s%s", d, filename ? filename : "havel.cfg");
}

static void mkdirRecursive(const char *path) {
  char *tmp = strdup(path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      havel_mkdir(tmp);
      *p = '/';
    }
  }
  havel_mkdir(tmp);
  free(tmp);
}

void HavelConfig_ensureConfigDir(void) {
  PATH_MUTEX_LOCK();
  getPathDir();
  struct stat st;
  if (stat(g_configDir, &st) != 0)
    mkdirRecursive(g_configDir);
  if (g_hotkeysDir && stat(g_hotkeysDir, &st) != 0)
    mkdirRecursive(g_hotkeysDir);
  PATH_MUTEX_UNLOCK();
}

// ---------------------------------------------------------------------------
// TOML parsing
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Load / Save / Reload
// ---------------------------------------------------------------------------

// Defined in the save-pipeline block below.
static int copyFileTo(const char *src, const char *dst);
static void cfgParseInto(const char *path, CfgMap *dst, size_t *dupSections, size_t *malformed);
static int cfgRecoverFromBackup(const char *path);

// The single read path. load() and reload() used to carry two copies of this
// logic; they drifted, and reload's copy lacked the zero-length recovery, so a
// truncated file was read as "no settings" instead of being restored.
static void cfgLoadFromDisk(const char *fpath, int notifyWatchers) {
  // A zero-length config is never a legitimate state. Falling back to the
  // newest good backup beats starting up with an empty config, whose only
  // symptom is a downstream "expected object, got null" far from the cause.
  struct stat lst;
  bool recovered = false;
  if (stat(fpath, &lst) == 0 && lst.st_size == 0)
    recovered = cfgRecoverFromBackup(fpath) != 0;

  // Parse into a FRESH map and swap it in. Parsing straight into the live map
  // meant keys deleted from the file survived forever in memory and were
  // written back on the next save.
  CfgMap fresh;
  cfgMapInit(&fresh);
  size_t dups = 0, bad = 0;
  cfgParseInto(fpath, &fresh, &dups, &bad);
  if (dups || bad)
    HAVEL_LOGF_WARN("Config: %s has %zu duplicate section(s) and %zu malformed line(s)", fpath,
                    dups, bad);

  havel_mutex_lock(&g_config.mutex);
  if (notifyWatchers) {
    for (size_t w = 0; w < g_config.watcherCount; w++) {
      CfgWatch *cw = &g_config.watchers[w];
      for (size_t i = 0; i < fresh.cap; i++) {
        if (!fresh.entries[i].occupied)
          continue;
        const char *nk = fresh.entries[i].key;
        const char *nv = fresh.entries[i].value;
        const char *ov = cfgMapGet(&g_config.values, nk);
        if ((!ov || strcmp(ov, nv) != 0) && (strcmp(cw->key, nk) == 0 || cw->key[0] == '\0'))
          cw->cb(nk, nv, cw->userData);
      }
    }
  }
  cfgMapFree(&g_config.values);
  g_config.values = fresh;
  havel_mutex_unlock(&g_config.mutex);

  // A file that does not exist yet (first run) is normal; a file that exists
  // with content but parses to nothing is a real problem worth shouting about.
  if (fresh.len == 0 && !recovered && stat(fpath, &lst) == 0 && lst.st_size > 0)
    HAVEL_LOGF_WARN("Config: %s yielded no keys", fpath);
}

void HavelConfig_load(HavelConfig *cfg, const char *filename) {
  (void)cfg;
  char fpath[4096];
  HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));

  havel_mutex_lock(&g_config.mutex);
  free(g_config.path);
  g_config.path = strdup(fpath);
  havel_mutex_unlock(&g_config.mutex);

  cfgLoadFromDisk(fpath, 0);
}

static int cmpStr(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

// ===========================================================================
// Canonical serialization, diffing, atomic writes, backups, validation
// ===========================================================================
//
// The config file is the user's data: it survives across processes and is
// edited by hand. Everything below exists because a plain
// fopen(path,"w") + fprintf loop can, and did, destroy it:
//
//   - a crash or a kill mid-write leaves a truncated file (the 0-byte
//     artifacts a killed linker leaves behind are the same failure mode);
//   - rewriting when nothing changed needlessly churns the file and burns
//     backup slots, so real changes become invisible;
//   - a bad write had no way back, so a single mistake cost every setting.
//
// The pipeline is therefore: serialize in memory -> diff against disk ->
// only if it differs, rotate a backup and write via temp+fsync+rename.

#define CFG_BACKUP_COUNT 3
#define CFG_MAX_REPORT_KEYS 64

// Growable text buffer.
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} TextBuf;

static void tbInit(TextBuf *b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static int tbReserve(TextBuf *b, size_t extra) {
  if (b->len + extra + 1 <= b->cap)
    return 1;
  size_t want = b->cap ? b->cap : 4096;
  while (want < b->len + extra + 1)
    want *= 2;
  char *p = (char *)realloc(b->data, want);
  if (!p)
    return 0;
  b->data = p;
  b->cap = want;
  return 1;
}

static void tbAppend(TextBuf *b, const char *s, size_t n) {
  if (!tbReserve(b, n))
    return;
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void tbPuts(TextBuf *b, const char *s) {
  tbAppend(b, s, strlen(s));
}

// ---------------------------------------------------------------------------
// Canonical serializer: root keys first, then exactly one [Section] header per
// section, keys sorted within each. This is the ONLY writer of the file format;
// a second writer is how the duplicate-header corruption got in.
// ---------------------------------------------------------------------------
static int cfgSerialize(const CfgMap *m, char **outText) {
  size_t nKeys = 0;
  for (size_t i = 0; i < m->cap; i++)
    if (m->entries[i].occupied)
      nKeys++;
  if (nKeys == 0) {
    *outText = strdup("");
    return *outText != NULL;
  }

  char **keys = (char **)malloc(nKeys * sizeof(char *));
  if (!keys)
    return 0;
  size_t ki = 0;
  for (size_t i = 0; i < m->cap; i++)
    if (m->entries[i].occupied)
      keys[ki++] = m->entries[i].key;
  qsort(keys, ki, sizeof(char *), cmpStr);

  TextBuf b;
  tbInit(&b);
  if (!tbReserve(&b, 8192)) {
    free(keys);
    return 0;
  }
  b.data[0] = '\0';

  // Root keys must precede any [table]: in TOML a bare key written after a
  // table header belongs to that table.
  for (size_t i = 0; i < ki; i++) {
    if (strchr(keys[i], '.'))
      continue;
    const char *val = cfgMapGet(m, keys[i]);
    if (!val)
      continue;
    tbPuts(&b, keys[i]);
    tbPuts(&b, " = ");
    // writeTomlValue writes to a FILE; mirror it into the buffer by hand.
    size_t vlen = strlen(val);
    int bare = (strcmp(val, "true") == 0 || strcmp(val, "false") == 0);
    if (!bare && vlen >= 2 &&
        ((val[0] == '"' && val[vlen - 1] == '"') || (val[0] == '\'' && val[vlen - 1] == '\'')))
      bare = 1;
    if (!bare && vlen > 0 && (val[0] == '[' || val[0] == '{'))
      bare = 1;
    if (!bare) {
      char *end;
      strtod(val, &end);
      if (*end == '\0' && end != val)
        bare = 1;
    }
    if (bare) {
      tbPuts(&b, val);
    } else {
      tbPuts(&b, "\"");
      tbPuts(&b, val);
      tbPuts(&b, "\"");
    }
    tbPuts(&b, "\n");
  }

  // One header per section. Compare against the PREVIOUS KEY's section
  // prefix: the earlier version stored the whole previous key
  // ("IO.Backend") as "lastSection", compared the next section's length
  // against strlen("IO.Backend"), and therefore never matched - every key
  // got its own [Section] header. That output redefines tables, which TOML
  // forbids, so a later load lost the multi-key sections entirely.
  const char *lastKey = NULL;
  for (size_t i = 0; i < ki; i++) {
    const char *dot = strchr(keys[i], '.');
    if (!dot)
      continue;
    const char *val = cfgMapGet(m, keys[i]);
    if (!val)
      continue;
    size_t secLen = (size_t)(dot - keys[i]);
    int sameSection = 0;
    if (lastKey) {
      const char *lastDot = strchr(lastKey, '.');
      if (lastDot && (size_t)(lastDot - lastKey) == secLen &&
          strncmp(keys[i], lastKey, secLen) == 0)
        sameSection = 1;
    }
    if (!sameSection) {
      if (b.len > 0)
        tbPuts(&b, "\n");
      tbPuts(&b, "[");
      tbAppend(&b, keys[i], secLen);
      tbPuts(&b, "]\n");
    }
    tbPuts(&b, dot + 1);
    tbPuts(&b, " = ");
    size_t vlen = strlen(val);
    int bare = (strcmp(val, "true") == 0 || strcmp(val, "false") == 0);
    if (!bare && vlen >= 2 &&
        ((val[0] == '"' && val[vlen - 1] == '"') || (val[0] == '\'' && val[vlen - 1] == '\'')))
      bare = 1;
    if (!bare && vlen > 0 && (val[0] == '[' || val[0] == '{'))
      bare = 1;
    if (!bare) {
      char *end;
      strtod(val, &end);
      if (*end == '\0' && end != val)
        bare = 1;
    }
    if (bare) {
      tbPuts(&b, val);
    } else {
      tbPuts(&b, "\"");
      tbPuts(&b, val);
      tbPuts(&b, "\"");
    }
    tbPuts(&b, "\n");
    lastKey = keys[i];
  }

  free(keys);
  if (!b.data) {
    *outText = strdup("");
    return 1;
  }
  *outText = b.data;
  return 1;
}

// ---------------------------------------------------------------------------
// Diff: what a save would add / remove / change relative to disk.
// ---------------------------------------------------------------------------
static char *cfgReadWholeFile(const char *path, size_t *outLen) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (outLen)
      *outLen = 0;
    return NULL;
  }
  TextBuf b;
  tbInit(&b);
  if (!tbReserve(&b, 8192)) {
    fclose(f);
    return NULL;
  }
  b.data[0] = '\0';
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    tbAppend(&b, chunk, n);
  fclose(f);
  if (outLen)
    *outLen = b.len;
  if (!b.data)
    b.data = strdup("");
  return b.data;
}

// Parse a file into a fresh map without touching global state.
static void cfgParseInto(const char *path, CfgMap *dst, size_t *dupSections, size_t *malformed) {
  size_t dups = 0, bad = 0;
  FILE *f = fopen(path, "r");
  if (f) {
    char *section = NULL;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = '\0';
      char *sp = line;
      while (*sp && isspace((unsigned char)*sp))
        sp++;
      size_t len = strlen(sp);
      while (len > 0 && isspace((unsigned char)sp[len - 1]))
        sp[--len] = '\0';
      if (!*sp || *sp == '#' || *sp == ';')
        continue;
      if (*sp == '[' && len >= 2 && sp[len - 1] == ']') {
        sp[len - 1] = '\0';
        char *sec = sp + 1;
        if (*sec == '[' && len >= 3 && sp[len - 2] == ']') {
          sp[len - 2] = '\0';
          sec++;
        }
        char *trimmed = trimDup(sec);
        if (section && *section && strcmp(section, trimmed) != 0) {
          // A different section: fine. Same section repeated is the
          // redefinition the old writer produced.
        } else if (section && *section) {
          dups++;
        }
        free(section);
        section = trimmed;
        continue;
      }
      char *eq = strchr(sp, '=');
      if (!eq) {
        bad++;
        continue;
      }
      *eq = '\0';
      char *key = trimDup(sp);
      char *val = trimDup(eq + 1);
      size_t vlen = strlen(val);
      if (vlen >= 2 &&
          ((val[0] == '"' && val[vlen - 1] == '"') || (val[0] == '\'' && val[vlen - 1] == '\''))) {
        memmove(val, val + 1, vlen - 2);
        val[vlen - 2] = '\0';
      }
      if (section && *section) {
        size_t klen = strlen(section) + strlen(key) + 2;
        char *fullKey = (char *)malloc(klen);
        snprintf(fullKey, klen, "%s.%s", section, key);
        cfgMapSet(dst, fullKey, val);
        free(fullKey);
      } else {
        cfgMapSet(dst, key, val);
      }
      free(key);
      free(val);
    }
    fclose(f);
    free(section);
  }
  if (dupSections)
    *dupSections = dups;
  if (malformed)
    *malformed = bad;
}

static void cfgDiffMaps(const CfgMap *mem, const CfgMap *disk, HavelConfigSaveReport *rep) {
  for (size_t i = 0; i < mem->cap; i++) {
    if (!mem->entries[i].occupied)
      continue;
    const char *k = mem->entries[i].key;
    const char *v = mem->entries[i].value;
    const char *dv = cfgMapGet(disk, k);
    if (!dv)
      rep->added++;
    else if (strcmp(dv, v) != 0)
      rep->changed++;
  }
  for (size_t i = 0; i < disk->cap; i++) {
    if (!disk->entries[i].occupied)
      continue;
    if (!cfgMapGet(mem, disk->entries[i].key))
      rep->removed++;
  }
}

// ---------------------------------------------------------------------------
// Atomic write: temp file in the SAME directory, fsync, rename, fsync dir.
// A reader either sees the old file or the new one, never a partial one.
// ---------------------------------------------------------------------------
static int cfgWriteAtomic(const char *path, const char *text) {
  char tmp[4200];
  snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    HAVEL_LOGF_ERROR("Config: cannot create temp file %s", tmp);
    return 0;
  }
  size_t len = strlen(text);
  if (len > 0 && fwrite(text, 1, len, f) != len) {
    fclose(f);
    remove(tmp);
    HAVEL_LOGF_ERROR("Config: short write to %s", tmp);
    return 0;
  }
  if (fflush(f) != 0) {
    fclose(f);
    remove(tmp);
    return 0;
  }
  if (fsync(fileno(f)) != 0 && errno != EINVAL) {
    fclose(f);
    remove(tmp);
    HAVEL_LOGF_ERROR("Config: fsync failed for %s", tmp);
    return 0;
  }
  if (fclose(f) != 0) {
    remove(tmp);
    return 0;
  }
  if (rename(tmp, path) != 0) {
    remove(tmp);
    HAVEL_LOGF_ERROR("Config: rename %s -> %s failed: %s", tmp, path, strerror(errno));
    return 0;
  }
  // fsync the directory so the rename itself is durable.
  char dir[4096];
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (slash) {
    *slash = '\0';
    int dfd = open(dir, O_RDONLY);
    if (dfd >= 0) {
      fsync(dfd);
      close(dfd);
    }
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Backup ring: current -> .bak1 -> .bak2 -> .bak3, only when about to
// overwrite with genuinely different content.
// ---------------------------------------------------------------------------
static void cfgBackupRotate(const char *path, char *outBackup, size_t outSz) {
  struct stat st;
  if (stat(path, &st) != 0 || st.st_size == 0)
    return; // nothing worth keeping

  char oldest[4200], prev[4200];
  snprintf(oldest, sizeof(oldest), "%s.bak%d", path, CFG_BACKUP_COUNT);
  remove(oldest);
  for (int i = CFG_BACKUP_COUNT - 1; i >= 1; i--) {
    snprintf(prev, sizeof(prev), "%s.bak%d", path, i);
    snprintf(oldest, sizeof(oldest), "%s.bak%d", path, i + 1);
    rename(prev, oldest);
  }
  snprintf(prev, sizeof(prev), "%s.bak1", path);
  if (copyFileTo(path, prev) && outBackup && outSz > 0)
    snprintf(outBackup, outSz, "%s", prev);
}

static int copyFileTo(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in)
    return 0;
  FILE *out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return 0;
  }
  char chunk[8192];
  size_t n;
  int ok = 1;
  while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
    if (fwrite(chunk, 1, n, out) != n) {
      ok = 0;
      break;
    }
  }
  fclose(in);
  if (fflush(out) == 0)
    fsync(fileno(out));
  fclose(out);
  if (!ok)
    remove(dst);
  return ok;
}

// Recovery: if the live file is gone or unreadable-as-config, adopt the
// newest backup instead of silently continuing with an empty config.
static int cfgRecoverFromBackup(const char *path) {
  for (int i = 1; i <= CFG_BACKUP_COUNT; i++) {
    char bak[4200];
    snprintf(bak, sizeof(bak), "%s.bak%d", path, i);
    struct stat st;
    if (stat(bak, &st) != 0 || st.st_size == 0)
      continue;
    CfgMap probe;
    cfgMapInit(&probe);
    size_t dups = 0, bad = 0;
    cfgParseInto(bak, &probe, &dups, &bad);
    size_t keys = probe.len;
    cfgMapFree(&probe);
    if (keys == 0)
      continue;
    HAVEL_LOGF_WARN("Config: %s is missing or unreadable; recovered from %s", path, bak);
    return copyFileTo(bak, path);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Save pipeline: serialize -> diff vs disk -> backup + atomic write only when
// the content actually differs.
// ---------------------------------------------------------------------------
void HavelConfig_saveWithReport(HavelConfig *cfg, const char *filename,
                                HavelConfigSaveReport *out) {
  (void)cfg;
  HavelConfigSaveReport rep;
  memset(&rep, 0, sizeof(rep));

  char fpath[4096];
  if (filename && *filename) {
    HavelConfig_getConfigFilePath(filename, fpath, sizeof(fpath));
  } else {
    havel_mutex_lock(&g_config.mutex);
    if (!g_config.path || !*g_config.path) {
      havel_mutex_unlock(&g_config.mutex);
      if (out)
        *out = rep;
      return;
    }
    snprintf(fpath, sizeof(fpath), "%s", g_config.path);
    havel_mutex_unlock(&g_config.mutex);
  }

  HavelConfig_ensureConfigDir();

  // Serialize a consistent snapshot under the lock, then do all file work
  // outside it so a slow disk cannot block readers.
  char *text = NULL;
  havel_mutex_lock(&g_config.mutex);
  int serialized = cfgSerialize(&g_config.values, &text);
  havel_mutex_unlock(&g_config.mutex);
  if (!serialized || !text) {
    HAVEL_LOGF_ERROR("Config: serialization failed; %s left untouched", fpath);
    if (out)
      *out = rep;
    return;
  }

  // What is on disk right now, and is it even readable?
  CfgMap disk;
  cfgMapInit(&disk);
  size_t dups = 0, bad = 0;
  struct stat st;
  if (stat(fpath, &st) == 0 && st.st_size == 0) {
    // A zero-length config is not a legitimate state - recover before
    // diffing, otherwise the diff would read as "everything added" and the
    // empty state would be written back as if it were intended.
    if (cfgRecoverFromBackup(fpath))
      rep.recovered = true;
  }
  cfgParseInto(fpath, &disk, &dups, &bad);
  rep.duplicates = dups;
  rep.malformed = bad;

  size_t diskLen = 0;
  char *diskText = cfgReadWholeFile(fpath, &diskLen);
  if (diskText && diskLen == strlen(text) && memcmp(diskText, text, diskLen) == 0) {
    // Nothing changed: do not rewrite, do not burn a backup slot. This is
    // what makes a later real change visible in the diff.
    free(diskText);
    free(text);
    cfgMapFree(&disk);
    rep.written = false;
    if (out)
      *out = rep;
    return;
  }
  free(diskText);

  cfgDiffMaps(&g_config.values, &disk, &rep);
  cfgMapFree(&disk);

  if (rep.recovered || rep.duplicates || rep.malformed) {
    // The file we are about to replace was malformed: say so loudly instead
    // of silently "fixing" the user's file.
    HAVEL_LOGF_WARN("Config: rewriting %s (recovered=%d duplicate sections=%d malformed lines=%d)",
                    fpath, (int)rep.recovered, (int)rep.duplicates, (int)rep.malformed);
  }

  cfgBackupRotate(fpath, rep.backup, sizeof(rep.backup));

  if (!cfgWriteAtomic(fpath, text)) {
    free(text);
    if (out)
      *out = rep;
    return;
  }
  free(text);
  rep.written = true;

  if (rep.added || rep.removed || rep.changed) {
    HAVEL_LOGF_INFO("Config: saved %s (+%zu ~%zu -%zu)", fpath, rep.added, rep.changed,
                    rep.removed);
  }

  if (out)
    *out = rep;
}

void HavelConfig_save(HavelConfig *cfg, const char *filename) {
  HavelConfig_saveWithReport(cfg, filename, NULL);
}

void HavelConfig_diffToDisk(HavelConfig *cfg, HavelConfigSaveReport *out) {
  (void)cfg;
  HavelConfigSaveReport rep;
  memset(&rep, 0, sizeof(rep));

  char fpath[4096];
  havel_mutex_lock(&g_config.mutex);
  if (!g_config.path || !*g_config.path) {
    havel_mutex_unlock(&g_config.mutex);
    if (out)
      *out = rep;
    return;
  }
  snprintf(fpath, sizeof(fpath), "%s", g_config.path);
  havel_mutex_unlock(&g_config.mutex);

  CfgMap disk;
  cfgMapInit(&disk);
  size_t dups = 0, bad = 0;
  cfgParseInto(fpath, &disk, &dups, &bad);
  rep.duplicates = dups;
  rep.malformed = bad;
  cfgDiffMaps(&g_config.values, &disk, &rep);
  cfgMapFree(&disk);
  if (out)
    *out = rep;
}

int HavelConfig_validateFile(const char *filename, size_t *duplicateSections,
                             size_t *malformedLines) {
  char fpath[4096];
  HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));
  struct stat st;
  if (stat(fpath, &st) != 0) {
    if (duplicateSections)
      *duplicateSections = 0;
    if (malformedLines)
      *malformedLines = 0;
    return -1; // missing
  }
  CfgMap probe;
  cfgMapInit(&probe);
  size_t dups = 0, bad = 0;
  cfgParseInto(fpath, &probe, &dups, &bad);
  cfgMapFree(&probe);
  if (duplicateSections)
    *duplicateSections = dups;
  if (malformedLines)
    *malformedLines = bad;
  return (int)(dups + bad);
}

size_t HavelConfig_listBackups(const char *filename, char ***outPaths) {
  char fpath[4096];
  HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));
  char **paths = (char **)calloc(CFG_BACKUP_COUNT, sizeof(char *));
  size_t n = 0;
  if (!paths) {
    *outPaths = NULL;
    return 0;
  }
  for (int i = 1; i <= CFG_BACKUP_COUNT; i++) {
    char bak[4200];
    snprintf(bak, sizeof(bak), "%s.bak%d", fpath, i);
    struct stat st;
    if (stat(bak, &st) != 0 || st.st_size == 0)
      continue;
    paths[n++] = strdup(bak);
  }
  *outPaths = paths;
  return n;
}

void HavelConfig_freeBackups(char **paths, size_t count) {
  if (!paths)
    return;
  for (size_t i = 0; i < count; i++)
    free(paths[i]);
  free(paths);
}

bool HavelConfig_restoreBackup(HavelConfig *cfg, const char *filename, size_t index) {
  (void)cfg;
  char fpath[4096];
  HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));

  char bak[4200];
  snprintf(bak, sizeof(bak), "%s.bak%d", fpath, (int)index + 1);
  size_t bakLen = 0;
  char *content = cfgReadWholeFile(bak, &bakLen);
  if (!content || bakLen == 0) {
    free(content);
    return false;
  }

  // Rotate the live file only AFTER taking a copy of the backup, because
  // rotating pushes the live file into .bak1 - the very slot being restored
  // from. Rotating first silently restored the file that was already there.
  cfgBackupRotate(fpath, NULL, 0);
  int ok = cfgWriteAtomic(fpath, content);
  free(content);
  if (!ok)
    return false;

  HAVEL_LOGF_INFO("Config: restored %s from %s", fpath, bak);
  HavelConfig_reload(HavelConfig_getInstance());
  return true;
}

void HavelConfig_reload(HavelConfig *cfg) {
  char fpath[4096];
  havel_mutex_lock(&g_config.mutex);
  if (!g_config.path || !*g_config.path) {
    havel_mutex_unlock(&g_config.mutex);
    return;
  }
  snprintf(fpath, sizeof(fpath), "%s", g_config.path);
  havel_mutex_unlock(&g_config.mutex);

  cfgLoadFromDisk(fpath, 1);
}

// ---------------------------------------------------------------------------
// Debounced save
// ---------------------------------------------------------------------------

static void *saveThreadFunc(void *arg) {
  HavelConfig *cfg = (HavelConfig *)arg;
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

void HavelConfig_requestSave(HavelConfig *cfg) {
  cfg->savePending = true;
  havel_mutex_lock(&cfg->saveMutex);
  if (!cfg->saveThread) {
    havel_thread_create(&cfg->saveThread, saveThreadFunc, cfg);
    havel_thread_detach(cfg->saveThread);
  }
  havel_mutex_unlock(&cfg->saveMutex);
}

void HavelConfig_forceSave(HavelConfig *cfg) {
  cfg->savePending = false;
  HavelConfig_save(cfg, NULL);
}

// ---------------------------------------------------------------------------
// Ensure config file with defaults
// ---------------------------------------------------------------------------

void HavelConfig_ensureConfigFile(HavelConfig *cfg, const char *filename) {
  char fpath[4096];
  HavelConfig_getConfigFilePath(filename ? filename : "havel.cfg", fpath, sizeof(fpath));

  havel_mutex_lock(&cfg->mutex);
  free(cfg->path);
  cfg->path = strdup(fpath);
  havel_mutex_unlock(&cfg->mutex);

  HavelConfig_ensureConfigDir();

  struct stat st;
  if (stat(fpath, &st) == 0)
    return;

  FILE *f = fopen(fpath, "w");
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

const char *HavelConfig_getString(const HavelConfig *cfg, const char *key, const char *defaultVal) {
  (void)cfg;
  havel_mutex_lock((havel_mutex_t *)&g_config.mutex);
  const char *v = cfgMapGet(&g_config.values, key);
  havel_mutex_unlock((havel_mutex_t *)&g_config.mutex);
  return v ? v : (defaultVal ? defaultVal : "");
}

bool HavelConfig_getBool(const HavelConfig *cfg, const char *key, bool defaultVal) {
  const char *v = HavelConfig_getString(cfg, key, NULL);
  if (!v)
    return defaultVal;
  if (havel_strcasecmp(v, "true") == 0 || havel_strcasecmp(v, "yes") == 0 ||
      havel_strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0)
    return true;
  if (havel_strcasecmp(v, "false") == 0 || havel_strcasecmp(v, "no") == 0 ||
      havel_strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0)
    return false;
  return defaultVal;
}

int HavelConfig_getInt(const HavelConfig *cfg, const char *key, int defaultVal) {
  const char *v = HavelConfig_getString(cfg, key, NULL);
  if (!v)
    return defaultVal;
  char *end;
  long n = strtol(v, &end, 10);
  return (*end == '\0') ? (int)n : defaultVal;
}

double HavelConfig_getDouble(const HavelConfig *cfg, const char *key, double defaultVal) {
  const char *v = HavelConfig_getString(cfg, key, NULL);
  if (!v)
    return defaultVal;
  char *end;
  double d = strtod(v, &end);
  return (*end == '\0') ? d : defaultVal;
}

static void notifyWatchers(HavelConfig *cfg, const char *key, const char *value) {
  for (size_t i = 0; i < cfg->watcherCount; i++) {
    CfgWatch *cw = &cfg->watchers[i];
    if (strcmp(cw->key, key) == 0 || cw->key[0] == '\0') {
      cw->cb(key, value, cw->userData);
    }
  }
}

void HavelConfig_setString(HavelConfig *cfg, const char *key, const char *value, bool save) {
  havel_mutex_lock(&cfg->mutex);
  cfgMapSet(&cfg->values, key, value);
  notifyWatchers(cfg, key, value);
  havel_mutex_unlock(&cfg->mutex);
  if (save)
    HavelConfig_requestSave(cfg);
}

void HavelConfig_setBool(HavelConfig *cfg, const char *key, bool value, bool save) {
  HavelConfig_setString(cfg, key, value ? "true" : "false", save);
}

void HavelConfig_setInt(HavelConfig *cfg, const char *key, int value, bool save) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", value);
  HavelConfig_setString(cfg, key, buf, save);
}

void HavelConfig_setDouble(HavelConfig *cfg, const char *key, double value, bool save) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.17g", value);
  HavelConfig_setString(cfg, key, buf, save);
}

bool HavelConfig_has(const HavelConfig *cfg, const char *key) {
  havel_mutex_lock((havel_mutex_t *)&g_config.mutex);
  bool found = cfgMapGet(&g_config.values, key) != NULL;
  havel_mutex_unlock((havel_mutex_t *)&g_config.mutex);
  return found;
}

bool HavelConfig_remove(HavelConfig *cfg, const char *key) {
  havel_mutex_lock(&cfg->mutex);
  bool removed = cfgMapRemove(&cfg->values, key);
  havel_mutex_unlock(&cfg->mutex);
  if (removed)
    HavelConfig_requestSave(cfg);
  return removed;
}

// ---------------------------------------------------------------------------
// Key enumeration
// ---------------------------------------------------------------------------

size_t HavelConfig_getAllKeys(const HavelConfig *cfg, char ***outKeys) {
  havel_mutex_lock((havel_mutex_t *)&g_config.mutex);
  size_t n = g_config.values.len;
  char **arr = (char **)malloc(n * sizeof(char *));
  size_t ki = 0;
  for (size_t i = 0; i < g_config.values.cap; i++) {
    if (g_config.values.entries[i].occupied)
      arr[ki++] = strdup(g_config.values.entries[i].key);
  }
  havel_mutex_unlock((havel_mutex_t *)&g_config.mutex);
  *outKeys = arr;
  return ki;
}

void HavelConfig_freeKeys(char **keys, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(keys[i]);
  free(keys);
}

size_t HavelConfig_getConfigs(const HavelConfig *cfg, char ***outEntries) {
  havel_mutex_lock((havel_mutex_t *)&g_config.mutex);
  size_t n = g_config.values.len;
  char **arr = (char **)malloc(n * sizeof(char *));
  size_t ki = 0;
  for (size_t i = 0; i < g_config.values.cap; i++) {
    if (g_config.values.entries[i].occupied) {
      size_t len =
          strlen(g_config.values.entries[i].key) + 1 + strlen(g_config.values.entries[i].value) + 1;
      char *entry = (char *)malloc(len);
      snprintf(entry, len, "%s=%s", g_config.values.entries[i].key,
               g_config.values.entries[i].value);
      arr[ki++] = entry;
    }
  }
  havel_mutex_unlock((havel_mutex_t *)&g_config.mutex);
  *outEntries = arr;
  return ki;
}

void HavelConfig_freeEntries(char **entries, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(entries[i]);
  free(entries);
}

// ---------------------------------------------------------------------------
// Path getters
// ---------------------------------------------------------------------------

const char *HavelConfig_getPath(const HavelConfig *cfg) {
  (void)cfg;
  return g_config.path ? g_config.path : "";
}

void HavelConfig_setPath(const char *newPath) {
  const char *lastSlash = strrchr(newPath, '/');
  if (lastSlash) {
    size_t dlen = (size_t)(lastSlash - newPath) + 1;
    char *dir = (char *)malloc(dlen + 1);
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

bool HavelConfig_getVerboseKeyLogging(const HavelConfig *cfg) {
  return HavelConfig_getBool(cfg, "Debug.VerboseKeyLogging", false);
}

bool HavelConfig_getVerboseWindowLogging(const HavelConfig *cfg) {
  return HavelConfig_getBool(cfg, "Debug.VerboseWindowLogging", false);
}

bool HavelConfig_getVerboseConditionLogging(const HavelConfig *cfg) {
  return HavelConfig_getBool(cfg, "Debug.VerboseConditionLogging", false);
}

// ---------------------------------------------------------------------------
// File watching
//
// POSIX watches with inotify and falls back to polling when inotify is
// unavailable. Windows has no inotify at all, so it takes the polling path
// directly. That is the same code the POSIX fallback already runs, at the same
// one-second granularity its poll() timeout used, so the observable behaviour
// - reload when the file's mtime moves forward - is unchanged.
// ---------------------------------------------------------------------------

static time_t getLastModified(const char *filepath) {
#if HAVEL_PLATFORM_WINDOWS
  const int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
  if (wlen <= 0)
    return 0;
  wchar_t *wpath = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wpath)
    return 0;
  MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);

  WIN32_FILE_ATTRIBUTE_DATA data;
  const BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
  free(wpath);
  if (!ok)
    return 0;

  ULARGE_INTEGER stamp;
  stamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
  stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
  /* Win32 counts 100ns ticks from 1601-01-01; time_t counts seconds from
     1970-01-01. 11644473600 is exactly that gap in seconds. */
  return (time_t)((stamp.QuadPart - 116444736000000000ULL) / 10000000ULL);
#else
  struct stat st;
  if (stat(filepath, &st) == 0)
    return st.st_mtime;
  return 0;
#endif
}

static void *watchThreadFunc(void *arg) {
  HavelConfig *cfg = (HavelConfig *)arg;

  havel_mutex_lock(&cfg->mutex);
  char *fpath = cfg->path ? strdup(cfg->path) : NULL;
  havel_mutex_unlock(&cfg->mutex);

  if (!fpath)
    return NULL;

#if HAVEL_PLATFORM_WINDOWS
  /* No inotify on Windows, so this is the polling path natively rather than
     as a degraded fallback. The one-second tick matches the granularity the
     POSIX poll() timeout used, so a reload still lands within a second of
     the file changing. Sleep() is interruptible-free here by design: the
     loop re-reads cfg->watching every tick, which is what lets
     HavelConfig_stopFileWatching() join this thread. */
  time_t lastMod = getLastModified(fpath);
  while (cfg->watching) {
    Sleep(1000);
    time_t curMod = getLastModified(fpath);
    if (curMod > lastMod) {
      lastMod = curMod;
      HavelConfig_reload(cfg);
    }
  }
  free(fpath);
  return NULL;
#else
  int fd = inotify_init1(IN_NONBLOCK);
  if (fd == -1) {
    HAVEL_LOG_WARN("inotify_init failed, falling back to polling");
    time_t lastMod = getLastModified(fpath);
    while (cfg->watching) {
#if HAVEL_PLATFORM_WINDOWS
      Sleep(1000);
#else
      struct timespec ts = {1, 0};
      nanosleep(&ts, NULL);
#endif
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
#endif // HAVEL_PLATFORM_WINDOWS
}

void HavelConfig_startFileWatching(HavelConfig *cfg, const char *filename) {
  havel_mutex_lock(&cfg->watchMutex);
  if (cfg->watching) {
    havel_mutex_unlock(&cfg->watchMutex);
    return;
  }
  HavelConfig_ensureConfigFile(cfg, filename ? filename : "havel.cfg");
  cfg->watching = true;
  havel_thread_create(&cfg->watchThread, watchThreadFunc, cfg);
  havel_mutex_unlock(&cfg->watchMutex);
}

void HavelConfig_stopFileWatching(HavelConfig *cfg) {
  havel_mutex_lock(&cfg->watchMutex);
  cfg->watching = false;
  if (cfg->watchThread) {
    havel_thread_join(cfg->watchThread, NULL);
    cfg->watchThread = HAVEL_THREAD_NULL;
  }
  havel_mutex_unlock(&cfg->watchMutex);
}

// ---------------------------------------------------------------------------
// Watch callbacks
// ---------------------------------------------------------------------------

void HavelConfig_watch(HavelConfig *cfg, const char *key, HavelConfig_WatchCallback cb,
                       void *userData) {
  havel_mutex_lock(&cfg->mutex);
  if (cfg->watcherCount >= cfg->watcherCap) {
    size_t newCap = cfg->watcherCap ? cfg->watcherCap * 2 : 8;
    cfg->watchers = (CfgWatch *)realloc(cfg->watchers, newCap * sizeof(CfgWatch));
    cfg->watcherCap = newCap;
  }
  cfg->watchers[cfg->watcherCount].key = strdup(key ? key : "");
  cfg->watchers[cfg->watcherCount].cb = cb;
  cfg->watchers[cfg->watcherCount].userData = userData;
  cfg->watcherCount++;
  havel_mutex_unlock(&cfg->mutex);
}

// ---------------------------------------------------------------------------
// Print
// ---------------------------------------------------------------------------

void HavelConfig_print(const HavelConfig *cfg) {
  (void)cfg;
  printf("=== Config Values ===\n");
  havel_mutex_lock((havel_mutex_t *)&g_config.mutex);
  for (size_t i = 0; i < g_config.values.cap; i++) {
    if (g_config.values.entries[i].occupied) {
      printf("%s = %s\n", g_config.values.entries[i].key, g_config.values.entries[i].value);
    }
  }
  havel_mutex_unlock((havel_mutex_t *)&g_config.mutex);
  printf("====================\n");
}
