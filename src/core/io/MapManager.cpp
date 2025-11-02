#include "MapManager.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

using json = nlohmann::json;
using namespace spdlog;

namespace havel {

// ============================================================================
// MappingCondition Implementation
// ============================================================================

bool MappingCondition::Evaluate() const {
    switch (type) {
        case ConditionType::Always:
            return true;
            
        case ConditionType::Custom:
            return customCheck ? customCheck() : true;
            
#ifdef __linux__
        case ConditionType::WindowTitle:
        case ConditionType::WindowClass: {
            Display* display = XOpenDisplay(nullptr);
            if (!display) return false;
            
            Window focused;
            int revert;
            XGetInputFocus(display, &focused, &revert);
            
            if (focused == None) {
                XCloseDisplay(display);
                return false;
            }
            
            std::string value;
            if (type == ConditionType::WindowTitle) {
                char* name = nullptr;
                XFetchName(display, focused, &name);
                if (name) {
                    value = name;
                    XFree(name);
                }
            } else {
                XClassHint classHint;
                if (XGetClassHint(display, focused, &classHint)) {
                    value = classHint.res_class ? classHint.res_class : "";
                    if (classHint.res_name) XFree(classHint.res_name);
                    if (classHint.res_class) XFree(classHint.res_class);
                }
            }
            
            XCloseDisplay(display);
            
            try {
                std::regex regex(pattern, std::regex::icase);
                return std::regex_search(value, regex);
            } catch (...) {
                return value.find(pattern) != std::string::npos;
            }
        }
        
        case ConditionType::ProcessName: {
            // Check if process is running by reading /proc
            std::ifstream cmdline("/proc/self/cmdline");
            std::string cmd;
            std::getline(cmdline, cmd, '\0');
            return cmd.find(pattern) != std::string::npos;
        }
#endif
        
        default:
            return true;
    }
}

// ============================================================================
// Mapping Implementation
// ============================================================================

bool Mapping::ShouldActivate() const {
    if (!enabled) return false;
    
    if (conditions.empty()) return true;
    
    // All conditions must be satisfied
    for (const auto& condition : conditions) {
        if (!condition.Evaluate()) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Profile Implementation
// ============================================================================

Mapping* Profile::FindMapping(const std::string& sourceKey) {
    auto it = std::find_if(mappings.begin(), mappings.end(),
        [&](const Mapping& m) { return m.sourceKey == sourceKey; });
    return it != mappings.end() ? &(*it) : nullptr;
}

const Mapping* Profile::FindMapping(const std::string& sourceKey) const {
    auto it = std::find_if(mappings.begin(), mappings.end(),
        [&](const Mapping& m) { return m.sourceKey == sourceKey; });
    return it != mappings.end() ? &(*it) : nullptr;
}

// ============================================================================
// MapManager Implementation
// ============================================================================

MapManager::MapManager(IO* io) : io(io) {
    info("MapManager initialized");
}

MapManager::~MapManager() {
    ClearAllMappings();
}

void MapManager::AddProfile(const Profile& profile) {
    std::lock_guard<std::mutex> lock(profileMutex);
    profiles[profile.id] = profile;
    info("Added profile: {} ({})", profile.name, profile.id);
}

void MapManager::RemoveProfile(const std::string& profileId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    // Unregister all mappings first
    if (profileId == activeProfileId) {
        ClearAllMappings();
    }
    
    profiles.erase(profileId);
    stats.erase(profileId);
    info("Removed profile: {}", profileId);
}

Profile* MapManager::GetProfile(const std::string& profileId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    auto it = profiles.find(profileId);
    return it != profiles.end() ? &it->second : nullptr;
}

const Profile* MapManager::GetProfile(const std::string& profileId) const {
    std::lock_guard<std::mutex> lock(profileMutex);
    auto it = profiles.find(profileId);
    return it != profiles.end() ? &it->second : nullptr;
}

std::vector<std::string> MapManager::GetProfileIds() const {
    std::lock_guard<std::mutex> lock(profileMutex);
    std::vector<std::string> ids;
    for (const auto& [id, _] : profiles) {
        ids.push_back(id);
    }
    return ids;
}

void MapManager::SetActiveProfile(const std::string& profileId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    if (profiles.find(profileId) == profiles.end()) {
        error("Profile not found: {}", profileId);
        return;
    }
    
    // Clear current profile mappings
    if (!activeProfileId.empty()) {
        ClearAllMappings();
    }
    
    activeProfileId = profileId;
    info("Activated profile: {}", profileId);
    
    // Apply new profile
    ApplyProfile(profileId);
}

Profile* MapManager::GetActiveProfile() {
    return GetProfile(activeProfileId);
}

void MapManager::AddMapping(const std::string& profileId, const Mapping& mapping) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto it = profiles.find(profileId);
    if (it == profiles.end()) {
        error("Profile not found: {}", profileId);
        return;
    }
    
    it->second.mappings.push_back(mapping);
    
    // Register if this is the active profile
    if (profileId == activeProfileId) {
        RegisterMapping(profileId, it->second.mappings.back());
    }
    
    info("Added mapping '{}' to profile '{}'", mapping.name, profileId);
}

void MapManager::RemoveMapping(const std::string& profileId, const std::string& mappingId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto profileIt = profiles.find(profileId);
    if (profileIt == profiles.end()) return;
    
    auto& mappings = profileIt->second.mappings;
    auto it = std::find_if(mappings.begin(), mappings.end(),
        [&](const Mapping& m) { return m.id == mappingId; });
    
    if (it != mappings.end()) {
        if (profileId == activeProfileId) {
            UnregisterMapping(profileId, mappingId);
        }
        mappings.erase(it);
        info("Removed mapping: {}", mappingId);
    }
}

void MapManager::UpdateMapping(const std::string& profileId, const Mapping& mapping) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto profileIt = profiles.find(profileId);
    if (profileIt == profiles.end()) return;
    
    auto& mappings = profileIt->second.mappings;
    auto it = std::find_if(mappings.begin(), mappings.end(),
        [&](const Mapping& m) { return m.id == mapping.id; });
    
    if (it != mappings.end()) {
        // Unregister old
        if (profileId == activeProfileId) {
            UnregisterMapping(profileId, mapping.id);
        }
        
        // Update
        *it = mapping;
        
        // Re-register
        if (profileId == activeProfileId) {
            RegisterMapping(profileId, *it);
        }
        
        info("Updated mapping: {}", mapping.id);
    }
}

Mapping* MapManager::GetMapping(const std::string& profileId, const std::string& mappingId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto profileIt = profiles.find(profileId);
    if (profileIt == profiles.end()) return nullptr;
    
    auto& mappings = profileIt->second.mappings;
    auto it = std::find_if(mappings.begin(), mappings.end(),
        [&](const Mapping& m) { return m.id == mappingId; });
    
    return it != mappings.end() ? &(*it) : nullptr;
}

std::vector<Mapping*> MapManager::GetMappings(const std::string& profileId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    std::vector<Mapping*> result;
    auto profileIt = profiles.find(profileId);
    if (profileIt != profiles.end()) {
        for (auto& mapping : profileIt->second.mappings) {
            result.push_back(&mapping);
        }
    }
    return result;
}

void MapManager::EnableProfile(const std::string& profileId, bool enable) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto it = profiles.find(profileId);
    if (it != profiles.end()) {
        it->second.enabled = enable;
        
        if (profileId == activeProfileId) {
            if (enable) {
                ApplyProfile(profileId);
            } else {
                ClearAllMappings();
            }
        }
    }
}

