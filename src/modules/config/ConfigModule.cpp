/*
 * ConfigModule.cpp
 * 
 * Configuration module for Havel language.
 * Provides access to ConfigManager for reading/writing configuration.
 */
#include "ConfigModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/ConfigManager.hpp"

namespace havel::modules {

void registerConfigModule(Environment& env, IHostAPI*) {
    // Create config object with proper namespace
    auto configObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // =========================================================================
    // config.get(key, [default]) - Get configuration value
    // =========================================================================
    
    (*configObj)["get"] = HavelValue(BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("config.get() requires key");
        }
        
        std::string key = valueToString(args[0]);
        std::string def = args.size() >= 2 ? valueToString(args[1]) : std::string("");
        
        auto& config = Configs::Get();
        return HavelValue(config.Get<std::string>(key, def));
    }));
    
    // =========================================================================
    // config.set(key, value) - Set configuration value
    // =========================================================================
    
    (*configObj)["set"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("config.set() requires (key, value)");
        }

        std::string key = args[0].isString() ? args[0].asString() :
            std::to_string(static_cast<int>(args[0].asNumber()));

        auto& config = Configs::Get();
        const HavelValue& value = args[1];

        if (value.is<bool>()) {
            config.Set(key, value.get<bool>(), true);  // Save to disk
        } else if (value.is<int>()) {
            config.Set(key, value.get<int>(), true);  // Save to disk
        } else if (value.is<double>()) {
            config.Set(key, value.get<double>(), true);  // Save to disk
        } else {
            config.Set(key, value.isString() ? value.asString() :
                std::to_string(static_cast<int>(value.asNumber())), true);  // Save to disk
        }
        return HavelValue(true);
    }));
    
    // =========================================================================
    // config.setPath(path) - Set configuration file path
    // =========================================================================
    
    (*configObj)["setPath"] = HavelValue(BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 1) {
            return HavelRuntimeError("config.setPath() requires (path)");
        }
        
        std::string path = valueToString(args[0]);
        auto& config = Configs::Get();
        config.SetPath(path);
        return HavelValue(true);
    }));
    
    // =========================================================================
    // config.load([path]) - Load configuration from file
    // =========================================================================
    
    (*configObj)["load"] = HavelValue(BuiltinFunction([valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        try {
            auto& config = Configs::Get();
            if (args.empty()) {
                config.Reload();
            } else {
                config.Load(valueToString(args[0]));
            }
            std::cout << "[INFO] Configuration loaded successfully" << std::endl;
            return HavelValue(true);
        } catch (const std::exception& e) {
            return HavelRuntimeError("Failed to load configuration: " + std::string(e.what()));
        }
    }));
    
    // =========================================================================
    // config.reload() - Reload configuration from current path
    // =========================================================================

    (*configObj)["reload"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        try {
            auto& config = Configs::Get();
            config.Reload();
            std::cout << "[INFO] Configuration reloaded successfully" << std::endl;
            return HavelValue(true);
        } catch (const std::exception& e) {
            return HavelRuntimeError("Failed to reload configuration: " + std::string(e.what()));
        }
    }));

    // =========================================================================
    // config.save() - Save configuration to disk
    // =========================================================================

    (*configObj)["save"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        try {
            auto& config = Configs::Get();
            config.Save();
            std::cout << "[INFO] Configuration saved successfully" << std::endl;
            return HavelValue(true);
        } catch (const std::exception& e) {
            return HavelRuntimeError("Failed to save configuration: " + std::string(e.what()));
        }
    }));

    // Register config module
    env.Define("config", HavelValue(configObj));
}

} // namespace havel::modules
