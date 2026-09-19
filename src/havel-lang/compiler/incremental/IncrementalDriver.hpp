#pragma once

// ===== Incremental Compilation Driver =====
//
// High-level driver that orchestrates the incremental compilation pipeline:
//
// 1. Fingerprint source + dependencies
// 2. Check cache (memory -> disk)
// 3. If cache hit: load artifact, skip compilation
// 4. If cache miss: compile, store in cache, update dependency graph
// 5. Update dependency tracker
// 6. Write lock file (.hvd)
//
// Per TODO.md #31/#32: Red/Green incremental model.

#include "Fingerprint.hpp"
#include "DependencyGraph.hpp"
#include "CacheLayer.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace havel::compiler::incremental {

/// Artifact types for different compilation stages.
enum class ArtifactType {
    BytecodeIR,      // .hvb - validated BytecodeIR (after pass pipeline)
    NativeCode,      // .hvc - Cranelift/LLVM native code
    OptimizationSummary, // .hvs - profile/optimization data
};

/// Result of an incremental compilation attempt.
struct CompilationResult {
    bool from_cache = false;        // true if loaded from cache
    ArtifactType type = ArtifactType::BytecodeIR;
    std::vector<uint8_t> artifact;  // serialized artifact
    std::string cache_key;          // key used
    std::string fingerprint;        // input fingerprint
    std::vector<std::string> outputs; // output files written
};

/// Configuration for the incremental driver.
struct IncrementalConfig {
    std::filesystem::path cache_root;       // cache directory root
    size_t memory_cache_entries = 1000;     // L1 cache size
    size_t memory_cache_bytes = 64 * 1024 * 1024; // 64 MB
    bool enable_disk_cache = true;
    bool enable_memory_cache = true;
    uint8_t opt_level = 2;                  // optimization level
    std::string target_arch = "x86_64";     // target architecture
    std::string ir_version = "1.0";         // IR version
    std::string compiler_version = "0.1.0"; // compiler version
};

/// Incremental compilation driver.
class IncrementalDriver {
public:
    explicit IncrementalDriver(const IncrementalConfig& config);

    /// Compile a module incrementally.
    /// - source_path: path to .hv source file
    /// - import_resolver: callback to resolve imports
    /// - compile_fn: function that does actual compilation when cache miss
    ///   Returns (artifact_bytes, fingerprint) or nullopt on failure.
    /// Defined in the header: CompileFn is a call-site lambda (no linkage),
    /// so the template cannot be instantiated from the .cpp.
    template <typename CompileFn>
    std::optional<CompilationResult> compileModule(
        const std::filesystem::path& source_path,
        const ImportResolver& import_resolver,
        CompileFn&& compile_fn) {
        // 1. Fingerprint module
        auto mod_fp = fingerprintModule(source_path, import_resolver, config_fp_);
        if (!mod_fp) {
            return std::nullopt;
        }

        // 2. Generate cache key
        std::string cache_key = makeCacheKey(
            mod_fp->source.value,
            mod_fp->deps.value,
            config_fp_.value);

        // 3. Check cache
        if (auto cached = cache_->get(cache_key)) {
            CompilationResult result;
            result.from_cache = true;
            result.artifact = *cached;
            result.cache_key = cache_key;
            result.fingerprint = mod_fp->combined.value;
            return result;
        }

        // 4. Cache miss - compile. Read source
        std::ifstream ifs(source_path);
        if (!ifs) return std::nullopt;
        std::string source((std::istreambuf_iterator<char>(ifs)), {});

        // Call the compile function
        auto artifact_opt = compile_fn(source);
        if (!artifact_opt) return std::nullopt;

        // 5. Store in cache
        CacheEntryMeta meta;
        meta.key = cache_key;
        meta.fingerprint = mod_fp->combined.value;
        meta.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.size_bytes = artifact_opt->size();
        meta.opt_level = config_.opt_level;
        meta.target_arch = config_.target_arch;
        meta.ir_version = config_.ir_version;
        meta.compiler_version = config_.compiler_version;

        cache_->put(cache_key, *artifact_opt, meta);

        // 6. Update dependency graph
        dep_graph_.addNode(source_path.string());
        for (const auto& imp : mod_fp->imports) {
            dep_graph_.addNode(imp.string());
            dep_graph_.addEdge(source_path.string(), imp.string());
        }

        // 7. Update tracker
        DependencyTracker::CompilationUnit unit;
        unit.name = source_path.string();
        unit.fingerprint = mod_fp->combined.value;
        unit.inputs = {source_path.string()};
        for (const auto& imp : mod_fp->imports) {
            unit.inputs.push_back(imp.string());
        }
        unit.outputs = {cache_key};
        unit.timestamp_ms = meta.timestamp_ms;
        tracker_.recordCompilation(unit);

        // 8. Return result
        CompilationResult result;
        result.from_cache = false;
        result.artifact = *artifact_opt;
        result.cache_key = cache_key;
        result.fingerprint = mod_fp->combined.value;
        result.outputs = {cache_key};
        return result;
    }

    /// Invalidate cache entries for changed files.
    void invalidate(const std::vector<std::filesystem::path>& changed_files);

    /// Access the underlying cache (for direct cache reads/writes outside compileModule).
    CacheLayer* cache() const { return cache_.get(); }

    /// Get dependency graph for analysis.
    const DependencyGraph& dependencyGraph() const { return dep_graph_; }

    /// Get dependency tracker.
    const DependencyTracker& tracker() const { return tracker_; }

    /// Save dependency state to lock file (.hvd).
    bool saveLockFile(const std::filesystem::path& path);

    /// Load dependency state from lock file.
    bool loadLockFile(const std::filesystem::path& path);

private:
    IncrementalConfig config_;
    std::unique_ptr<CacheLayer> cache_;
    DependencyGraph dep_graph_;
    DependencyTracker tracker_;
    Fingerprint config_fp_;
};

/// Compile a module with incremental support (convenience function).
/// Returns true if compilation was performed (not from cache).
bool compileWithIncremental(
    const std::filesystem::path& source_path,
    const ImportResolver& import_resolver,
    const IncrementalConfig& config,
    const std::function<std::optional<std::vector<uint8_t>>(const std::string& source)>& compile_fn,
    std::vector<uint8_t>& out_artifact);

}  // namespace havel::compiler::incremental