void MapManager::EnableMapping(const std::string& profileId, const std::string& mappingId, bool enable) {
    auto mapping = GetMapping(profileId, mappingId);
    if (mapping) {
        mapping->enabled = enable;
        
        if (profileId == activeProfileId) {
            if (enable) {
                RegisterMapping(profileId, *mapping);
            } else {
                UnregisterMapping(profileId, mappingId);
            }
        }
    }
}

void MapManager::ApplyProfile(const std::string& profileId) {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    auto it = profiles.find(profileId);
    if (it == profiles.end() || !it->second.enabled) return;
    
    info("Applying profile: {}", it->second.name);
    
    for (auto& mapping : it->second.mappings) {
        if (mapping.enabled) {
            RegisterMapping(profileId, mapping);
        }
    }
}

void MapManager::ApplyActiveProfile() {
    if (!activeProfileId.empty()) {
        ApplyProfile(activeProfileId);
    }
}

void MapManager::ClearAllMappings() {
    // Remove all registered hotkeys
    for (auto& [profileId, hotkeyIds] : profileHotkeyIds) {
        for (int id : hotkeyIds) {
            io->RemoveHotkey(id);
        }
    }
    profileHotkeyIds.clear();
}

void MapManager::NextProfile() {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    if (profiles.empty()) return;
    
    auto ids = GetProfileIds();
    auto it = std::find(ids.begin(), ids.end(), activeProfileId);
    
    if (it == ids.end() || ++it == ids.end()) {
        SetActiveProfile(ids.front());
    } else {
        SetActiveProfile(*it);
    }
}

