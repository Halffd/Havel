#ifndef HAVEL_CPLAT_H
#define HAVEL_CPLAT_H

/*
 * havel_cplat.h - C-callable platform shim for src/c (PLAT-001).
 *
 * src/c is deliberately plain C, so it cannot use the C++ shims under
 * src/havel-lang/compiler/platform/. It still needs the four primitives every
 * other language in the tree takes for granted: a mutex, a directory walk, a
 * mkdir and an unlink.
 *
 * This header exists so those four have exactly one implementation. The
 * alternative - open-coding #ifdef blocks at each call site - produces a second
 * mutex wrapper and a second directory walk per file, and they drift.
 *
 * Every Windows function is resolved from the API already linked into the
 * process. Nothing here is loaded dynamically, so there is no startup-order
 * hazard: the shim is header-only and does no work before main().
 */

#include "havel_platform.h"

#if HAVEL_PLATFORM_WINDOWS
/* WIN32_LEAN_AND_MEAN keeps windows.h from dragging in winsock and COM, which
 * this shim has no use for and which redefine a lot of common identifiers. */
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <direct.h>   /* _mkdir */
#  include <stdio.h>    /* _unlink */
#  include <string.h>   /* _stricmp */
#else
#  include <pthread.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <dirent.h>
#  include <strings.h>  /* strcasecmp */
#endif

#include <stddef.h>
#include <string.h>

/* Longest path this shim will build a search pattern for. Matches the buffer
   size src/c/LoggerC.c already uses for paths, so a path that fits the
   caller's buffer fits here. */
#define HAVEL_CPLAT_PATH_MAX 4096

#ifdef __cplusplus
#  define HAVEL_CPLAT_INLINE inline
#else
#  define HAVEL_CPLAT_INLINE static inline
#endif

/* ---------------------------------------------------------------------------
 * Mutex
 *
 * CRITICAL_SECTION is the right Windows counterpart: it is recursive, exactly
 * like a default-initialised pthread_mutex_t, so a call path that re-enters
 * the lock behaves the same on both platforms. A non-recursive SRWLOCK would
 * deadlock where the POSIX build does not.
 * ------------------------------------------------------------------------ */

#if HAVEL_PLATFORM_WINDOWS
typedef CRITICAL_SECTION havel_mutex_t;
HAVEL_CPLAT_INLINE void havel_mutex_init(havel_mutex_t* m) { InitializeCriticalSection(m); }
HAVEL_CPLAT_INLINE void havel_mutex_destroy(havel_mutex_t* m) { DeleteCriticalSection(m); }
HAVEL_CPLAT_INLINE void havel_mutex_lock(havel_mutex_t* m) { EnterCriticalSection(m); }
HAVEL_CPLAT_INLINE void havel_mutex_unlock(havel_mutex_t* m) { LeaveCriticalSection(m); }
#else
typedef pthread_mutex_t havel_mutex_t;
HAVEL_CPLAT_INLINE void havel_mutex_init(havel_mutex_t* m) { pthread_mutex_init(m, NULL); }
HAVEL_CPLAT_INLINE void havel_mutex_destroy(havel_mutex_t* m) { pthread_mutex_destroy(m); }
HAVEL_CPLAT_INLINE void havel_mutex_lock(havel_mutex_t* m) { pthread_mutex_lock(m); }
HAVEL_CPLAT_INLINE void havel_mutex_unlock(havel_mutex_t* m) { pthread_mutex_unlock(m); }
#endif

/* ---------------------------------------------------------------------------
 * Directory walk
 *
 * Only the entry name is exposed, because that is all src/c consumes (the
 * log-cleanup sweep matches on the date prefix in the filename). Exposing the
 * name alone also means the Windows side does not have to invent a stat for
 * each entry just to keep a struct shape both platforms agree on.
 *
 * "." and ".." are skipped on both platforms. POSIX readdir yields them and
 * every current caller already filters them out by length, but skipping them
 * here means a caller cannot accidentally act on them.
 * ------------------------------------------------------------------------ */

