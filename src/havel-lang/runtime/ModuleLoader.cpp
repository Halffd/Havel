#include "ModuleLoader.hpp"
#include "c/ModulePlugin.h"
#include "dl/Loader.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <system_error>
#include <array>
#include <fstream>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

// SHA-256 implementation (local copy for ModuleLoader to avoid circular deps)
namespace {
const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ((w[i-15] >> 7) | (w[i-15] << 25)) ^ ((w[i-15] >> 18) | (w[i-15] << 14)) ^ (w[i-15] >> 3);
        uint32_t s1 = ((w[i-2] >> 17) | (w[i-2] << 15)) ^ ((w[i-2] >> 19) | (w[i-2] << 13)) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = ((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7));
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + SHA256_K[i] + w[i];
        uint32_t s0 = ((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10));
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    size_t pos = 0;
    while (len >= 64) {
        sha256_transform(state, data + pos);
        pos += 64;
        len -= 64;
    }
    uint8_t block[64] = {0};
    memcpy(block, data + pos, len);
    block[len] = 0x80;
    size_t bit_len = (pos + len) * 8;
    if (len >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }
    block[63] = bit_len & 0xFF;
    block[62] = (bit_len >> 8) & 0xFF;
    block[61] = (bit_len >> 16) & 0xFF;
    block[60] = (bit_len >> 24) & 0xFF;
    block[59] = (bit_len >> 32) & 0xFF;
    block[58] = (bit_len >> 40) & 0xFF;
    block[57] = (bit_len >> 48) & 0xFF;
    block[56] = (bit_len >> 56) & 0xFF;
    sha256_transform(state, block);
    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; ++i) {
        out[i*4] = state[i] >> 24;
        out[i*4+1] = state[i] >> 16;
        out[i*4+2] = state[i] >> 8;
        out[i*4+3] = state[i];
    }
    return out;
}

std::array<uint8_t, 32> sha256_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    return sha256(buf.data(), buf.size());
}

std::string sha256_file_hex(const std::string& path) {
    auto hash = sha256_file(path);
    std::ostringstream oss;
    for (uint8_t b : hash) oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}

std::string sha256_hex(const std::string& data) {
    auto hash = sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    std::ostringstream oss;
    for (uint8_t b : hash) oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return oss.str();
}
} // anonymous namespace