void MapManager::PreviousProfile() {
    std::lock_guard<std::mutex> lock(profileMutex);
    
    if (profiles.empty()) return;
    
    auto ids = GetProfileIds();
    auto it = std::find(ids.begin(), ids.end(), activeProfileId);
    
    if (it == ids.end() || it == ids.begin()) {
        SetActiveProfile(ids.back());
    } else {
        SetActiveProfile(*(--it));
    }
}

void MapManager::SetProfileSwitchHotkey(const std::string& hotkey) {
    if (profileSwitchHotkeyId != -1) {
        io->RemoveHotkey(profileSwitchHotkeyId);
    }
    
    profileSwitchHotkeyId = io->Hotkey(hotkey, [this]() {
        NextProfile();
        auto profile = GetActiveProfile();
        if (profile) {
            info("Switched to profile: {}", profile->name);
        }
    });
}

void MapManager::StartMacroRecording(const std::string& macroName) {
    macroRecording = true;
    currentMacroName = macroName;
    recordedMacro.clear();
    lastMacroEvent = std::chrono::steady_clock::now();
    info("Started macro recording: {}", macroName);
}

void MapManager::StopMacroRecording() {
    macroRecording = false;
    info("Stopped macro recording: {} ({} events)", currentMacroName, recordedMacro.size());
}

void MapManager::SaveMacro(const std::string& profileId, const std::string& mappingId) {
    auto mapping = GetMapping(profileId, mappingId);
    if (mapping) {
        mapping->macroSequence = recordedMacro;
        info("Saved macro to mapping: {}", mappingId);
    }
}

void MapManager::RecordMacroEvent(const std::string& key) {
    if (!macroRecording) return;
    
    auto now = std::chrono::steady_clock::now();
    int delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastMacroEvent).count();
    
    recordedMacro.push_back({key, delay});
    lastMacroEvent = now;
}

// ============================================================================
// Internal Helpers
// ============================================================================

void MapManager::RegisterMapping(const std::string& profileId, Mapping& mapping) {
    if (!mapping.ShouldActivate()) return;
    
    std::string hotkeyStr = "@" + mapping.sourceKey;
    
    int hotkeyId = io->Hotkey(hotkeyStr, [this, &mapping, profileId]() {
        ExecuteMapping(mapping, true);
        
        // Update stats
        auto& stat = stats[profileId][mapping.id];
        stat.activationCount++;
        stat.lastActivation = std::chrono::steady_clock::now();
    });
    
    if (hotkeyId >= 0) {
        profileHotkeyIds[profileId].push_back(hotkeyId);
        debug("Registered mapping: {} -> {}", mapping.sourceKey, 
              mapping.targetKeys.empty() ? "action" : mapping.targetKeys[0]);
    }
}

void MapManager::UnregisterMapping(const std::string& profileId, const std::string& mappingId) {
    // Find and remove hotkey IDs for this mapping
    // Note: This is simplified - in production you'd track per-mapping hotkey IDs
    auto it = profileHotkeyIds.find(profileId);
    if (it != profileHotkeyIds.end()) {
        for (int id : it->second) {
            io->RemoveHotkey(id);
        }
        it->second.clear();
    }
}

void MapManager::ExecuteMapping(Mapping& mapping, bool down) {
    mapping.active = down;
    
    switch (mapping.actionType) {
        case ActionType::Press:
            if (down && !mapping.targetKeys.empty()) {
                io->Send(mapping.targetKeys[0]);
            }
            break;
            
        case ActionType::Hold:
            for (const auto& key : mapping.targetKeys) {
                if (down) {
                    io->KeyDown(key);
                } else {
                    io->KeyUp(key);
                }
            }
            break;
            
        case ActionType::Toggle:
            if (down) {
                mapping.toggleState = !mapping.toggleState;
                for (const auto& key : mapping.targetKeys) {
                    if (mapping.toggleState) {
                        io->KeyDown(key);
                    } else {
                        io->KeyUp(key);
                    }
                }
            }
            break;
            
        case ActionType::Autofire:
        case ActionType::Turbo:
            if (down) {
                ExecuteAutofire(mapping);
            }
            break;
            
        case ActionType::Macro:
            if (down) {
                ExecuteMacro(mapping);
            }
            break;
            
        default:
            break;
    }
    
    // Record for macro if recording
    if (macroRecording && down) {
        RecordMacroEvent(mapping.sourceKey);
    }
}

