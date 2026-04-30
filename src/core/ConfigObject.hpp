/*
 * ConfigObject.hpp
 * 
 * A wrapper class for configuration objects that provides:
 * - Better encapsulation than raw unordered_map
 * - Type-safe accessors
 * - Validation support
 * - Default values
 * - Schema support (future)
 * - Hot reload notifications
 */
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "utils/Util.hpp"

namespace havel {

// Forward declaration
class ConfigObject;

// Type aliases for config values
using ConfigValue = std::string;
using ConfigMap = std::unordered_map<std::string, ConfigValue>;
using ConfigObjectPtr = std::shared_ptr<ConfigObject>;

// Trim whitespace helper
using havel::trim; // from Util.hpp
// inline std::string trim(const std::string &str) {

// Case-insensitive string comparison
inline bool iequals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(a[i]) != std::tolower(b[i])) return false;
    }
    return true;
}

/**
 * ConfigObject - Wrapper for configuration data
 * 
 * Provides type-safe access to configuration values with support for:
 * - Getting/setting values by key
 * - Type conversion (bool, int, double, string)
 * - Default values
 * - Nested configuration (ConfigObject can contain ConfigObject)
 * - Validation callbacks
 */
class ConfigObject {
public:
    // Constructors
    ConfigObject() = default;
    ConfigObject(const std::string &name) : name_(name) {}
    
    // Copy/move semantics
    ConfigObject(const ConfigObject &) = default;
    ConfigObject &operator=(const ConfigObject &) = default;
    ConfigObject(ConfigObject &&) = default;
    ConfigObject &operator=(ConfigObject &&) = default;
    
    // Value access - string
    std::string getString(const std::string &key, const std::string &defaultVal = "") const {
        auto it = values_.find(key);
        return (it != values_.end()) ? trim(it->second) : defaultVal;
    }
    
    void setString(const std::string &key, const std::string &value) {
        values_[key] = trim(value);
        notifyChange(key);
    }
    
    // Value access - boolean
    bool getBool(const std::string &key, bool defaultVal = false) const {
        auto it = values_.find(key);
        if (it == values_.end()) return defaultVal;
        
        const std::string &val = it->second;
        if (iequals(val, "true") || iequals(val, "yes") || 
            iequals(val, "on") || val == "1") {
            return true;
        }
        if (iequals(val, "false") || iequals(val, "no") || 
            iequals(val, "off") || val == "0") {
            return false;
        }
        return defaultVal;
    }
    
    void setBool(const std::string &key, bool value) {
        values_[key] = value ? "true" : "false";
        notifyChange(key);
    }
    
    // Value access - integer
    int getInt(const std::string &key, int defaultVal = 0) const {
        auto it = values_.find(key);
        if (it == values_.end()) return defaultVal;
        
        try {
            return std::stoi(trim(it->second));
        } catch (...) {
            return defaultVal;
        }
    }
    
    void setInt(const std::string &key, int value) {
        values_[key] = std::to_string(value);
        notifyChange(key);
    }
    
    // Value access - double
    double getDouble(const std::string &key, double defaultVal = 0.0) const {
        auto it = values_.find(key);
        if (it == values_.end()) return defaultVal;
        
        try {
            return std::stod(trim(it->second));
        } catch (...) {
            return defaultVal;
        }
    }
    
    void setDouble(const std::string &key, double value) {
        std::ostringstream oss;
        oss << value;
        values_[key] = oss.str();
        notifyChange(key);
    }
    
    // Value access - generic (template)
    template <typename T>
    T get(const std::string &key, const T &defaultVal) const {
        // Specializations below
        return defaultVal;
    }
    
    template <typename T>
    void set(const std::string &key, const T &value) {
        // Convert to string and store
        std::ostringstream oss;
        oss << value;
        setString(key, oss.str());
    }
    
    // Check if key exists
    bool has(const std::string &key) const {
        return values_.find(key) != values_.end();
    }
    
    // Remove key
    bool remove(const std::string &key) {
        auto it = values_.find(key);
        if (it != values_.end()) {
            notifyChange(key);
            values_.erase(it);
            return true;
        }
        return false;
    }
    
    // Clear all values
    void clear() {
        values_.clear();
    }
    