typedef struct {
#if HAVEL_PLATFORM_WINDOWS
    HANDLE            handle;
    WIN32_FIND_DATAA  data;
    int               first;   /* FindFirstFile already produced `data` */
#else
    DIR*              dir;
#endif
} havel_dir_t;

#if HAVEL_PLATFORM_WINDOWS

/* Appends a trailing wildcard if the caller passed a bare directory path.
   FindFirstFile needs a pattern, and callers naturally pass the directory.
   Marked inline so a translation unit that includes this header but never walks
   a directory does not trip C4505 (unreferenced local function). */
HAVEL_CPLAT_INLINE int havel_dir_pattern(const char* path, char* out, size_t out_size) {
    size_t len = strlen(path);
    if (len == 0 || len + 3 > out_size) return 0;
    memcpy(out, path, len);
    if (path[len - 1] == '\\' || path[len - 1] == '/') {
        out[len]     = '*';
        out[len + 1] = '\0';
    } else {
        out[len]     = '\\';
        out[len + 1] = '*';
        out[len + 2] = '\0';
    }
    return 1;
}

HAVEL_CPLAT_INLINE int havel_dir_open(havel_dir_t* it, const char* path) {
    char pattern[HAVEL_CPLAT_PATH_MAX];
    if (!havel_dir_pattern(path, pattern, sizeof(pattern))) return 0;
    it->handle = FindFirstFileA(pattern, &it->data);
    if (it->handle == INVALID_HANDLE_VALUE) return 0;
    it->first = 1;
    return 1;
}

HAVEL_CPLAT_INLINE int havel_dir_next(havel_dir_t* it, const char** name_out) {
    for (;;) {
        if (it->first) {
            it->first = 0;
        } else if (!FindNextFileA(it->handle, &it->data)) {
            return 0;
        }
        const char* n = it->data.cFileName;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        *name_out = n;
        return 1;
    }
}

HAVEL_CPLAT_INLINE void havel_dir_close(havel_dir_t* it) {
    if (it->handle != INVALID_HANDLE_VALUE) {
        FindClose(it->handle);
        it->handle = INVALID_HANDLE_VALUE;
    }
}

#else /* POSIX */

HAVEL_CPLAT_INLINE int havel_dir_open(havel_dir_t* it, const char* path) {
    it->dir = opendir(path);
    return it->dir != NULL;
}

