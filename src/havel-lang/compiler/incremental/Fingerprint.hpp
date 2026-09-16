#pragma once

// ===== Incremental Compilation: Source Fingerprinting =====
//
// Computes stable content-based fingerprints for Havel source files and
// compiler artifacts. Used as cache keys for incremental compilation.
//
// Cache key components (per TODO.md #31):
//   - source hash (content + encoding)
//   - dependency hashes (imported modules)
//   - compiler version
//   - IR version
//   - optimization level
//   - target architecture
//   - relevant configuration
//
// Fingerprints are deterministic SHA-256 hashes encoded as hex strings.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace havel::compiler::incremental {

struct Fingerprint {
    std::string value;          // hex-encoded SHA-256
    uint64_t timestamp_ms = 0;  // mtime for quick change detection
    uint64_t size = 0;          // file size for quick change detection

    bool operator==(const Fingerprint& other) const { return value == other.value; }
    bool operator!=(const Fingerprint& other) const { return !(*this == other); }
};

/// Compute fingerprint of a file's content (SHA-256).
/// Returns nullopt if file doesn't exist or can't be read.
std::optional<Fingerprint> fingerprintFile(const std::filesystem::path& path);

/// Compute fingerprint from raw bytes.
Fingerprint fingerprintBytes(const uint8_t* data, size_t len);

/// Compute fingerprint from string.
Fingerprint fingerprintString(const std::string& str);

/// Compute combined fingerprint from multiple fingerprints (order-independent).
/// Uses sorted concatenation of hex values + a domain separator.
Fingerprint combineFingerprints(const std::vector<Fingerprint>& fps);

/// Compute fingerprint of compiler configuration that affects codegen.
/// Includes: compiler version, IR version, opt level, target arch, feature flags.
Fingerprint fingerprintConfig(const std::string& compiler_version,
                              const std::string& ir_version,
                              uint8_t opt_level,
                              const std::string& target_arch,
                              const std::vector<std::string>& features);

/// Import resolver callback type.
using ImportResolver = std::function<std::optional<std::filesystem::path>(
    const std::string& import_name, const std::filesystem::path& from_dir)>;

/// Fingerprint a Havel module (source + dependencies).
/// Recursively fingerprints all transitive imports.
struct ModuleFingerprint {
    Fingerprint source;                    // source file content
    Fingerprint deps;                      // combined fingerprint of all imports
    Fingerprint config;                    // compiler config affecting this module
    Fingerprint combined;                  // final cache key = hash(source | deps | config)
    std::vector<std::filesystem::path> imports;  // resolved import paths
};

std::optional<ModuleFingerprint> fingerprintModule(
    const std::filesystem::path& source_path,
    const ImportResolver& resolver,
    const Fingerprint& config_fp);

/// Quick change detection without full re-fingerprint.
/// Returns true if file's mtime/size matches the fingerprint.
bool isFingerprintCurrent(const Fingerprint& fp, const std::filesystem::path& path);

}  // namespace havel::compiler::incremental