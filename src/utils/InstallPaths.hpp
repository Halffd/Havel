#pragma once

// System-install aware path discovery.
//
// Two layouts are supported:
//   Source tree / build dir: <exe_dir>/../modules        (repo root modules/)
//   System install:          <exe_dir>/../share/havel/modules
//                            e.g. /usr/bin/havel -> /usr/share/havel/modules
//
// The first existing candidate wins; dev layouts keep priority so existing
// workflows are unaffected.

#include <filesystem>
#include <string>

#include "core/util/Env.hpp"

namespace havel::install_paths {

inline std::filesystem::path modulesRoot() {
    namespace fs = std::filesystem;
    const auto exePath = Env::executable();
    if (exePath.empty()) return {};
    const fs::path exeDir = fs::path(exePath).parent_path();
    const fs::path candidates[] = {
        exeDir / ".." / "modules",                       // source tree / build dir
        exeDir / ".." / "share" / "havel" / "modules",   // system install
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            auto canonical = fs::weakly_canonical(candidate, ec);
            return ec ? candidate : canonical;
        }
    }
    return {};
}

inline std::string stdlibRoot() {
    auto root = modulesRoot();
    if (root.empty()) return {};
    return root.string(); // Return modules root; stdlib modules are in subdirectories (std/, type/, etc.)
}

inline std::filesystem::path scriptsRoot() {
    auto root = modulesRoot();
    if (root.empty()) return {};
    return root.parent_path() / "scripts";
}

// Returns the system install bytecode directory if it exists (e.g. /usr/share/havel/modules)
inline std::filesystem::path bytecodeRoot() {
    namespace fs = std::filesystem;
    const auto exePath = Env::executable();
    if (exePath.empty()) return {};
    const fs::path exeDir = fs::path(exePath).parent_path();
    const fs::path candidate = exeDir / ".." / "share" / "havel" / "modules";
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
        auto canonical = fs::weakly_canonical(candidate, ec);
        return ec ? candidate : canonical;
    }
    return {};
}

// Returns the system install module plugin directory if it exists (e.g. /usr/lib/havel/modules)
inline std::filesystem::path modulePluginRoot() {
    namespace fs = std::filesystem;
    const auto exePath = Env::executable();
    if (exePath.empty()) return {};
    const fs::path exeDir = fs::path(exePath).parent_path();
    const fs::path candidates[] = {
        exeDir / ".." / "lib" / "havel" / "modules",           // /usr/bin/havel -> /usr/lib/havel/modules
        fs::path("/usr/lib/havel/modules"),                    // fallback for /usr/local/bin/havel
        fs::path("/usr/local/lib/havel/modules"),              // fallback for /usr/local/bin/havel
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            auto canonical = fs::weakly_canonical(candidate, ec);
            return ec ? candidate : canonical;
        }
    }
    return {};
}

} // namespace havel::install_paths
