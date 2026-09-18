#pragma once

// ===== Incremental Compilation: Cache Layers =====
//
// Three-layer cache for incremental compilation artifacts:
//
// 1. Memory cache (fast, per-process)
//    - Hot functions/modules
//    - LRU eviction
//
// 2. Disk cache (persistent, cross-process)
//    - .hvb  → compiled bytecode / IR
//    - .hvd  → dependency metadata (lock file)
//    - .hvs  → optional optimization/profile summaries
//
// 3. Recompile (fallback)
//    - When cache misses or invalidation fails
//
// Cache keys include (per TODO.md #31):
//   - source hash
//   - dependency hashes
//   - compiler version
//   - IR version
//   - optimization level
//   - target architecture
//   - relevant configuration
//
// Never reuse incompatible cached native code.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::compiler::incremental {

/// Cache entry metadata.
struct CacheEntryMeta {
    std::string key;              // full cache key (source+deps+config hash)
    std::string fingerprint;      // input fingerprint
    uint64_t timestamp_ms = 0;    // creation time
    uint64_t size_bytes = 0;      // artifact size
    uint8_t opt_level = 0;        // optimization level
    std::string target_arch;      // target architecture
    std::string ir_version;       // IR version
    std::string compiler_version; // compiler version
};

/// Abstract cache layer interface.
class CacheLayer {
public:
    virtual ~CacheLayer() = default;

    /// Try to get an artifact by key.
    /// Returns empty if not found or corrupted.
    virtual std::optional<std::vector<uint8_t>> get(const std::string& key) = 0;

    /// Store an artifact with metadata.
    virtual bool put(const std::string& key, const std::vector<uint8_t>& data,
                     const CacheEntryMeta& meta) = 0;

    /// Check if key exists.
    virtual bool has(const std::string& key) = 0;

    /// Remove an entry.
    virtual bool remove(const std::string& key) = 0;

    /// Clear all entries.
    virtual void clear() = 0;

    /// Get cache stats.
    virtual size_t size() const = 0;
    virtual size_t memoryUsage() const = 0;
};

/// In-memory LRU cache.
class MemoryCache : public CacheLayer {
public:
    explicit MemoryCache(size_t max_entries = 1000, size_t max_bytes = 64 * 1024 * 1024);

    std::optional<std::vector<uint8_t>> get(const std::string& key) override;
    bool put(const std::string& key, const std::vector<uint8_t>& data,
             const CacheEntryMeta& meta) override;
    bool has(const std::string& key) override;
    bool remove(const std::string& key) override;
    void clear() override;
    size_t size() const override;
    size_t memoryUsage() const override;

private:
    struct Entry {
        std::vector<uint8_t> data;
        CacheEntryMeta meta;
        // LRU tracking
        mutable uint64_t last_access = 0;
    };

    std::unordered_map<std::string, Entry> map_;
    size_t max_entries_;
    size_t max_bytes_;
    size_t current_bytes_ = 0;
    mutable uint64_t access_counter_ = 0;

    void evictIfNeeded();
};

/// Disk cache in a directory.
/// Stores artifacts as files with .hvb/.hvd/.hvs extensions.
class DiskCache : public CacheLayer {
public:
    explicit DiskCache(const std::filesystem::path& root_dir);

    std::optional<std::vector<uint8_t>> get(const std::string& key) override;
    bool put(const std::string& key, const std::vector<uint8_t>& data,
             const CacheEntryMeta& meta) override;
    bool has(const std::string& key) override;
    bool remove(const std::string& key) override;
    void clear() override;
    size_t size() const override;
    size_t memoryUsage() const override { return 0; }

    /// Scan and validate cache entries (for startup/invalidation).
    struct ValidationResult {
        size_t valid = 0;
        size_t corrupted = 0;
        size_t missing_meta = 0;
        size_t removed = 0;
    };
    ValidationResult validate(const std::function<bool(const CacheEntryMeta&)>& predicate);

private:
    std::filesystem::path root_;
    std::string keyToPath(const std::string& key) const;
    std::string keyToMetaPath(const std::string& key) const;
};

/// Multi-layer cache: memory -> disk -> recompile.
class TieredCache : public CacheLayer {
public:
    TieredCache(std::unique_ptr<CacheLayer> l1, std::unique_ptr<CacheLayer> l2);

    std::optional<std::vector<uint8_t>> get(const std::string& key) override;
    bool put(const std::string& key, const std::vector<uint8_t>& data,
             const CacheEntryMeta& meta) override;
    bool has(const std::string& key) override;
    bool remove(const std::string& key) override;
    void clear() override;
    size_t size() const override;
    size_t memoryUsage() const override;

    CacheLayer* l1() const { return l1_.get(); }
    CacheLayer* l2() const { return l2_.get(); }

private:
    std::unique_ptr<CacheLayer> l1_;
    std::unique_ptr<CacheLayer> l2_;
};

/// Generate a stable cache key from components.
std::string makeCacheKey(const std::string& source_hash,
                         const std::string& deps_hash,
                         const std::string& config_hash);

/// Compute artifact filename from key.
/// Format: <prefix>_<key>.<ext> where ext is .hvb/.hvd/.hvs
std::string artifactPath(const std::string& key, const std::string& ext,
                         const std::filesystem::path& cache_dir);

}  // namespace havel::compiler::incremental