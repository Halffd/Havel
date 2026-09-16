#include "Fingerprint.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>

namespace havel::compiler::incremental {

namespace {

// Internal SHA-256 helper
std::string sha256(const uint8_t* data, size_t len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

}  // namespace

std::optional<Fingerprint> fingerprintFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::nullopt;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return std::nullopt;
    }

    // Get file size and mtime
    std::error_code ec2;
    uint64_t size = std::filesystem::file_size(path, ec2);
    if (ec2) size = 0;
    uint64_t mtime = 0;
    auto ftime = std::filesystem::last_write_time(path, ec2);
    if (!ec2) {
        mtime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    ftime.time_since_epoch()).count();
    }

    // Read and hash
    ifs.seekg(0, std::ios::end);
    std::streamsize file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(file_size);
    ifs.read(reinterpret_cast<char*>(buffer.data()), file_size);

    Fingerprint fp;
    fp.value = sha256(buffer.data(), buffer.size());
    fp.size = static_cast<uint64_t>(size);
    fp.timestamp_ms = mtime;
    return fp;
}

Fingerprint fingerprintBytes(const uint8_t* data, size_t len) {
    Fingerprint fp;
    fp.value = sha256(data, len);
    fp.size = len;
    return fp;
}

Fingerprint fingerprintString(const std::string& str) {
    return fingerprintBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

Fingerprint combineFingerprints(const std::vector<Fingerprint>& fps) {
    std::vector<std::string> sorted;
    sorted.reserve(fps.size());
    for (const auto& fp : fps) {
        sorted.push_back(fp.value);
    }
    std::sort(sorted.begin(), sorted.end());

    // Domain separator for incremental cache fingerprints
    const std::string domain = "havel::incremental::combineFingerprints:v1";
    std::string combined = domain + "\n";
    for (const auto& s : sorted) {
        combined += s + "\n";
    }
    return fingerprintString(combined);
}

Fingerprint fingerprintConfig(const std::string& compiler_version,
                              const std::string& ir_version,
                              uint8_t opt_level,
                              const std::string& target_arch,
                              const std::vector<std::string>& features) {
    std::ostringstream oss;
    oss << "havel::config:v1\n"
        << compiler_version << "\n"
        << ir_version << "\n"
        << static_cast<int>(opt_level) << "\n"
        << target_arch << "\n";
    std::vector<std::string> sorted_features = features;
    std::sort(sorted_features.begin(), sorted_features.end());
    for (const auto& f : sorted_features) {
        oss << f << "\n";
    }
    return fingerprintString(oss.str());
}

std::optional<ModuleFingerprint> fingerprintModule(
    const std::filesystem::path& source_path,
    const ImportResolver& resolver,
    const Fingerprint& config_fp) {

    std::optional<Fingerprint> source_fp = fingerprintFile(source_path);
    if (!source_fp) return std::nullopt;

    // Fingerprint all transitive imports
    std::vector<Fingerprint> dep_fps;
    std::vector<std::filesystem::path> imports;

    // Simple single-pass import resolution (in reality would be recursive)
    // For now, just fingerprint the direct import statements found in source
    std::string content;
    {
        std::ifstream ifs(source_path);
        if (!ifs) return std::nullopt;
        content.assign(std::istreambuf_iterator<char>(ifs), {});
    }

    // Parse import statements (very simple - production would use real parser)
    // Format: `use modulename` or `use { fn } from "modulename"`
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        size_t use_pos = line.find("use ");
        if (use_pos != std::string::npos) {
            // Find the module name
            size_t start = use_pos + 4;
            size_t end = line.find_first_of(" \t{\"#", start);
            if (end == std::string::npos) end = line.size();
            std::string mod_name = line.substr(start, end - start);
            if (!mod_name.empty() && mod_name.front() != '{') {
                // Remove trailing semicolon/whitespace
                mod_name.erase(mod_name.find_last_not_of(" \t;") + 1);
                if (!mod_name.empty()) {
                    auto resolved = resolver(mod_name, source_path.parent_path());
                    if (resolved) {
                        imports.push_back(*resolved);
                        if (auto dep_fp = fingerprintFile(*resolved)) {
                            dep_fps.push_back(*dep_fp);
                        }
                    }
                }
            }
        }
    }

    Fingerprint deps_fp = combineFingerprints(dep_fps);
    Fingerprint combined = combineFingerprints({*source_fp, deps_fp, config_fp});

    ModuleFingerprint mfp;
    mfp.source = *source_fp;
    mfp.deps = deps_fp;
    mfp.config = config_fp;
    mfp.combined = combined;
    mfp.imports = std::move(imports);
    return mfp;
}

bool isFingerprintCurrent(const Fingerprint& fp, const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    auto current = fingerprintFile(path);
    if (!current) return false;

    // Quick check: mtime and size match
    std::error_code ec2;
    auto ftime = std::filesystem::last_write_time(path, ec2);
    uint64_t mtime = 0;
    if (!ec2) {
        mtime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    ftime.time_since_epoch()).count();
    }
    uint64_t size = std::filesystem::file_size(path, ec2);

    return (mtime == fp.timestamp_ms || fp.timestamp_ms == 0) &&
           size == fp.size;
}

}  // namespace havel::compiler::incremental