namespace havel {

std::string ModuleLoader::cacheFileNameForSource(const std::string& canonicalSourcePath) {
    namespace fs = std::filesystem;

    // Namespace prefix for bundled modules: lang.<stem> / std.<stem>
    std::string normalized = canonicalSourcePath;
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    if (normalized.find("/modules/lang/") != std::string::npos) {
        std::string stem = fs::path(normalized).stem().string();
        return "lang." + stem;
    }
    if (normalized.find("/modules/std/") != std::string::npos) {
        std::string stem = fs::path(normalized).stem().string();
        return "std." + stem;
    }

    // User module: stem + short hash of the canonical path so two files with
    // the same stem in different directories cannot clobber each other.
    std::string stem = fs::path(normalized).stem().string();
    std::string hash = sha256_hex(normalized);
    return stem + "." + hash.substr(0, 8);
}

ModuleLoader::~ModuleLoader() {
    unloadNativeExtensions();
}

// Build a BytecodeCache ResolvedModule. If the .hv source next to .hvc exists
// (or was located), record its canonical path so the VM can re-verify the
// embedded source hash against the live file content.
static ModuleLoader::ResolvedModule makeBcCache(const std::filesystem::path &hvcPath,
                                                const std::filesystem::path &hvPath,
                                                const std::string &modulePath) {
    namespace fs = std::filesystem;
    std::string bcPath;
    try { bcPath = fs::canonical(hvcPath).string(); }
    catch (...) { bcPath = hvcPath.string(); }
    std::string srcPath;
    if (fs::exists(hvPath)) {
        try { srcPath = fs::canonical(hvPath).string(); }
        catch (...) { srcPath = hvPath.string(); }
    }
    return ModuleLoader::ResolvedModule{ModuleLoader::ResolvedModule::BytecodeCache,
                                        bcPath, modulePath, srcPath};
}

void ModuleLoader::addSearchPath(const std::string& path) {
    searchPaths_.push_back(path);
}

void ModuleLoader::addModuleSoPath(const std::string& path) {
  moduleSoPaths_.push_back(path);
}

void ModuleLoader::setStdlibPath(const std::string& path) {
    stdlibPath_ = path;
}

std::optional<ModuleLoader::ResolvedModule>
ModuleLoader::resolve(const std::string& modulePath,
                      const std::string& scriptDir) const {
  namespace fs = std::filesystem;

  std::string name = modulePath;

  // 1b. Check cache directory first (compiled .hvc bytecode)
  // Load hash index for persistent cache validation
  loadHashIndex();
  std::string cacheDir = getCacheDir();

  auto checkBcCache = [&](const fs::path& hvcPath, const fs::path& hvPath,
                          const std::string& hashKey) -> std::optional<ResolvedModule> {
    if (!fs::exists(hvcPath)) return std::nullopt;

    // Check persistent hash index first
    loadHashIndex();
    auto hashIt = bytecode_hash_index_.find(hashKey);
    if (hashIt != bytecode_hash_index_.end() && fs::exists(hvPath)) {
      // Compute current source hash
      std::string currentHash = sha256_file_hex(hvPath.string());
      if (currentHash == hashIt->second) {
        // Hash matches - cache is valid
        return makeBcCache(hvcPath, hvPath, modulePath);
      }
      // Hash mismatch - cache is stale, fall through to mtime check
    }

    // Fallback to mtime check
    auto hvcTime = fs::last_write_time(hvcPath);
    bool newerOrEqual = !fs::exists(hvPath) ||
                        hvcTime >= fs::last_write_time(hvPath);
    if (newerOrEqual) return makeBcCache(hvcPath, hvPath, modulePath);
    return std::nullopt;
  };

  // Check the flat cache for a resolved source file. The cache filename is
  // derived from the canonical source path (stem + path hash for user
  // modules), so same-stem files in different directories cannot collide.
  auto flatCacheFor = [&](const std::string& canonicalSrcPath,
                          ResolvedModule::Type srcType)
      -> std::optional<ResolvedModule> {
    std::string cacheName = cacheFileNameForSource(canonicalSrcPath);
    auto bc = checkBcCache(fs::path(cacheDir) / (cacheName + ".hvc"),
                           fs::path(cacheDir) / (cacheName + ".hv"),
                           cacheName);
    if (bc) return bc;
    return ResolvedModule{srcType, canonicalSrcPath, modulePath, ""};
  };

  // Check if path is absolute
  if (fs::path(modulePath).is_absolute()) {
    if (fs::exists(modulePath)) {
      try {
        return flatCacheFor(fs::canonical(modulePath).string(),
                            ResolvedModule::UserSource);
      } catch (...) {
        return ResolvedModule{ResolvedModule::UserSource, modulePath, modulePath};
      }
    }
    // Also try with .hvc extension for absolute paths
    fs::path hvcPath = fs::path(modulePath).replace_extension(".hvc");
    if (fs::exists(hvcPath)) {
      try {
        return ResolvedModule{ResolvedModule::BytecodeCache,
                              fs::canonical(hvcPath).string(), modulePath};
      } catch (...) {
        return ResolvedModule{ResolvedModule::BytecodeCache, hvcPath.string(), modulePath};
      }
    }
    return std::nullopt;
  }

  // Handle explicit relative paths starting with ./ or ../
  if (modulePath.starts_with("./") || modulePath.starts_with("../")) {
    fs::path resolved = fs::path(scriptDir) / modulePath;
    if (fs::exists(resolved)) {
      return flatCacheFor(fs::canonical(resolved).string(),
                          ResolvedModule::UserSource);
    }
    // Also try .hvc variant
    fs::path hvcPath = fs::path(scriptDir) / (modulePath + ".hvc");
    if (modulePath.ends_with(".hv")) {
      hvcPath = fs::path(scriptDir) / (modulePath.substr(0, modulePath.size() - 3) + ".hvc");
    }
    if (fs::exists(hvcPath)) {
      return ResolvedModule{ResolvedModule::BytecodeCache,
                            fs::canonical(hvcPath).string(), modulePath};
    }
    return std::nullopt;
  }

// For bare module names, try priority search

  // 1. Check cache (already loaded?)
  // If already in cache, return Cached type — BUT first drop the entry if the
  // underlying source/.hvc filename has been touched since we cached it.
  if (cache_.count(modulePath) > 0) {
    if (!isFreshLocked(modulePath)) {
      cache_.erase(modulePath);
      freshness_.erase(modulePath);
    } else {
      return ResolvedModule{ResolvedModule::Cached, "", modulePath};
    }
  }

  // 1. lang.<name>.hvc (lang modules take priority)
  if (auto bc = checkBcCache(
        fs::path(cacheDir) / ("lang." + name + ".hvc"),
        fs::path(cacheDir) / ("lang." + name + ".hv"),
        "lang." + name)) {
    return *bc;
  }

  // 2. std.<name>.hvc (stdlib modules)
  if (auto bc = checkBcCache(
        fs::path(cacheDir) / ("std." + name + ".hvc"),
        fs::path(cacheDir) / ("std." + name + ".hv"),
        "std." + name)) {
    return *bc;
  }

  // 3. <name>.hvc (user modules, no namespace)
  if (auto bc = checkBcCache(
        fs::path(cacheDir) / (name + ".hvc"),
        fs::path(cacheDir) / (name + ".hv"),
        name)) {
    return *bc;
  }

  // 2. Check script directory first for local modules:
  // scriptDir/name.hvc (prefer pre-compiled), then name.hv
  if (!scriptDir.empty()) {
    fs::path scriptDirPath(scriptDir);

    // Prefer .hvc if it exists and is newer than .hv (or .hv absent)
    auto pickHvOrHvc = [&](const fs::path& basePath) -> std::optional<ResolvedModule> {
      fs::path hvPath = fs::path(basePath) / (name + ".hv");
      fs::path hvcPath = fs::path(basePath) / (name + ".hvc");
      bool hvExists = fs::exists(hvPath);
      bool hvcExists = fs::exists(hvcPath);
      if (hvcExists && hvExists) {
        if (fs::last_write_time(hvcPath) >= fs::last_write_time(hvPath)) {
          return makeBcCache(hvcPath, hvPath, modulePath);
        }
        return flatCacheFor(fs::canonical(hvPath).string(),
                            ResolvedModule::UserSource);
      }
      if (hvcExists) {
        return makeBcCache(hvcPath, hvPath, modulePath);
      }
      if (hvExists) {
        return flatCacheFor(fs::canonical(hvPath).string(),
                            ResolvedModule::UserSource);
      }
      return std::nullopt;
    };

    auto local = pickHvOrHvc(scriptDirPath);
    if (local) return local;

    // Package-style: scriptDir/name/name.hv or name.hvc
    fs::path pkgDir = scriptDirPath / name;
    auto pkg = pickHvOrHvc(pkgDir);
    if (pkg) return pkg;
  }

  // 3. Check stdlibPath_ for name.hvc or name.hv
  // But first, check if there's a plugin (native extension) for this module
  // in the search paths, to allow plugins to override stdlib .hv/.hvc files.
  for (const auto& sp : searchPaths_) {
    fs::path spDir(sp);
    fs::path soPath = spDir / (name + ".so");
    if (fs::exists(soPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
                            fs::canonical(soPath).string(), modulePath, ""};
    }
    fs::path libPath = spDir / ("libhavel_" + name + ".so");
    if (fs::exists(libPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
                            fs::canonical(libPath).string(), modulePath, ""};
    }
    // Also check plugin naming convention (havel_mod_<name>.so)
    fs::path pluginPath = fs::path(sp) / ("havel_mod_" + name + ".so");
    if (fs::exists(pluginPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
                            fs::canonical(pluginPath).string(), modulePath, ""};
    }
  }

  // 4. Check stdlibPath_ for name.hvc or name.hv
  if (!stdlibPath_.empty()) {
    fs::path stdlibHvcPath = fs::path(stdlibPath_) / (name + ".hvc");
    fs::path stdlibHvPath = fs::path(stdlibPath_) / (name + ".hv");
    bool hvcExists = fs::exists(stdlibHvcPath);
    bool hvExists = fs::exists(stdlibHvPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(stdlibHvcPath) >= fs::last_write_time(stdlibHvPath)) {
        return makeBcCache(stdlibHvcPath, stdlibHvPath, modulePath);
      }
      return ResolvedModule{ResolvedModule::StdlibSource,
                            fs::canonical(stdlibHvPath).string(), modulePath, ""};
    }
    if (hvcExists) {
      // Check if there's a plugin for this module before using the cache
      for (const auto& sp : searchPaths_) {
        fs::path spDir(sp);
        fs::path soPath = spDir / (name + ".so");
        if (fs::exists(soPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(soPath).string(), modulePath, ""};
        }
        fs::path libPath = spDir / ("libhavel_" + name + ".so");
        if (fs::exists(libPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(libPath).string(), modulePath, ""};
        }
        fs::path pluginPath = fs::path(sp) / ("havel_mod_" + name + ".so");
        if (fs::exists(pluginPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(pluginPath).string(), modulePath, ""};
        }
      }
      // No source side-by-side - use cache but no sourcePath for hash check.
      return makeBcCache(stdlibHvcPath, stdlibHvPath, modulePath);
    }
    if (hvExists) {
      // Check if there's a plugin for this module before using the .hv file
      if (!stdlibPath_.empty()) {
        fs::path stdlibPluginPath = fs::path(stdlibPath_) / (name + havel_loader_suffix());
        if (fs::exists(stdlibPluginPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(stdlibPluginPath).string(), modulePath, ""};
        }
      }
      // Also check search paths for plugins
      for (const auto& sp : searchPaths_) {
        fs::path spDir(sp);
        fs::path soPath = spDir / (name + ".so");
        if (fs::exists(soPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(soPath).string(), modulePath, ""};
        }
        fs::path libPath = spDir / ("libhavel_" + name + ".so");
        if (fs::exists(libPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(libPath).string(), modulePath, ""};
        }
        fs::path pluginPath = fs::path(sp) / ("havel_mod_" + name + ".so");
        if (fs::exists(pluginPath)) {
          return ResolvedModule{ResolvedModule::NativeExtension,
                                fs::canonical(pluginPath).string(), modulePath, ""};
        }
      }
      return flatCacheFor(fs::canonical(stdlibHvPath).string(),
                          ResolvedModule::StdlibSource);
    }
  }

