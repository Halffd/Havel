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

template <typename CompileFn>
std::optional<CompilationResult> IncrementalDriver::compileModule(
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

    // 4. Cache miss - compile
    // Read source
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

// Template instantiation is handled implicitly since CompileFn is templated.

void IncrementalDriver::invalidate(
    const std::vector<std::filesystem::path>& changed_files) {
    std::unordered_set<std::string> changed;
    for (const auto& f : changed_files) {
        changed.insert(f.string());
    }

    // Find affected units
    auto affected = tracker_.getAffectedUnits(changed);
    for (const auto& unit_name : affected) {
        // Remove from cache
        // Note: we'd need to know the cache key for each unit
        // For now, just remove from tracker
        // In practice, we'd maintain a reverse mapping
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