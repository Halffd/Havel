/*
 * ConfigManager.hpp
 *
 * Thin C++ wrapper over the C Config API (Config.h / Config.c).
 * All state lives in the C implementation. This header provides
 * backward-compatible C++ types and convenience methods.
 */
#pragma once

#include "core/config/Config.h"
#include "utils/Logger.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

namespace havel {

namespace ConfigPaths {
inline std::string GetDefaultConfigDir() {
    char buf[4096];
    HavelConfig_getConfigDir(buf, sizeof(buf));
    return std::string(buf);
}

inline void SetConfigPath(const std::string &path, const std::string &basename = "havel.cfg") {
    HavelConfig_setConfigPath(path.c_str(), basename.c_str());
}

inline std::string GetConfigPath() {
    char buf[4096];
    HavelConfig_getConfigDir(buf, sizeof(buf));
    return std::string(buf);
}

inline std::string GetConfigPath(const std::string &filename) {
    char buf[4096];
    HavelConfig_getConfigFilePath(filename.c_str(), buf, sizeof(buf));
    return std::string(buf);
}

inline void EnsureConfigDir() {
    HavelConfig_ensureConfigDir();
}
} // namespace ConfigPaths

class Configs {
public:
    enum Level { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARNING = 2, LOG_ERROR = 3, LOG_FATAL = 4 };

    static Configs &Get() {
        static Configs instance;
        return instance;
    }

    void Load(const std::string &filename = "havel.cfg") {
        HavelConfig_load(handle(), filename.c_str());
    }

    void Save(const std::string &filename = "") {
        HavelConfig_save(handle(), filename.empty() ? NULL : filename.c_str());
    }

    void Reload() {
        HavelConfig_reload(handle());
    }

    std::string getPath() {
        return std::string(HavelConfig_getPath(handle()));
    }

    static void SetPath(const std::string &newPath) {
        HavelConfig_setPath(newPath.c_str());
    }

    void StartFileWatching(const std::string &filename = "havel.cfg") {
        HavelConfig_startFileWatching(handle(), filename.c_str());
    }

    void StopFileWatching() {
        HavelConfig_stopFileWatching(handle());
    }

    void EnsureConfigFile(const std::string &filename = "havel.cfg") {
        HavelConfig_ensureConfigFile(handle(), filename.c_str());
    }

    // --- Typed getters (all specializations inline in class body) ---

    template <typename T>
    T Get(const std::string &key, const T &defaultVal) const {
        // Fallback: return default for unsupported types
        (void)key;
        return defaultVal;
    }

    // --- Typed setters ---

    // Typed setters — only explicit specializations exist (bool, int, double, string)
    template <typename T>
    void Set(const std::string &key, const T &value, bool save = false);

    // String setter (non-template overload, not specialization)
    void Set(const std::string &key, const std::string &value, bool save = false) {
        HavelConfig_setString(handle(), key.c_str(), value.c_str(), save);
    }

    bool Remove(const std::string &key) {
        return HavelConfig_remove(handle(), key.c_str());
    }

    bool Has(const std::string &key) const {
        return HavelConfig_has(handle(), key.c_str());
    }

    void BeginBatch() {}
    void EndBatch() { HavelConfig_requestSave(handle()); }

    std::vector<std::string> GetAllKeys() const {
        char** keys = nullptr;
        size_t n = HavelConfig_getAllKeys(handle(), &keys);
        std::vector<std::string> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) result.emplace_back(keys[i]);
        HavelConfig_freeKeys(keys, n);
        return result;
    }

    bool GetVerboseKeyLogging() const {
        return HavelConfig_getVerboseKeyLogging(handle());
    }

    bool GetVerboseWindowLogging() const {
        return HavelConfig_getVerboseWindowLogging(handle());
    }

    bool GetVerboseConditionLogging() const {
        return HavelConfig_getVerboseConditionLogging(handle());
    }

    std::vector<std::string> GetGamingApps() const {
        return splitComma(HavelConfig_getString(handle(), "General.GamingApps", ""));
    }
    std::vector<std::string> GetGamingAppsExclude() const {
        return splitComma(HavelConfig_getString(handle(), "General.GamingAppsExclude", ""));
    }
    std::vector<std::string> GetGamingAppsExcludeTitle() const {
        return splitComma(HavelConfig_getString(handle(), "General.GamingAppsExcludeTitle", ""));
    }
    std::vector<std::string> GetGamingAppsTitle() const {
        return splitComma(HavelConfig_getString(handle(), "General.GamingAppsTitle", ""));
    }