  // 5. Check ~/.havel/packages/name/name.hvc or name.hv
  if (const char* home = std::getenv("HOME")) {
    fs::path pkgDir = fs::path(home) / ".havel" / "packages" / name;
    fs::path pkgHvcPath = pkgDir / (name + ".hvc");
    fs::path pkgHvPath = pkgDir / (name + ".hv");
    bool hvcExists = fs::exists(pkgHvcPath);
    bool hvExists = fs::exists(pkgHvPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(pkgHvcPath) >= fs::last_write_time(pkgHvPath)) {
        return makeBcCache(pkgHvcPath, pkgHvPath, modulePath);
      }
      return flatCacheFor(fs::canonical(pkgHvPath).string(),
                          ResolvedModule::PackageSource);
    }
    if (hvcExists) {
      return makeBcCache(pkgHvcPath, pkgHvPath, modulePath);
    }
    if (hvExists) {
      return flatCacheFor(fs::canonical(pkgHvPath).string(),
                          ResolvedModule::PackageSource);
    }
  }

// 6. Check each user search path for name.hvc, name.hv, or name/name.hv
  for (const auto& sp : searchPaths_) {
      fs::path spDir(sp);

        // Prefer .hvc if available and newer
        fs::path hvPath = spDir / (name + ".hv");
        fs::path hvcPath = spDir / (name + ".hvc");
        bool hvcExists = fs::exists(hvcPath);
        bool hvExists = fs::exists(hvPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(hvcPath) >= fs::last_write_time(hvPath)) {
        return makeBcCache(hvcPath, hvPath, modulePath);
      }
      return flatCacheFor(fs::canonical(hvPath).string(),
                          ResolvedModule::UserSource);
    }
    if (hvcExists) {
      return makeBcCache(hvcPath, hvPath, modulePath);
    }
    if (hvExists) {
      return flatCacheFor(fs::canonical(hvPath).string(),
                          ResolvedModule::UserSource);
    }

    // Try name/name.hv or name/name.hvc (package style)
    fs::path pkgDir = spDir / name;
    fs::path hvPkgPath = pkgDir / (name + ".hv");
    fs::path hvcPkgPath = pkgDir / (name + ".hvc");
    hvcExists = fs::exists(hvcPkgPath);
    hvExists = fs::exists(hvPkgPath);
    if (hvcExists && hvExists) {
      if (fs::last_write_time(hvcPkgPath) >= fs::last_write_time(hvPkgPath)) {
        return makeBcCache(hvcPkgPath, hvPkgPath, modulePath);
      }
      return flatCacheFor(fs::canonical(hvPkgPath).string(),
                          ResolvedModule::UserSource);
    }
    if (hvcExists) {
      return makeBcCache(hvcPkgPath, hvPkgPath, modulePath);
    }
    if (hvExists) {
      return flatCacheFor(fs::canonical(hvPkgPath).string(),
                          ResolvedModule::UserSource);
    }
  }

  // 7. Check each search path for native extensions (.so)
  for (const auto& sp : searchPaths_) {
    fs::path spDir(sp);

    // Try name.so
    fs::path soPath = spDir / (name + ".so");
    if (fs::exists(soPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
        fs::canonical(soPath).string(), modulePath};
    }

    // Try libhavel_name.so
    fs::path libPath = spDir / ("libhavel_" + name + ".so");
    if (fs::exists(libPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
        fs::canonical(libPath).string(), modulePath};
    }
  }

  // 7b. Check module .so paths for havel_mod_<name>.so (plugin naming convention)
  for (const auto& sp : moduleSoPaths_) {
    fs::path soPath = fs::path(sp) / ("havel_mod_" + name + ".so");
    if (fs::exists(soPath)) {
      return ResolvedModule{ResolvedModule::NativeExtension,
        fs::canonical(soPath).string(), modulePath, ""};
    }
  }

    // 8. Check for host builtin module
    if (hostFns_.count(name) > 0 || envModules_.count(name) > 0) {
        return ResolvedModule{ResolvedModule::HostBuiltin, "", modulePath, ""};
    }

    return std::nullopt;
}

