#include "IncrementalDriver.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

namespace havel::compiler::incremental {

IncrementalDriver::IncrementalDriver(const IncrementalConfig& config)
    : config_(config) {
    // Build config fingerprint
    std::vector<std::string> features;
    config_fp_ = fingerprintConfig(
        config.compiler_version, config.ir_version, config.opt_level,
        config.target_arch, features);

    // Initialize cache layers
    auto l1 = std::make_unique<MemoryCache>(config.memory_cache_entries, config.memory_cache_bytes);
    std::unique_ptr<CacheLayer> l2;
    if (config.enable_disk_cache) {
        l2 = std::make_unique<DiskCache>(config.cache_root);
    } else {
        l2 = std::make_unique<MemoryCache>(0, 0); // no-op
    }
    cache_ = std::make_unique<TieredCache>(std::move(l1), std::move(l2));
}

// The compileModule template body lives in the header: CompileFn is a
// call-site lambda (no linkage), so instantiation must see the definition.


void IncrementalDriver::invalidate(
    const std::vector<std::filesystem::path>& changed_files) {
    std::unordered_set<std::string> changed;
    for (const auto& f : changed_files) {
        changed.insert(f.string());
    }

    // Find affected units: units whose recorded INPUTS intersect the
    // changed set are stale. unit.outputs holds the exact cache keys
    // compileModule wrote for them, so the stale artifacts are dropped
    // from the cache directly (the next compileModule call misses and
    // recompiles). The compilation record itself stays: it is history,
    // and needsRecompilation still reports the truth via the fingerprint.
    auto affected = tracker_.getAffectedUnits(changed);
    for (const auto& unit_name : affected) {
        auto unit = tracker_.getCompilation(unit_name);
        if (!unit) continue;
        for (const auto& out_key : unit->outputs) {
            cache_->remove(out_key);
        }
    }
}

bool IncrementalDriver::saveLockFile(const std::filesystem::path& path) {
    std::ofstream ofs(path);
    if (!ofs) return false;

    ofs << "HAVEL_LOCK v1\n";
    ofs << "DEPGRAF\n" << dep_graph_.serialize() << "\n";
    ofs << "DEPLOCK\n" << tracker_.serialize() << "\n";
    return true;
}

bool IncrementalDriver::loadLockFile(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return false;

    std::string header;
    if (!std::getline(ifs, header) || header != "HAVEL_LOCK v1") return false;

    std::string dep_data, lock_data;
    bool in_dep = false, in_lock = false;
    while (std::getline(ifs, header)) {
        if (header == "DEPGRAF") {
            in_dep = true; in_lock = false;
        } else if (header == "DEPLOCK") {
            in_lock = true; in_dep = false;
        } else if (in_dep) {
            dep_data += header + "\n";
        } else if (in_lock) {
            lock_data += header + "\n";
        }
    }

    auto dep_opt = DependencyGraph::deserialize(dep_data);
    if (!dep_opt) return false;
    dep_graph_ = *dep_opt;

    auto tracker_opt = DependencyTracker::deserialize(lock_data);
    if (!tracker_opt) return false;
    tracker_ = *tracker_opt;

    return true;
}

bool compileWithIncremental(
    const std::filesystem::path& source_path,
    const ImportResolver& import_resolver,
    const IncrementalConfig& config,
    const std::function<std::optional<std::vector<uint8_t>>(const std::string& source)>& compile_fn,
    std::vector<uint8_t>& out_artifact) {

    IncrementalDriver driver(config);
    auto result = driver.compileModule(source_path, import_resolver,
        [&](const std::string& source) { return compile_fn(source); });

    if (!result) return false;
    out_artifact = std::move(result->artifact);
    return !result->from_cache;
}

}  // namespace havel::compiler::incremental