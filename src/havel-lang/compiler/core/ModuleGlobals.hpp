#pragma once

namespace havel::compiler {

// Canonical list of module globals that exist at runtime, registered via
// setGlobal() when modules load (stdlib + src/modules + VM host bridges).
//
// This is the single source of truth for strict-mode name resolution of the
// VM-less bytecode build path (runBuild) and for the pipeline's static
// fallback. Keeping it in one place means adding a module global updates one
// list, not several hand-maintained copies.
//
// The list is validated against the actual setGlobal() registrations by the
// "module globals drift guard" script (scripts/check_module_globals.sh,
// wired as a CTest test). That guard fails the build if a real setGlobal()
// global is registered but missing here, so this cannot silently drift out of
// sync the way a hand-maintained literal previously did.
//
// VM-core internal type/constructor objects (class, struct, prot, load,
// scheduler, None, Object, object, Option, newEnum, getVariant) and numeric
// constants (E, INF, NAN, PI, RAND_MAX) are intentionally NOT listed here:
// they are handled by the keyword/type lists in the callers.
inline constexpr const char *kModuleGlobals[] = {
    "alttab",       "altTab",       "app",           "array",
    "Array",        "audio",        "automation",    "bc",
    "bit",          "brightness",   "browser",       "bytecodeBuilder",
    "cfg",          "channel",      "choice",        "clipboard",
    "clipboardHistory",            "clipboardMgr",  "clipboardMonitor",
    "conf",         "config",       "debug",         "display",
    "extension",    "ffi",          "filemanager",   "fmt",
    "fs",           "hotkey",       "Hotkey",        "http",
    "image",        "interval",     "io",            "jit",
    "log",          "mapmanager",   "math",          "media",
    "mouse",        "pack",         "physics",       "Physics",
    "pixel",        "process",      "ptr",           "rand",
    "randint",      "random",       "readline",      "Regex",
    "screenshot",   "scroll",       "shell",         "state",
    "string",       "String",       "sys",           "system",
    "textChunker",  "thread",       "time",          "timeout",
    "timer",        "Type",         "ui",            "vec",
    "wayland",
};

} // namespace havel::compiler