bool ModuleLoader::isCached(const std::string& key) const {
    return cache_.count(key) > 0;
}

bool ModuleLoader::getCached(const std::string& key, core::Value* outValue) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    if (outValue) {
        *outValue = it->second.exports;
    }
    return true;
}

bool ModuleLoader::getCachedGlobals(const std::string& key, std::shared_ptr<std::unordered_map<std::string, core::Value>>* outGlobals) const {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    if (outGlobals) {
        *outGlobals = it->second.globals_snapshot;
    }
    return true;
}

void ModuleLoader::putCache(const std::string& key, core::Value value) {
    cache_[key] = CachedModule{value, nullptr};
}

void ModuleLoader::putCache(const std::string& key, core::Value value,
                            const std::string &sourcePath, const std::string &bytecodePath) {
    cache_[key] = CachedModule{value, nullptr};
    freshness_[key] = CacheFreshness{sourcePath, bytecodePath,
                                     std::max(mtimeNs(sourcePath), mtimeNs(bytecodePath))};
}

void ModuleLoader::putCacheWithGlobals(const std::string& key, core::Value value, std::shared_ptr<std::unordered_map<std::string, core::Value>> globals) {
    cache_[key] = CachedModule{value, globals};
}

void ModuleLoader::putCacheWithGlobals(const std::string& key, core::Value value, std::shared_ptr<std::unordered_map<std::string, core::Value>> globals,
                                       const std::string &sourcePath, const std::string &bytecodePath) {
    cache_[key] = CachedModule{value, globals};
    freshness_[key] = CacheFreshness{sourcePath, bytecodePath,
                                     std::max(mtimeNs(sourcePath), mtimeNs(bytecodePath))};
}

