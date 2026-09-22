#include "CacheLayer.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace havel::compiler::incremental {

// MemoryCache

MemoryCache::MemoryCache(size_t max_entries, size_t max_bytes)
    : max_entries_(max_entries), max_bytes_(max_bytes) {}

std::optional<std::vector<uint8_t>> MemoryCache::get(const std::string& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    it->second.last_access = ++access_counter_;
    return it->second.data;
}

bool MemoryCache::put(const std::string& key, const std::vector<uint8_t>& data,
                      const CacheEntryMeta& meta) {
    evictIfNeeded();

    size_t new_size = data.size();
    auto [it, inserted] = map_.emplace(key, Entry{data, meta, ++access_counter_});
    if (!inserted) {
        // Update existing
        current_bytes_ -= it->second.data.size();
        it->second.data = data;
        it->second.meta = meta;
        it->second.last_access = ++access_counter_;
    }
    current_bytes_ += new_size;
    return true;
}

bool MemoryCache::has(const std::string& key) {
    return map_.count(key) > 0;
}

bool MemoryCache::remove(const std::string& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return false;
    current_bytes_ -= it->second.data.size();
    map_.erase(it);
    return true;
}

void MemoryCache::clear() {
    map_.clear();
    current_bytes_ = 0;
    access_counter_ = 0;
}

size_t MemoryCache::size() const {
    return map_.size();
}

size_t MemoryCache::memoryUsage() const {
    return current_bytes_;
}

void MemoryCache::evictIfNeeded() {
    while ((map_.size() >= max_entries_ && max_entries_ > 0) ||
           (current_bytes_ >= max_bytes_ && max_bytes_ > 0)) {
        // Find LRU entry
        auto lru_it = map_.end();
        uint64_t min_access = UINT64_MAX;
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            if (it->second.last_access < min_access) {
                min_access = it->second.last_access;
                lru_it = it;
            }
        }
        if (lru_it == map_.end()) break;

        current_bytes_ -= lru_it->second.data.size();
        map_.erase(lru_it);
    }
}

// DiskCache

DiskCache::DiskCache(const std::filesystem::path& root_dir)
    : root_(root_dir) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
}

std::string DiskCache::keyToPath(const std::string& key) const {
    // Use first 2 chars as subdirectory to avoid too many files in one dir
    std::string subdir = key.substr(0, 2);
    return (root_ / subdir / (key + ".hvb")).string();
}

std::string DiskCache::keyToMetaPath(const std::string& key) const {
    std::string subdir = key.substr(0, 2);
    return (root_ / subdir / (key + ".hvd")).string();
}

std::optional<std::vector<uint8_t>> DiskCache::get(const std::string& key) {
    std::string path = keyToPath(key);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;

    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    ifs.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool DiskCache::put(const std::string& key, const std::vector<uint8_t>& data,
                    const CacheEntryMeta& meta) {
    std::error_code ec;
    std::string path = keyToPath(key);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return false;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!ofs) return false;

    // Write metadata
    std::string meta_path = keyToMetaPath(key);
    std::ofstream mofs(meta_path);
    if (!mofs) return false;

    mofs << "HVBCACHE v1\n";
    mofs << "key:" << meta.key << "\n";
    mofs << "fingerprint:" << meta.fingerprint << "\n";
    mofs << "timestamp:" << meta.timestamp_ms << "\n";
    mofs << "size:" << meta.size_bytes << "\n";
    mofs << "opt_level:" << static_cast<int>(meta.opt_level) << "\n";
    mofs << "target_arch:" << meta.target_arch << "\n";
    mofs << "ir_version:" << meta.ir_version << "\n";
    mofs << "compiler_version:" << meta.compiler_version << "\n";

    return true;
}

bool DiskCache::has(const std::string& key) {
    std::error_code ec;
    return std::filesystem::exists(keyToPath(key), ec);
}

bool DiskCache::remove(const std::string& key) {
    std::error_code ec;
    std::filesystem::remove(keyToPath(key), ec);
    std::filesystem::remove(keyToMetaPath(key), ec);
    return !ec;
}

void DiskCache::clear() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
    std::filesystem::create_directories(root_, ec);
}

size_t DiskCache::size() const {
    size_t count = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".hvb") ++count;
    }
    return count;
}

DiskCache::ValidationResult DiskCache::validate(
    const std::function<bool(const CacheEntryMeta&)>& predicate) {
    ValidationResult result;
    std::error_code ec;

    for (auto it = std::filesystem::recursive_directory_iterator(root_, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() != ".hvb") continue;

        auto meta_path = it->path();
        meta_path.replace_extension(".hvd");

        CacheEntryMeta meta;
        bool meta_ok = false;

        // Try to read metadata
        std::filesystem::path meta_path2 = it->path();
        meta_path2.replace_extension(".hvd");
        std::ifstream mofs(meta_path2);
        if (mofs) {
            std::string line;
            while (std::getline(mofs, line)) {
                if (line.rfind("fingerprint:", 0) == 0) {
                    meta.fingerprint = line.substr(12);
                    meta_ok = true;
                }
            }
        }

        if (!meta_ok) {
            result.missing_meta++;
            if (!predicate(meta)) {
                std::filesystem::remove(it->path(), ec);
                std::filesystem::path d = it->path();
                d.replace_extension(".hvd");
                std::filesystem::remove(d, ec);
                result.removed++;
            }
            continue;
        }

        if (!predicate(meta)) {
            std::filesystem::remove(it->path(), ec);
            std::filesystem::path d = it->path();
            d.replace_extension(".hvd");
            std::filesystem::remove(d, ec);
            result.removed++;
        } else {
            result.valid++;
        }
    }

    return result;
}

// TieredCache

TieredCache::TieredCache(std::unique_ptr<CacheLayer> l1,
                         std::unique_ptr<CacheLayer> l2)
    : l1_(std::move(l1)), l2_(std::move(l2)) {}

std::optional<std::vector<uint8_t>> TieredCache::get(const std::string& key) {
    if (auto data = l1_->get(key)) {
        return data;
    }
    if (auto data = l2_->get(key)) {
        // Promote to L1
        l1_->put(key, *data, CacheEntryMeta{});
        return data;
    }
    return std::nullopt;
}

bool TieredCache::put(const std::string& key, const std::vector<uint8_t>& data,
                      const CacheEntryMeta& meta) {
    l1_->put(key, data, meta);
    return l2_->put(key, data, meta);
}

bool TieredCache::has(const std::string& key) {
    return l1_->has(key) || l2_->has(key);
}

bool TieredCache::remove(const std::string& key) {
    l1_->remove(key);
    return l2_->remove(key);
}

void TieredCache::clear() {
    l1_->clear();
    l2_->clear();
}

size_t TieredCache::size() const {
    return l1_->size() + l2_->size();
}

size_t TieredCache::memoryUsage() const {
    return l1_->memoryUsage();
}

// Utilities

std::string makeCacheKey(const std::string& source_hash,
                         const std::string& deps_hash,
                         const std::string& config_hash) {
    std::string combined = "havel::cache:v1\n" + source_hash + "\n" + deps_hash + "\n" + config_hash + "\n";
    // Simple hash - in production use SHA-256
    std::hash<std::string> hasher;
    size_t h = hasher(combined);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

std::string artifactPath(const std::string& key, const std::string& ext,
                         const std::filesystem::path& cache_dir) {
    std::string subdir = key.substr(0, 2);
    return (cache_dir / subdir / (key + ext)).string();
}

}  // namespace havel::compiler::incremental