void MapManager::ExecuteAutofire(Mapping& mapping) {
    auto now = std::chrono::steady_clock::now();
    int interval = mapping.turbo ? mapping.turboInterval : mapping.autofireInterval;
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - mapping.lastFireTime).count();
    
    if (elapsed >= interval) {
        for (const auto& key : mapping.targetKeys) {
            io->Send(key);
        }
        mapping.lastFireTime = now;
    }
}

void MapManager::ExecuteMacro(const Mapping& mapping) {
    std::thread([this, mapping]() {
        for (const auto& [key, delay] : mapping.macroSequence) {
            io->Send(key);
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
    }).detach();
}

void MapManager::ExecuteMouseMovement(const Mapping& mapping, float axisValue) {
    // Apply deadzone
    if (std::abs(axisValue) < mapping.deadzone) {
        return;
    }
    
    // Apply sensitivity
    float movement = axisValue * mapping.sensitivity;
    
    // Apply acceleration if enabled
    if (mapping.acceleration) {
        movement *= std::abs(axisValue); // Quadratic acceleration
    }
    
    // Move mouse (this would need IO support for relative mouse movement)
    // io->MoveMouse(static_cast<int>(movement), 0);
}

std::string MapManager::GenerateId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 16; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

// ============================================================================
// Save/Load Implementation
// ============================================================================

void MapManager::SaveProfiles(const std::string& filepath) {
    json j = json::array();
    
    std::lock_guard<std::mutex> lock(profileMutex);
    for (const auto& [id, profile] : profiles) {
        j.push_back(ExportProfileToJson(id));
    }
    
    std::ofstream file(filepath);
    file << j.dump(2);
    info("Saved {} profiles to {}", profiles.size(), filepath);
}

void MapManager::LoadProfiles(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        error("Failed to open profiles file: {}", filepath);
        return;
    }
    
    json j;
    file >> j;
    
    for (const auto& profileJson : j) {
        ImportProfileFromJson(profileJson.dump());
    }
    
    info("Loaded profiles from {}", filepath);
}

std::string MapManager::ExportProfileToJson(const std::string& profileId) const {
    auto profile = GetProfile(profileId);
    if (!profile) return "{}";
    
    json j;
    j["id"] = profile->id;
    j["name"] = profile->name;
    j["description"] = profile->description;
    j["enabled"] = profile->enabled;
    
    j["mappings"] = json::array();
    for (const auto& mapping : profile->mappings) {
        json m;
        m["id"] = mapping.id;
        m["name"] = mapping.name;
        m["enabled"] = mapping.enabled;
        m["sourceKey"] = mapping.sourceKey;
        m["actionType"] = static_cast<int>(mapping.actionType);
        m["targetKeys"] = mapping.targetKeys;
        m["autofire"] = mapping.autofire;
        m["autofireInterval"] = mapping.autofireInterval;
        m["turbo"] = mapping.turbo;
        m["toggleMode"] = mapping.toggleMode;
        
        j["mappings"].push_back(m);
    }
    
    return j.dump(2);
}

void MapManager::ImportProfileFromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        Profile profile;
        profile.id = j["id"];
        profile.name = j["name"];
        profile.description = j.value("description", "");
        profile.enabled = j.value("enabled", true);
        
        for (const auto& m : j["mappings"]) {
            Mapping mapping;
            mapping.id = m["id"];
            mapping.name = m["name"];
            mapping.enabled = m.value("enabled", true);
            mapping.sourceKey = m["sourceKey"];
            mapping.actionType = static_cast<ActionType>(m["actionType"].get<int>());
            mapping.targetKeys = m["targetKeys"].get<std::vector<std::string>>();
            mapping.autofire = m.value("autofire", false);
            mapping.autofireInterval = m.value("autofireInterval", 100);
            mapping.turbo = m.value("turbo", false);
            mapping.toggleMode = m.value("toggleMode", false);
            
            profile.mappings.push_back(mapping);
        }
        
        AddProfile(profile);
    } catch (const std::exception& e) {
        error("Failed to import profile: {}", e.what());
    }
}

const MapManager::MappingStats* MapManager::GetMappingStats(
    const std::string& profileId, const std::string& mappingId) const {
    
    auto profileIt = stats.find(profileId);
    if (profileIt == stats.end()) return nullptr;
    
    auto mappingIt = profileIt->second.find(mappingId);
    if (mappingIt == profileIt->second.end()) return nullptr;
    
    return &mappingIt->second;
}

void MapManager::ResetStats() {
    stats.clear();
}

} // namespace havel
