/*
 * MapManagerModule.cpp
 *
 * Map Manager module for Havel language.
 * Host binding - connects language to MapManager.
 */
#include "MapManagerModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/IO.hpp"
#include "core/io/MapManager.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace havel::modules {

// Static instance - matches the pattern in Interpreter.cpp
static std::unique_ptr<MapManager> coreMapManager;

void registerMapManagerModule(Environment &env,
                              std::shared_ptr<IHostAPI> hostAPI) {
  // Create mapmanager module object
  auto mapManagerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Helper to convert value to string
  auto valueToString = [](const HavelValue &v) -> std::string {
    if (v.isString())
      return v.asString();
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
    if (v.isBool())
      return v.asBool() ? "true" : "false";
    return "";
  };

  // =========================================================================
  // Initialize MapManager
  // =========================================================================

  (*mapManagerObj)["init"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager && hostAPI->GetIO()) {
          coreMapManager = std::make_unique<MapManager>(hostAPI->GetIO());
        }

        // Check if auto-activation should be prevented (for script
        // initialization)
        bool preventAutoActivation = false;
        if (args.size() > 0) {
          // If first argument is true or false, control auto-activation
          if (args[0].isBool()) {
            preventAutoActivation = !args[0].asBool();
          }
        }

        if (!preventAutoActivation) {
          // Auto-create and activate "default" profile on initialization
          if (coreMapManager) {
            // Check if default profile exists
            auto profileIds = coreMapManager->GetProfileIds();
            bool hasDefault = std::find(profileIds.begin(), profileIds.end(),
                                        "default") != profileIds.end();

            if (!hasDefault) {
              // Create default profile
              Profile defaultProfile;
              defaultProfile.id = "default";
              defaultProfile.name = "Default Profile";
              defaultProfile.description =
                  "Automatically created default profile for general use";
              defaultProfile.enabled = true;

              coreMapManager->AddProfile(defaultProfile);
              spdlog::info("Created default MapManager profile");
            }

            // Activate default profile
            coreMapManager->SetActiveProfile("default");
            spdlog::info("Activated default MapManager profile on startup");

            // Ensure hotkeys are registered to active profile
            coreMapManager->ApplyActiveProfile();
            spdlog::info("Applied hotkeys to active MapManager profile");
          }
        } else {
          // Manual initialization - don't auto-activate or apply hotkeys
          if (coreMapManager) {
            spdlog::info(
                "MapManager initialized without auto-activation (manual mode)");
          }
        }

        return HavelValue(coreMapManager != nullptr);
      }));

  // =========================================================================
  // Profile management
  // =========================================================================

  (*mapManagerObj)["addProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.addProfile() requires (id, name)");
        }

        std::string id =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string name =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string desc = args.size() > 2
                               ? (args[2].isString() ? args[2].asString() : "")
                               : "";

        Profile profile;
        profile.id = id;
        profile.name = name;
        profile.description = desc;

        coreMapManager->AddProfile(profile);
        return HavelValue(true);
      }));

  (*mapManagerObj)["removeProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
          return HavelRuntimeError(
              "mapmanager.removeProfile() requires profileId");
        }

        std::string id =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->RemoveProfile(id);
        return HavelValue(true);
      }));

  (*mapManagerObj)["setActiveProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
          return HavelRuntimeError(
              "mapmanager.setActiveProfile() requires profileId");
        }

        std::string id =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->SetActiveProfile(id);
        return HavelValue(true);
      }));

  (*mapManagerObj)["getActiveProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        return HavelValue(coreMapManager->GetActiveProfileId());
      }));

  (*mapManagerObj)["getProfileIds"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        auto arr = std::make_shared<std::vector<HavelValue>>();
        if (coreMapManager) {
          auto ids = coreMapManager->GetProfileIds();
          for (const auto &id : ids) {
            arr->push_back(HavelValue(id));
          }
        }
        return HavelValue(arr);
      }));

  (*mapManagerObj)["nextProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (coreMapManager) {
          coreMapManager->NextProfile();
        }
        return HavelValue(nullptr);
      }));

  (*mapManagerObj)["previousProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (coreMapManager) {
          coreMapManager->PreviousProfile();
        }
        return HavelValue(nullptr);
      }));

  (*mapManagerObj)["enableProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.enableProfile() requires (profileId, enable)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        bool enable = args[1].asBool();
        coreMapManager->EnableProfile(profileId, enable);
        return HavelValue(true);
      }));

  // =========================================================================
  // Mapping management
  // =========================================================================

  (*mapManagerObj)["addMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.addMapping() requires "
                                   "(profileId, sourceKey, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

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

  (*mapManagerObj)["removeMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.removeMapping() requires (profileId, mappingId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        coreMapManager->RemoveMapping(profileId, mappingId);
        return HavelValue(true);
      }));

  (*mapManagerObj)["clearAllMappings"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (coreMapManager) {
          coreMapManager->ClearAllMappings();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Save/Load profiles
  // =========================================================================

  (*mapManagerObj)["saveProfiles"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
          return HavelRuntimeError(
              "mapmanager.saveProfiles() requires filepath");
        }

        std::string path =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->SaveProfiles(path);
        return HavelValue(true);
      }));

  (*mapManagerObj)["loadProfiles"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError("MapManager not initialized");
        }
        if (args.empty()) {
          return HavelRuntimeError(
              "mapmanager.loadProfiles() requires filepath");
        }

        std::string path =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        coreMapManager->LoadProfiles(path);
        return HavelValue(true);
      }));

  (*mapManagerObj)["registerHotkeys"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        // Apply the active profile to ensure all hotkeys are registered
        coreMapManager->ApplyActiveProfile();
        info("Registered all hotkeys to active MapManager profile");
        return HavelValue(true);
      }));

  (*mapManagerObj)["getActiveProfile"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        std::string activeProfileId = coreMapManager->GetActiveProfileId();
        if (activeProfileId.empty()) {
          return HavelValue("No active profile");
        }

        auto profile = coreMapManager->GetProfile(activeProfileId);
        if (!profile) {
          return HavelValue("Active profile not found");
        }

        // Create profile info object
        auto profileObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*profileObj)["id"] = HavelValue(profile->id);
        (*profileObj)["name"] = HavelValue(profile->name);
        (*profileObj)["description"] = HavelValue(profile->description);
        (*profileObj)["enabled"] = HavelValue(profile->enabled);
        (*profileObj)["mappingCount"] =
            HavelValue(static_cast<double>(profile->mappings.size()));

        return HavelValue(profileObj);
      }));

  // =========================================================================
  // List, Query, and Filter Functions
  // =========================================================================

  (*mapManagerObj)["listProfiles"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        auto profileIds = coreMapManager->GetProfileIds();
        auto profilesArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto &profileId : profileIds) {
          auto profile = coreMapManager->GetProfile(profileId);
          if (profile) {
            auto profileObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*profileObj)["id"] = HavelValue(profile->id);
            (*profileObj)["name"] = HavelValue(profile->name);
            (*profileObj)["description"] = HavelValue(profile->description);
            (*profileObj)["enabled"] = HavelValue(profile->enabled);
            (*profileObj)["mappingCount"] =
                HavelValue(static_cast<double>(profile->mappings.size()));
            profilesArray->push_back(HavelValue(profileObj));
          }
        }

        return HavelValue(profilesArray);
      }));

  (*mapManagerObj)["getMappings"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.empty()) {
          return HavelRuntimeError(
              "mapmanager.getMappings() requires profileId");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        auto mappings = coreMapManager->GetMappings(profileId);
        auto mappingsArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto *mapping : mappings) {
          if (mapping) {
            auto mappingObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*mappingObj)["id"] = HavelValue(mapping->id);
            (*mappingObj)["name"] = HavelValue(mapping->name);
            (*mappingObj)["enabled"] = HavelValue(mapping->enabled);
            (*mappingObj)["sourceKey"] = HavelValue(mapping->sourceKey);
            (*mappingObj)["targetKeys"] = HavelValue([&]() -> HavelValue {
              auto targetArray = std::make_shared<std::vector<HavelValue>>();
              for (const auto &key : mapping->targetKeys) {
                targetArray->push_back(HavelValue(key));
              }
              return HavelValue(targetArray);
            }());
            (*mappingObj)["actionType"] = HavelValue(
                static_cast<double>(static_cast<int>(mapping->actionType)));
            (*mappingObj)["autofire"] = HavelValue(mapping->autofire);
            (*mappingObj)["turbo"] = HavelValue(mapping->turbo);
            mappingsArray->push_back(HavelValue(mappingObj));
          }
        }

        return HavelValue(mappingsArray);
      }));

  (*mapManagerObj)["findMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.findMapping() requires (profileId, sourceKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        auto mapping = coreMapManager->GetMapping(profileId, sourceKey);
        if (!mapping) {
          return HavelValue(nullptr);
        }

        auto mappingObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*mappingObj)["id"] = HavelValue(mapping->id);
        (*mappingObj)["name"] = HavelValue(mapping->name);
        (*mappingObj)["enabled"] = HavelValue(mapping->enabled);
        (*mappingObj)["sourceKey"] = HavelValue(mapping->sourceKey);
        (*mappingObj)["targetKeys"] = HavelValue([&]() -> HavelValue {
          auto targetArray = std::make_shared<std::vector<HavelValue>>();
          for (const auto &key : mapping->targetKeys) {
            targetArray->push_back(HavelValue(key));
          }
          return HavelValue(targetArray);
        }());
        (*mappingObj)["actionType"] = HavelValue(
            static_cast<double>(static_cast<int>(mapping->actionType)));
        (*mappingObj)["autofire"] = HavelValue(mapping->autofire);
        (*mappingObj)["turbo"] = HavelValue(mapping->turbo);

        return HavelValue(mappingObj);
      }));

  (*mapmanagerObj)["filterMappings"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.filterMappings() requires "
                                   "(profileId, filterField, filterValue)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string filterField =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string filterValue =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        auto mappings = coreMapManager->GetMappings(profileId);
        auto filteredArray = std::make_shared<std::vector<HavelValue>>();

        for (const auto *mapping : mappings) {
          if (!mapping)
            continue;

          bool matches = false;
          if (filterField == "enabled") {
            matches = (filterValue == "true" && mapping->enabled) ||
                      (filterValue == "false" && !mapping->enabled);
          } else if (filterField == "sourceKey") {
            matches = mapping->sourceKey.find(filterValue) != std::string::npos;
          } else if (filterField == "actionType") {
            matches =
                static_cast<int>(mapping->actionType) == std::stoi(filterValue);
          } else if (filterField == "autofire") {
            matches = (filterValue == "true" && mapping->autofire) ||
                      (filterValue == "false" && !mapping->autofire);
          } else if (filterField == "turbo") {
            matches = (filterValue == "true" && mapping->turbo) ||
                      (filterValue == "false" && !mapping->turbo);
          }

          if (matches) {
            auto mappingObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*mappingObj)["id"] = HavelValue(mapping->id);
            (*mappingObj)["name"] = HavelValue(mapping->name);
            (*mappingObj)["enabled"] = HavelValue(mapping->enabled);
            (*mappingObj)["sourceKey"] = HavelValue(mapping->sourceKey);
            (*mappingObj)["targetKeys"] = HavelValue([&]() -> HavelValue {
              auto targetArray = std::make_shared<std::vector<HavelValue>>();
              for (const auto &key : mapping->targetKeys) {
                targetArray->push_back(HavelValue(key));
              }
              return HavelValue(targetArray);
            }());
            (*mappingObj)["actionType"] = HavelValue(
                static_cast<double>(static_cast<int>(mapping->actionType)));
            (*mappingObj)["autofire"] = HavelValue(mapping->autofire);
            (*mappingObj)["turbo"] = HavelValue(mapping->turbo);
            filteredArray->push_back(HavelValue(mappingObj));
          }
        }

        return HavelValue(filteredArray);
      }));

  // =========================================================================
  // Conditional Hotkeys and Combo Hotkeys Support
  // =========================================================================

  (*mapManagerObj)["addConditionalMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 4) {
          return HavelRuntimeError(
              "mapmanager.addConditionalMapping() requires (profileId, "
              "sourceKey, targetKey, conditionType, conditionPattern)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));
        std::string conditionTypeStr =
            args[3].isString()
                ? args[3].asString()
                : std::to_string(static_cast<int>(args[3].asNumber()));

        Mapping mapping;
        mapping.id = sourceKey + "_conditional_to_" + targetKey;
        mapping.name = sourceKey + " -> " + targetKey + " (conditional)";
        mapping.sourceKey = sourceKey;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;

        // Parse condition type
        ConditionType conditionType = ConditionType::Always;
        if (conditionTypeStr == "window") {
          conditionType = ConditionType::WindowTitle;
        } else if (conditionTypeStr == "process") {
          conditionType = ConditionType::ProcessName;
        } else if (conditionTypeStr == "custom") {
          conditionType = ConditionType::Custom;
        }

        // Add condition (for window/process types, we need a pattern)
        if (conditionType != ConditionType::Always &&
            conditionType != ConditionType::Custom) {
          // For demo, use window title pattern as condition
          MappingCondition condition;
          condition.type = conditionType;
          condition.pattern = "*"; // Match all windows for demo
          mapping.conditions.push_back(condition);
        }

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addComboMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.addComboMapping() requires "
                                   "(profileId, comboKeys, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string comboKeys =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        Mapping mapping;
        mapping.id = comboKeys + "_combo_to_" + targetKey;
        mapping.name = comboKeys + " -> " + targetKey + " (combo)";
        mapping.sourceKey = comboKeys; // Store combo as source
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::Combo;
        mapping.actionType = ActionType::Press;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addMacroMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.addMacroMapping() requires (profileId, macroName)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string macroName =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        // Create a macro mapping that can be triggered by name
        Mapping mapping;
        mapping.id = macroName;
        mapping.name = "Macro: " + macroName;
        mapping.sourceKey = macroName; // Trigger by macro name
        mapping.type = MappingType::Macro;
        mapping.actionType = ActionType::Macro;

        // Initialize empty macro sequence (user will record later)
        mapping.macroSequence = {};

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["startMacroRecording"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 1) {
          return HavelRuntimeError(
              "mapmanager.startMacroRecording() requires macroName");
        }

        std::string macroName =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        coreMapManager->StartMacroRecording(macroName);
        return HavelValue(true);
      }));

  (*mapManagerObj)["stopMacroRecording"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        coreMapManager->StopMacroRecording();
        return HavelValue(true);
      }));

  (*mapManagerObj)["saveMacro"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.saveMacro() requires (profileId, mappingId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        coreMapManager->SaveMacro(profileId, mappingId);
        return HavelValue(true);
      }));

  // =========================================================================
  // JoyToKey-like Features
  // =========================================================================

  (*mapManagerObj)["addMultiSourceMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError(
              "mapmanager.addMultiSourceMapping() requires (profileId, "
              "sourceKeysArray, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        // Extract source keys array
        std::vector<std::string> sourceKeys;
        if (args[1].is<std::shared_ptr<std::vector<HavelValue>>>()) {
          auto sourceArray =
              args[1].as<std::shared_ptr<std::vector<HavelValue>>>();
          for (const auto &source : *sourceArray) {
            if (source.isString()) {
              sourceKeys.push_back(source.asString());
            }
          }
        }

        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        Mapping mapping;
        mapping.id =
            "multi_" + std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name = "Multi-Source Mapping";
        mapping.sourceKeys = sourceKeys;
        mapping.sourceKey =
            sourceKeys.empty() ? "" : sourceKeys[0]; // Backward compatibility
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addToggleMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.addToggleMapping() requires "
                                   "(profileId, sourceKey, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        Mapping mapping;
        mapping.id = "toggle_" + sourceKey + "_to_" + targetKey;
        mapping.name = sourceKey + " -> " + targetKey + " (toggle)";
        mapping.sourceKey = sourceKey;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Toggle;
        mapping.toggleMode = true;
        mapping.toggleState = false;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addScriptCallbackMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError(
              "mapmanager.addScriptCallbackMapping() requires (profileId, "
              "sourceKey, scriptCode)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string sourceKey =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string scriptCode =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        Mapping mapping;
        mapping.id = "script_" + sourceKey + "_" +
                     std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name = sourceKey + " -> Script Callback";
        mapping.sourceKey = sourceKey;
        mapping.type = MappingType::Macro;
        mapping.actionType = ActionType::Macro;
        mapping.scriptCallback = scriptCode;
        mapping.useScriptCallback = true;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["setToggleState"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.setToggleState() requires "
                                   "(profileId, mappingId, toggleState)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        bool toggleState =
            args[2].isBool() ? args[2].asBool() : (args[2].asNumber() != 0);

        auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
        if (mapping) {
          mapping->toggleState = toggleState;
          return HavelValue(true);
        }

        return HavelValue(false);
      }));

  (*mapManagerObj)["getToggleState"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.getToggleState() requires (profileId, mappingId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
        if (mapping) {
          return HavelValue(mapping->toggleState);
        }

        return HavelValue(false);
      }));

  (*mapManagerObj)["evaluateScriptCallback"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError("mapmanager.evaluateScriptCallback() "
                                   "requires (profileId, mappingId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
        if (mapping && mapping->useScriptCallback &&
            !mapping->scriptCallback.empty()) {
          // In a real implementation, this would evaluate the script
          // For now, return the script code as if it was evaluated
          return HavelValue("Script evaluated: " + mapping->scriptCallback);
        }

        return HavelValue("No script callback found");
      }));

  // =========================================================================
  // Mouse Binding Features
  // =========================================================================

  (*mapManagerObj)["addMouseButtonMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 4) {
          return HavelRuntimeError(
              "mapmanager.addMouseButtonMapping() requires (profileId, "
              "mouseButton, actionType, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        int mouseButton = static_cast<int>(args[1].asNumber());
        std::string actionType =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));
        std::string targetKey =
            args[3].isString()
                ? args[3].asString()
                : std::to_string(static_cast<int>(args[3].asNumber()));

        Mapping mapping;
        mapping.id = "mouse_" + std::to_string(mouseButton) + "_" +
                     std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name =
            "Mouse" + std::to_string(mouseButton) + " -> " + targetKey;
        mapping.sourceKey = "Mouse" + std::to_string(mouseButton);
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;
        mapping.mouseBinding = true;
        mapping.mouseButton = mouseButton;
        mapping.mouseAction = actionType;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addMouseWheelMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 4) {
          return HavelRuntimeError(
              "mapmanager.addMouseWheelMapping() requires (profileId, "
              "direction, actionType, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string direction =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string actionType =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));
        std::string targetKey =
            args[3].isString()
                ? args[3].asString()
                : std::to_string(static_cast<int>(args[3].asNumber()));

        Mapping mapping;
        mapping.id = "wheel_" + direction + "_" +
                     std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name = "MouseWheel" + direction + " -> " + targetKey;
        mapping.sourceKey = "MouseWheel" + direction;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;
        mapping.mouseBinding = true;

        // Convert direction to wheel direction code
        if (direction == "up")
          mapping.mouseWheelDirection = 1;
        else if (direction == "down")
          mapping.mouseWheelDirection = 2;
        else if (direction == "left")
          mapping.mouseWheelDirection = 3;
        else if (direction == "right")
          mapping.mouseWheelDirection = 4;

        mapping.mouseAction = actionType;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addMouseMoveMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 5) {
          return HavelRuntimeError(
              "mapmanager.addMouseMoveMapping() requires (profileId, axis, "
              "sensitivity, threshold, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string axis =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        float sensitivity = static_cast<float>(args[2].asNumber());
        float threshold = static_cast<float>(args[3].asNumber());
        std::string targetKey =
            args[4].isString()
                ? args[4].asString()
                : std::to_string(static_cast<int>(args[4].asNumber()));

        Mapping mapping;
        mapping.id = "move_" + axis + "_" +
                     std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name = "MouseMove" + axis + " -> " + targetKey;
        mapping.sourceKey = "MouseMove" + axis;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::MouseMove;
        mapping.actionType = ActionType::MouseMove;
        mapping.mouseBinding = true;
        mapping.mouseMovement = true;
        mapping.sensitivity = sensitivity;
        mapping.mouseThreshold = threshold;
        mapping.mouseAction = axis;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["addMouseGestureMapping"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 4) {
          return HavelRuntimeError(
              "mapmanager.addMouseGestureMapping() requires (profileId, "
              "gestureName, gesturePoints, targetKey)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string gestureName =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        std::string targetKey =
            args[2].isString()
                ? args[2].asString()
                : std::to_string(static_cast<int>(args[2].asNumber()));

        // Extract gesture points (simplified - in real implementation would be
        // more complex)
        std::vector<std::pair<int, int>> points;
        if (args.size() > 3 &&
            args[3].is<std::shared_ptr<std::vector<HavelValue>>>()) {
          auto pointsArray =
              args[3].as<std::shared_ptr<std::vector<HavelValue>>>();
          for (size_t i = 0; i < pointsArray->size(); i += 2) {
            if (i + 1 < pointsArray->size()) {
              int x = static_cast<int>((*pointsArray)[i].asNumber());
              int y = static_cast<int>((*pointsArray)[i + 1].asNumber());
              points.push_back({x, y});
            }
          }
        }

        Mapping mapping;
        mapping.id = "gesture_" + gestureName + "_" +
                     std::to_string(QDateTime::currentMSecsSinceEpoch());
        mapping.name = "Gesture" + gestureName + " -> " + targetKey;
        mapping.sourceKey = "Gesture" + gestureName;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::Macro;
        mapping.actionType = ActionType::Macro;
        mapping.mouseBinding = true;
        mapping.mouseGesture = true;
        mapping.gesturePoints = points;
        mapping.mouseAction = gestureName;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  (*mapManagerObj)["setMouseSensitivity"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 3) {
          return HavelRuntimeError("mapmanager.setMouseSensitivity() requires "
                                   "(profileId, mappingId, sensitivity)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));
        float sensitivity = static_cast<float>(args[2].asNumber());

        auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
        if (mapping && mapping->mouseBinding) {
          mapping->sensitivity = sensitivity;
          return HavelValue(true);
        }

        return HavelValue(false);
      }));

  (*mapManagerObj)["getMouseBindings"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 1) {
          return HavelRuntimeError(
              "mapmanager.getMouseBindings() requires (profileId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        auto mappings = coreMapManager->GetMappings(profileId);
        auto mouseMappings = std::make_shared<std::vector<HavelValue>>();

        for (auto *mapping : mappings) {
          if (mapping->mouseBinding) {
            auto mappingObj = std::make_shared<HavelObject>();
            (*mappingObj)["id"] = HavelValue(mapping->id);
            (*mappingObj)["name"] = HavelValue(mapping->name);
            (*mappingObj)["sourceKey"] = HavelValue(mapping->sourceKey);
            (*mappingObj)["mouseAction"] = HavelValue(mapping->mouseAction);
            (*mappingObj)["mouseButton"] = HavelValue(mapping->mouseButton);
            (*mappingObj)["mouseWheelDirection"] =
                HavelValue(mapping->mouseWheelDirection);
            (*mappingObj)["sensitivity"] = HavelValue(mapping->sensitivity);
            (*mappingObj)["mouseGesture"] = HavelValue(mapping->mouseGesture);

            if (!mapping->targetKeys.empty()) {
              (*mappingObj)["targetKey"] = HavelValue(mapping->targetKeys[0]);
            }

            mouseMappings->push_back(HavelValue(mappingObj));
          }
        }

        return HavelValue(mouseMappings);
      }));

  // =========================================================================
  // Key Recording Features
  // =========================================================================

  (*mapManagerObj)["startKeyRecording"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        coreMapManager->StartKeyRecording();
        return HavelValue(true);
      }));

  (*mapManagerObj)["stopKeyRecording"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        coreMapManager->StopKeyRecording();
        return HavelValue(true);
      }));

  (*mapManagerObj)["isKeyRecording"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        return HavelValue(coreMapManager->IsKeyRecording());
      }));

  (*mapManagerObj)["getLastRecordedKey"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        auto recordedKey = coreMapManager->GetLastRecordedKey();
        auto keyObj = std::make_shared<HavelObject>();

        (*keyObj)["keyName"] = HavelValue(recordedKey.keyName);
        (*keyObj)["keyCode"] =
            HavelValue(static_cast<double>(recordedKey.keyCode));
        (*keyObj)["source"] = HavelValue(recordedKey.source);
        (*keyObj)["modifiers"] = HavelValue(recordedKey.modifiers);
        (*keyObj)["isMouse"] = HavelValue(recordedKey.isMouse);
        (*keyObj)["isJoystick"] = HavelValue(recordedKey.isJoystick);

        return HavelValue(keyObj);
      }));

  (*mapManagerObj)["clearRecordedKey"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }

        coreMapManager->ClearRecordedKey();
        return HavelValue(true);
      }));

  (*mapManagerObj)["recordAndSetKey"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.recordAndSetKey() requires (profileId, mappingId)");
        }

        std::string profileId =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));
        std::string mappingId =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        // Start recording
        coreMapManager->StartKeyRecording();

        // In a real implementation, this would wait for user input
        // For now, simulate recording a key
        auto simulatedKey = coreMapManager->RecordCurrentInput();
        coreMapManager->StopKeyRecording();

        // Update the mapping with the recorded key
        auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
        if (mapping) {
          mapping->sourceKey = simulatedKey.keyName;
          mapping->sourceCode = simulatedKey.keyCode;

          // If it's a mouse binding, update mouse properties
          if (simulatedKey.isMouse) {
            mapping->mouseBinding = true;
            if (simulatedKey.keyName.find("Mouse") == 0) {
              int buttonNum = 0;
              sscanf(simulatedKey.keyName.c_str(), "Mouse%d", &buttonNum);
              mapping->mouseButton = buttonNum;
            }
          }

          return HavelValue("Recorded and set: " + simulatedKey.keyName);
        }

        return HavelValue("Failed to update mapping");
      }));

  (*mapManagerObj)["detectInputSource"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 1) {
          return HavelRuntimeError(
              "mapmanager.detectInputSource() requires (keyCode)");
        }

        int keyCode = static_cast<int>(args[0].asNumber());
        std::string source = coreMapManager->DetectInputSource(keyCode);

        return HavelValue(source);
      }));

  (*mapManagerObj)["keyCodeToString"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager) {
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        }
        if (args.size() < 2) {
          return HavelRuntimeError(
              "mapmanager.keyCodeToString() requires (keyCode, source)");
        }

        int keyCode = static_cast<int>(args[0].asNumber());
        std::string source =
            args[1].isString()
                ? args[1].asString()
                : std::to_string(static_cast<int>(args[1].asNumber()));

        std::string keyString =
            coreMapManager->KeyCodeToString(keyCode, source);
        return HavelValue(keyString);
      }));

  // Register mapmanager module
  env.Define("mapmanager", HavelValue(mapManagerObj));
}

} // namespace havel::modules
