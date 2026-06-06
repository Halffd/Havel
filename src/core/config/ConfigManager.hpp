#pragma once

#include "c/Config.h"
#include "utils/Logger.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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
}

class Configs {
public:
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

 void Reload() { HavelConfig_reload(handle()); }

 static void SetPath(const std::string &newPath) {
 HavelConfig_setPath(newPath.c_str());
 }

 void EnsureConfigFile(const std::string &filename = "havel.cfg") {
 HavelConfig_ensureConfigFile(handle(), filename.c_str());
 }

 template <typename T>
 T Get(const std::string &key, const T &defaultVal) const {
 (void)key;
 return defaultVal;
 }

 template <typename T>
 void Set(const std::string &key, const T &value, bool save = false);

 void Set(const std::string &key, const std::string &value, bool save = false) {
 HavelConfig_setString(handle(), key.c_str(), value.c_str(), save);
 }

 bool Remove(const std::string &key) {
 return HavelConfig_remove(handle(), key.c_str());
 }

 bool Has(const std::string &key) const {
 return HavelConfig_has(handle(), key.c_str());
 }

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

 std::vector<std::string> GetConfigs() const {
 char** entries = nullptr;
 size_t n = HavelConfig_getConfigs(handle(), &entries);
 std::vector<std::string> result;
 result.reserve(n);
 for (size_t i = 0; i < n; i++) result.emplace_back(entries[i]);
 HavelConfig_freeEntries(entries, n);
 return result;
 }

 void ForceSave() { HavelConfig_forceSave(handle()); }
 void RequestSave() { HavelConfig_requestSave(handle()); }

private:
 Configs() = default;
 ~Configs() = default;
 Configs(const Configs &) = delete;
 Configs &operator=(const Configs &) = delete;

 HavelConfig* handle() const { return HavelConfig_getInstance(); }
};

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

}

inline havel::Configs &Conf() { return havel::Configs::Get(); }