void ModuleLoader::clearCache() {
    cache_.clear();
    freshness_.clear();
}

void ModuleLoader::invalidate(const std::string& key) {
    cache_.erase(key);
    freshness_.erase(key);
}

long long ModuleLoader::mtimeNs(const std::string &path) {
    if (path.empty()) return 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();
}

bool ModuleLoader::isFreshLocked(const std::string &key) const {
    auto it = freshness_.find(key);
    if (it == freshness_.end()) return true; // no hint, treat as fresh
    const auto &h = it->second;
    // Compare the most-recently-touched side. If both source and .hvc were
    // recorded, just check the newer of the two (which is what we'd inspect
    // during resolve); in practice we check both and bail if either changed.
    if (!h.src.empty()) {
        long long m = mtimeNs(h.src);
        if (m != h.mtime_ns && !h.hvc.empty()) return false;
        // For source-only entries (no .hvc), expect mtime to match.
        if (h.hvc.empty() && m != h.mtime_ns) return false;
    }
    if (!h.hvc.empty()) {
        long long m = mtimeNs(h.hvc);
        if (m != h.mtime_ns) return false;
    }
    return true;
  }

  void ModuleLoader::loadHashIndex() const {
    if (hash_index_loaded_) return;
    hash_index_loaded_ = true;

    std::string cacheDir = getCacheDir();
    bytecode_hash_index_path_ = (std::filesystem::path(cacheDir) / ".havel_bytecode_hash_index").string();

    std::ifstream file(bytecode_hash_index_path_);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#') continue;
      size_t pos = line.find(' ');
      if (pos != std::string::npos) {
        std::string module = line.substr(0, pos);
        std::string hash = line.substr(pos + 1);
        bytecode_hash_index_[module] = hash;
      }
    }
  }

  void ModuleLoader::saveHashIndex() const {
    if (bytecode_hash_index_path_.empty()) return;

    std::ofstream file(bytecode_hash_index_path_);
    if (!file.is_open()) return;

    for (const auto& [module, hash] : bytecode_hash_index_) {
      file << module << ' ' << hash << '\n';
    }
  }

  void ModuleLoader::updateHashIndex(const std::string& moduleName, const std::string& hash) {
    loadHashIndex();
    bytecode_hash_index_[moduleName] = hash;
    saveHashIndex();
  }

  std::string ModuleLoader::sha256FileHex(const std::string& path) {
    return sha256_file_hex(path);
  }

  std::string ModuleLoader::canonicalizePath(const std::string& modulePath,
                                             const std::string& scriptDir) const {
    auto resolved = resolve(modulePath, scriptDir);
    if (resolved && !resolved->canonicalPath.empty()) {
      return resolved->canonicalPath;
    }
    return "";
  }

  std::string ModuleLoader::getCacheDir() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return (std::filesystem::path(home) / ".cache" / "havel").string();
  }

  std::vector<core::Value> ModuleLoader::cachedValues() const {
    std::vector<core::Value> values;
    values.reserve(cache_.size());
    for (const auto& [key, val] : cache_) {
        values.push_back(val.exports);
    }
    return values;
}

