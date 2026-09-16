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
    template <typename CompileFn>
    std::optional<CompilationResult> compileModule(
        const std::filesystem::path& source_path,
        const ImportResolver& import_resolver,
        CompileFn&& compile_fn);

    /// Invalidate cache entries for changed files.
    void invalidate(const std::vector<std::filesystem::path>& changed_files);

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