    std::vector<std::string> GetConfigs() const {
        char** entries = nullptr;
        size_t n = HavelConfig_getConfigs(handle(), &entries);
        std::vector<std::string> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) result.emplace_back(entries[i]);
        HavelConfig_freeEntries(entries, n);
        return result;
    }

    using WatchCallback = std::function<void(const std::string &)>;

    void Watch(const std::string &key, WatchCallback callback) {
        auto* heapCb = new WatchCallback(std::move(callback));
        HavelConfig_watch(handle(), key.c_str(),
            [](const char*, const char* v, void* ud) {
                auto* cb = static_cast<WatchCallback*>(ud);
                try { (*cb)(v); } catch (...) {}
            }, heapCb);
    }

    void RequestSave() { HavelConfig_requestSave(handle()); }
    void ForceSave()   { HavelConfig_forceSave(handle()); }
    void Print() const { HavelConfig_print(handle()); }

    template <typename T> static T Convert(const std::string &val);

private:
    Configs() = default;
    ~Configs() = default;
    Configs(const Configs &) = delete;
    Configs &operator=(const Configs &) = delete;

    HavelConfig* handle() const { return HavelConfig_getInstance(); }

    static std::vector<std::string> splitComma(const char* s) {
        std::vector<std::string> result;
        if (!s || !*s) return result;
        std::string str(s);
        size_t start = 0;
        while (start < str.size()) {
            size_t comma = str.find(',', start);
            std::string item = str.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty()) result.push_back(item);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return result;
    }
};

// Explicit specializations of Get<T> — outside class body, after class is complete
template <>
inline std::string Configs::Get<std::string>(const std::string &key, const std::string &defaultVal) const {
    return std::string(HavelConfig_getString(handle(), key.c_str(), defaultVal.c_str()));
}
template <>
inline bool Configs::Get<bool>(const std::string &key, const bool &defaultVal) const {
    return HavelConfig_getBool(handle(), key.c_str(), defaultVal);
}
template <>
inline int Configs::Get<int>(const std::string &key, const int &defaultVal) const {
    return HavelConfig_getInt(handle(), key.c_str(), defaultVal);
}
template <>
inline double Configs::Get<double>(const std::string &key, const double &defaultVal) const {
    return HavelConfig_getDouble(handle(), key.c_str(), defaultVal);
}

// Explicit specializations of Set<T> — outside class body
template <>
inline void Configs::Set<int>(const std::string &key, const int &value, bool save) {
    HavelConfig_setInt(handle(), key.c_str(), value, save);
}
template <>
inline void Configs::Set<long>(const std::string &key, const long &value, bool save) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    HavelConfig_setString(handle(), key.c_str(), buf, save);
}
template <>
inline void Configs::Set<double>(const std::string &key, const double &value, bool save) {
    HavelConfig_setDouble(handle(), key.c_str(), value, save);
}
template <>
inline void Configs::Set<bool>(const std::string &key, const bool &value, bool save) {
    HavelConfig_setBool(handle(), key.c_str(), value, save);
}
template <>
inline void Configs::Set<const char*>(const std::string &key, const char* const &value, bool save) {
    HavelConfig_setString(handle(), key.c_str(), value ? value : "", save);
}
template <>
inline void Configs::Set<std::string>(const std::string &key, const std::string &value, bool save) {
    HavelConfig_setString(handle(), key.c_str(), value.c_str(), save);
}

// Convert specializations
template <> inline bool Configs::Convert<bool>(const std::string &val) {
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "true" || lower == "yes" || lower == "1" || lower == "on");
}
template <> inline int Configs::Convert<int>(const std::string &val) { return std::stoi(val); }
template <> inline double Configs::Convert<double>(const std::string &val) { return std::stod(val); }
template <> inline float Configs::Convert<float>(const std::string &val) { return std::stof(val); }
template <> inline std::string Configs::Convert<std::string>(const std::string &val) { return val; }

inline void BackupConfig(const std::string &path = "havel.cfg") {
    std::string configPath = ConfigPaths::GetConfigPath(path);
    std::string backupPath = configPath + ".bak";
    if (rename(configPath.c_str(), backupPath.c_str()) != 0) {
        havel::error("Config backup failed: {}", strerror(errno));
    }
}

} // namespace havel

inline havel::Configs &Conf() { return havel::Configs::Get(); }