std::optional<ModuleLoader::NativeHandle>
ModuleLoader::loadNativeExtension(const std::string& path) {
  auto it = nativeHandles_.find(path);
  if (it != nativeHandles_.end()) {
    return it->second;
  }

#ifndef _WIN32
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    std::cerr << "dlopen failed: " << dlerror() << std::endl;
    return std::nullopt;
  }

  using InfoFn = const HavelModuleABI *(*)(void);
  InfoFn info_fn = reinterpret_cast<InfoFn>(dlsym(handle, "havel_module_info"));
  if (info_fn) {
    const HavelModuleABI *abi = info_fn();
    if (abi && abi->abi_version >= 1 &&
        abi->abi_version <= HAVEL_MODULE_ABI_VERSION && abi->register_fn) {
      // register_fn must be called with VMApi* from the VM
      // For now just store the handle; the VM will call register_fn
    }
  }
#else
  HMODULE handle = LoadLibraryA(path.c_str());
  if (!handle) {
    std::cerr << "LoadLibrary failed: " << GetLastError() << std::endl;
    return std::nullopt;
  }
#endif

  NativeHandle nh;
  nh.dlHandle = static_cast<void*>(handle);
  nh.name = path;
  nativeHandles_[path] = nh;

      return nh;
}

void ModuleLoader::unloadNativeExtensions() {
    for (auto& [name, handle] : nativeHandles_) {
        if (handle.dlHandle) {
#ifndef _WIN32
            dlclose(handle.dlHandle);
#else
            FreeLibrary(static_cast<HMODULE>(handle.dlHandle));
#endif
}
  }

  nativeHandles_.clear();
}