    // Get all keys
    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(values_.size());
        for (const auto &[key, value] : values_) {
            result.push_back(key);
        }
        return result;
    }
    
    // Get raw map (for iteration)
    const ConfigMap &values() const { return values_; }
    ConfigMap &values() { return values_; }
    
    // Nested config objects
    void setObject(const std::string &key, const ConfigObject &obj) {
        nestedObjects_[key] = obj;
    }
    
    ConfigObject *getObject(const std::string &key) {
        auto it = nestedObjects_.find(key);
        return (it != nestedObjects_.end()) ? &it->second : nullptr;
    }
    
    const ConfigObject *getObject(const std::string &key) const {
        auto it = nestedObjects_.find(key);
        return (it != nestedObjects_.end()) ? &it->second : nullptr;
    }
    
    // Validation
    using Validator = std::function<bool(const std::string &key, const std::string &value)>;
    
    void setValidator(Validator v) { validator_ = v; }
    
    // Change notification
    using ChangeCallback = std::function<void(const std::string &key)>;
    
    void onChange(ChangeCallback cb) { changeCallbacks_.push_back(cb); }
    
    // Name (for nested configs)
    const std::string &name() const { return name_; }
    void setName(const std::string &name) { name_ = name; }
    
    // Size
    size_t size() const { return values_.size(); }
    
    // Empty check
    bool empty() const { return values_.empty() && nestedObjects_.empty(); }
    
    // Merge with another config (other values override this)
    void merge(const ConfigObject &other) {
        for (const auto &[key, value] : other.values_) {
            values_[key] = value;
        }
        for (const auto &[key, obj] : other.nestedObjects_) {
            nestedObjects_[key] = obj;
        }
    }
    
    // Convert to TOML-style string
    std::string toString(const std::string &section = "") const {
        std::ostringstream oss;

        // Group keys by section (dot-separated: Section.Key)
        std::map<std::string, std::map<std::string, std::string>> sections;
        std::map<std::string, std::string> rootKeys;

        for (const auto &[key, value] : values_) {
            size_t dotPos = key.find('.');
            if (dotPos != std::string::npos) {
                std::string sec = key.substr(0, dotPos);
                std::string subKey = key.substr(dotPos + 1);
                sections[sec][subKey] = value;
            } else {
                rootKeys[key] = value;
            }
        }

        // Write root-level keys first (no section header)
        for (const auto &[key, value] : rootKeys) {
            oss << key << " = " << value << "\n";
        }

        // Write each section
        for (const auto &[sec, keys] : sections) {
            if (!rootKeys.empty() || &sec != &sections.begin()->first) {
                oss << "\n";
            }
            oss << "[" << sec << "]\n";
            for (const auto &[key, value] : keys) {
                oss << key << " = " << value << "\n";
            }
        }

        // Write nested objects (if any)
        for (const auto &[key, obj] : nestedObjects_) {
            oss << "\n" << obj.toString(key);
        }

        return oss.str();
    }
    
    // Parse from TOML-style string (section headers create dot-prefixed keys)
    static ConfigObject fromString(const std::string &str) {
        ConfigObject result;
        std::istringstream iss(str);
        std::string line, currentSection;

        while (std::getline(iss, line)) {
            line = trim(line);

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#' || line[0] == ';') {
                continue;
            }

            // Section header
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                // Handle [[array_of_tables]] by stripping one bracket pair
                if (currentSection.size() >= 2 && currentSection.front() == '[' && currentSection.back() == ']') {
                    currentSection = currentSection.substr(1, currentSection.size() - 2);
                }
                result.nestedObjects_[currentSection] = ConfigObject(currentSection);
                continue;
            }

            // Key=value pair
            size_t delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = trim(line.substr(0, delim));
                std::string value = trim(line.substr(delim + 1));

                if (!currentSection.empty()) {
                    result.values_[currentSection + "." + key] = value;
                    auto *obj = result.getObject(currentSection);
                    if (obj) {
                        obj->values_[key] = value;
                    }
                } else {
                    result.values_[key] = value;
                }
            }
        }

        return result;
    }

private:
    void notifyChange(const std::string &key) {
        for (auto &cb : changeCallbacks_) {
            try {
                cb(key);
            } catch (...) {
                // Ignore callback errors
            }
        }
    }
    
    std::string name_;
    ConfigMap values_;
    std::unordered_map<std::string, ConfigObject> nestedObjects_;
    Validator validator_;
    std::vector<ChangeCallback> changeCallbacks_;
};

// Template specializations for generic get/set
template <>
inline std::string ConfigObject::get<std::string>(const std::string &key, const std::string &defaultVal) const {
    return getString(key, defaultVal);
}

template <>
inline bool ConfigObject::get<bool>(const std::string &key, const bool &defaultVal) const {
    return getBool(key, defaultVal);
}

template <>
inline int ConfigObject::get<int>(const std::string &key, const int &defaultVal) const {
    return getInt(key, defaultVal);
}

template <>
inline double ConfigObject::get<double>(const std::string &key, const double &defaultVal) const {
    return getDouble(key, defaultVal);
}

template <>
inline void ConfigObject::set<std::string>(const std::string &key, const std::string &value) {
    setString(key, value);
}

template <>
inline void ConfigObject::set<bool>(const std::string &key, const bool &value) {
    setBool(key, value);
}

template <>
inline void ConfigObject::set<int>(const std::string &key, const int &value) {
    setInt(key, value);
}

template <>
inline void ConfigObject::set<double>(const std::string &key, const double &value) {
    setDouble(key, value);
}

} // namespace havel
