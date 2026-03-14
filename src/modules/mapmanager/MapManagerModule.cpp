/*
 * MapManagerModule.cpp
 * 
 * Map Manager module for Havel language.
 * Host binding - connects language to MapManager.
 */
#include "MapManagerModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/io/MapManager.hpp"
#include "core/IO.hpp"

namespace havel::modules {

// Static instance - matches the pattern in Interpreter.cpp
static std::unique_ptr<MapManager> coreMapManager;

void registerMapManagerModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    // Create mapmanager module object
    auto mapManagerObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
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
    // Initialize MapManager
    // =========================================================================
    
    (*mapManagerObj)["init"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        if (!coreMapManager && hostAPI->GetIO()) {
            coreMapManager = std::make_unique<MapManager>(hostAPI->GetIO());
        }
        return HavelValue(coreMapManager != nullptr);
    }));
    
    // =========================================================================
    // Profile management
    // =========================================================================
    
    (*mapManagerObj)["addProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
            return HavelRuntimeError("mapmanager.addProfile() requires (id, name)");
        }
        
        std::string id = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string name = args[1].isString() ? args[1].asString() : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string desc = args.size() > 2 ? (args[2].isString() ? args[2].asString() : "") : "";
        
        Profile profile;
        profile.id = id;
        profile.name = name;
        profile.description = desc;
        
        coreMapManager->AddProfile(profile);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["removeProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
            return HavelRuntimeError("mapmanager.removeProfile() requires profileId");
        }
        
        std::string id = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->RemoveProfile(id);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["setActiveProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
            return HavelRuntimeError("mapmanager.setActiveProfile() requires profileId");
        }
        
        std::string id = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->SetActiveProfile(id);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["getActiveProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        return HavelValue(coreMapManager->GetActiveProfileId());
    }));
    
    (*mapManagerObj)["getProfileIds"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto arr = std::make_shared<std::vector<HavelValue>>();
        if (coreMapManager) {
            auto ids = coreMapManager->GetProfileIds();
            for (const auto& id : ids) {
                arr->push_back(HavelValue(id));
            }
        }
        return HavelValue(arr);
    }));
    
    (*mapManagerObj)["nextProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        if (coreMapManager) {
            coreMapManager->NextProfile();
        }
        return HavelValue(nullptr);
    }));
    
    (*mapManagerObj)["previousProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        if (coreMapManager) {
            coreMapManager->PreviousProfile();
        }
        return HavelValue(nullptr);
    }));
    
    (*mapManagerObj)["enableProfile"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 2) {
            return HavelRuntimeError("mapmanager.enableProfile() requires (profileId, enable)");
        }
        
        std::string profileId = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        bool enable = args[1].asBool();
        coreMapManager->EnableProfile(profileId, enable);
        return HavelValue(true);
    }));
    
    // =========================================================================
    // Mapping management
    // =========================================================================
    
    (*mapManagerObj)["addMapping"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 3) {
            return HavelRuntimeError("mapmanager.addMapping() requires (profileId, sourceKey, targetKey)");
        }
        
        std::string profileId = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey = args[1].isString() ? args[1].asString() : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey = args[2].isString() ? args[2].asString() : std::to_string(static_cast<int>(args[2].asNumber()));
        
        Mapping mapping;
        mapping.id = sourceKey + "_to_" + targetKey;
        mapping.name = sourceKey + " -> " + targetKey;
        mapping.sourceKey = sourceKey;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;
        
        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["removeMapping"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 2) {
            return HavelRuntimeError("mapmanager.removeMapping() requires (profileId, mappingId)");
        }
        
        std::string profileId = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId = args[1].isString() ? args[1].asString() : std::to_string(static_cast<int>(args[1].asNumber()));
        coreMapManager->RemoveMapping(profileId, mappingId);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["clearAllMappings"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        if (coreMapManager) {
            coreMapManager->ClearAllMappings();
        }
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // Save/Load profiles
    // =========================================================================
    
    (*mapManagerObj)["saveProfiles"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
            return HavelRuntimeError("mapmanager.saveProfiles() requires filepath");
        }
        
        std::string path = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->SaveProfiles(path);
        return HavelValue(true);
    }));
    
    (*mapManagerObj)["loadProfiles"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (!coreMapManager) {
            return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
            return HavelRuntimeError("mapmanager.loadProfiles() requires filepath");
        }
        
        std::string path = args[0].isString() ? args[0].asString() : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->LoadProfiles(path);
        return HavelValue(true);
    }));
    
    // Register mapmanager module
    env.Define("mapmanager", HavelValue(mapManagerObj));
}

} // namespace havel::modules