// ============================================================================
// Backward compatibility: Environment-based host module registry
// ============================================================================

void ModuleLoader::add(const std::string& name, ModuleFn fn) {
    envModules_[name] = fn;
    hostModuleFlags_[name] = false;
    interpreterModuleFlags_[name] = false;
}

void ModuleLoader::addInterpreter(const std::string& name, InterpreterModuleFn fn) {
    interpreterFns_[name] = fn;
    hostModuleFlags_[name] = false;
    interpreterModuleFlags_[name] = true;
}

void ModuleLoader::addHost(const std::string& name, HostModuleFn fn) {
    envModules_[name] = [](Environment &env) {
        // Placeholder - will fail at load time if hostAPI not provided
    };
    hostFns_[name] = fn;
    hostModuleFlags_[name] = true;
    interpreterModuleFlags_[name] = false;
}

bool ModuleLoader::load(Environment& env, const std::string& name,
                     std::shared_ptr<IHostAPI> hostAPI,
                     Interpreter* interpreter) {
    // Check if interpreter module
    if (interpreterModuleFlags_[name]) {
        auto it = interpreterFns_.find(name);
        if (it != interpreterFns_.end()) {
            it->second(env, interpreter);
            envLoaded_.insert(name);
            return true;
        }
        return false;
    }

    // Check if host module
    if (hostModuleFlags_[name]) {
        if (!hostAPI) {
            throw std::runtime_error("Host module '" + name + "' requires host API");
        }
        auto hostIt = hostFns_.find(name);
        if (hostIt != hostFns_.end()) {
            hostIt->second(env, hostAPI);
            envLoaded_.insert(name);
            return true;
        }
        return false;
    }

    // Standard module
    auto it = envModules_.find(name);
    if (it == envModules_.end()) {
        return false;
    }

    it->second(env);
    envLoaded_.insert(name);
    return true;
}

bool ModuleLoader::has(const std::string& name) const {
    return envModules_.count(name) > 0 || hostFns_.count(name) > 0 ||
           interpreterFns_.count(name) > 0;
}

bool ModuleLoader::isLoaded(const std::string& name) const {
    return envLoaded_.count(name) > 0;
}

std::vector<std::string> ModuleLoader::list() const {
    std::vector<std::string> names;
    for (const auto& [name, fn] : envModules_) {
        names.push_back(name);
    }
    return names;
}

void ModuleLoader::clearLoaded() {
    envLoaded_.clear();
}

} // namespace havel