HAVEL_CPLAT_INLINE int havel_dir_next(havel_dir_t* it, const char** name_out) {
    struct dirent* e;
    while ((e = readdir(it->dir)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        *name_out = e->d_name;
        return 1;
    }
    return 0;
}

HAVEL_CPLAT_INLINE void havel_dir_close(havel_dir_t* it) {
    if (it->dir) {
        closedir(it->dir);
        it->dir = NULL;
    }
}

#endif

/* ---------------------------------------------------------------------------
 * Threads
 *
 * src/c only needs spawn-and-forget, spawn-and-join, and detach. Windows
 * threads are returned as a HANDLE that must be closed either way, whereas a
 * pthread_t is just a value - so join detaches implicitly and detach has
 * nothing to release. The macros below keep that asymmetry in one place
 * instead of at each call site.
 * ------------------------------------------------------------------------ */

#if HAVEL_PLATFORM_WINDOWS
typedef HANDLE havel_thread_t;
#define HAVEL_THREAD_NULL NULL
#else
typedef pthread_t havel_thread_t;
#define HAVEL_THREAD_NULL ((havel_thread_t)0)
#endif

#if HAVEL_PLATFORM_WINDOWS
/* CreateThread wants DWORD WINAPI(LPVOID); pthread_create wants
   void *(*)(void *). The two differ only in the return type, and every caller
   here stores the return in a void*, so the cast is the standard shim. */
HAVEL_CPLAT_INLINE int havel_thread_create(havel_thread_t *out,
                                           void *(*entry)(void *),
                                           void *arg) {
  HANDLE h = CreateThread(NULL, 0,
                            (LPTHREAD_START_ROUTINE)(void *(*)(void *))entry,
                            arg, 0, NULL);
  if (h == NULL) return 0;
  *out = h;
  return 1;
}

HAVEL_CPLAT_INLINE int havel_thread_join(havel_thread_t t, void **retval) {
  if (t == NULL) return 0;
  WaitForSingleObject(t, INFINITE);
  if (retval) {
    DWORD code = 0;
    GetExitCodeThread(t, &code);
    *retval = (void *)(uintptr_t)code;
  }
  CloseHandle(t);
  return 1;
}

/* Releasing the handle is all a detached thread needs: the OS reclaims the
   thread itself once it exits. */
HAVEL_CPLAT_INLINE void havel_thread_detach(havel_thread_t t) {
  if (t != NULL) CloseHandle(t);
}
#else
HAVEL_CPLAT_INLINE int havel_thread_create(havel_thread_t *out,
                                           void *(*entry)(void *),
                                           void *arg) {
  return pthread_create(out, NULL, entry, arg) == 0;
}

HAVEL_CPLAT_INLINE int havel_thread_join(havel_thread_t t, void **retval) {
  return pthread_join(t, retval) == 0;
}

/* A pthread_t carries no handle, so detaching is a no-op. */
HAVEL_CPLAT_INLINE void havel_thread_detach(havel_thread_t t) { (void)t; }
#endif

/* ---------------------------------------------------------------------------
 * One-time initialisation
 *
 * Replaces the __attribute__((constructor)) that src/c used to get a mutex
 * ready before main(). MSVC has no such attribute, and nothing runs before
 * main on Windows, so this is lazy instead.
 * ------------------------------------------------------------------------ */

#if HAVEL_PLATFORM_WINDOWS
typedef INIT_ONCE havel_once_t;
#define HAVEL_ONCE_INIT INIT_ONCE_STATIC_INIT
/* InitOnceExecuteOnce's callback takes three parameters; pthread_once's takes
   none. The callback body never uses them, so the cast is safe here and keeps
   one init function written once for both platforms. */
#define havel_once_call(once, fn)                                          \
  InitOnceExecuteOnce((once),                                            \
                        (BOOL(CALLBACK *)(PINIT_ONCE, PVOID, PVOID *))(fn), \
                        NULL, NULL)
#else
typedef pthread_once_t havel_once_t;
#define HAVEL_ONCE_INIT PTHREAD_ONCE_INIT
#define havel_once_call(once, fn) pthread_once((once), (fn))
#endif

/* ---------------------------------------------------------------------------
 * Single-path filesystem calls
 *
 * The POSIX forms take a mode argument that Windows has no equivalent for;
 * these wrappers drop it so call sites stay identical across platforms.
 * ------------------------------------------------------------------------ */

#if HAVEL_PLATFORM_WINDOWS
HAVEL_CPLAT_INLINE int havel_mkdir(const char* path) { return _mkdir(path); }
HAVEL_CPLAT_INLINE int havel_unlink(const char* path) { return _unlink(path); }
/* MSVC spells the case-insensitive compare _stricmp. It is a locale-sensitive
   comparison in the same way strcasecmp is, so config parsing that keys off
   "true"/"TRUE" keeps behaving identically. */
HAVEL_CPLAT_INLINE int havel_strcasecmp(const char* a, const char* b) { return _stricmp(a, b); }
#else
HAVEL_CPLAT_INLINE int havel_mkdir(const char* path) { return mkdir(path, 0755); }
HAVEL_CPLAT_INLINE int havel_unlink(const char* path) { return unlink(path); }
HAVEL_CPLAT_INLINE int havel_strcasecmp(const char* a, const char* b) { return strcasecmp(a, b); }
#endif

#endif /* HAVEL_CPLAT_H */