#include "MapManager.hpp"
#include "../DisplayManager.hpp"
#include "EventListener.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include "utils/Logger.hpp"
#include <sstream>
#include <thread>

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

using json = nlohmann::json;

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
    Display *display = havel::DisplayManager::GetDisplay();
    if (!display)
      return false;

    Window focused;
    int revert;
    XGetInputFocus(display, &focused, &revert);

    if (focused == 0) { // X11 None constant
      XCloseDisplay(display);
      return false;
    }

    std::string value;
    if (type == ConditionType::WindowTitle) {
      char *name = nullptr;
      XFetchName(display, focused, &name);
      if (name) {
        value = name;
        XFree(name);
      }
    } else {
      XClassHint classHint;
      if (XGetClassHint(display, focused, &classHint)) {
        value = classHint.res_class ? classHint.res_class : "";
        if (classHint.res_name)
          XFree(classHint.res_name);
        if (classHint.res_class)
          XFree(classHint.res_class);
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
  if (!enabled)
    return false;

  if (conditions.empty())
    return true;

  // All conditions must be satisfied
  for (const auto &condition : conditions) {
    if (!condition.Evaluate()) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// Profile Implementation
// ============================================================================

Mapping *Profile::FindMapping(const std::string &sourceKey) {
  auto it =
      std::find_if(mappings.begin(), mappings.end(),
                   [&](const Mapping &m) { return m.sourceKey == sourceKey; });
  return it != mappings.end() ? &(*it) : nullptr;
}

const Mapping *Profile::FindMapping(const std::string &sourceKey) const {
  auto it =
      std::find_if(mappings.begin(), mappings.end(),
                   [&](const Mapping &m) { return m.sourceKey == sourceKey; });
  return it != mappings.end() ? &(*it) : nullptr;
}

// ============================================================================
// MapManager Implementation
// ============================================================================

MapManager::MapManager(IO *io) : io(io) { havel::info("MapManager initialized"); }

MapManager::~MapManager() { ClearAllMappings(); }

void MapManager::AddProfile(const Profile &profile) {
  std::lock_guard<std::mutex> lock(profileMutex);
  profiles[profile.id] = profile;
  havel::info("Added profile: {} ({})", profile.name, profile.id);
}

void MapManager::RemoveProfile(const std::string &profileId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  // Unregister all mappings first
  if (profileId == activeProfileId) {
    ClearAllMappings();
  }

  profiles.erase(profileId);
  stats.erase(profileId);
  havel::info("Removed profile: {}", profileId);
}

Profile *MapManager::GetProfile(const std::string &profileId) {
  std::lock_guard<std::mutex> lock(profileMutex);
  auto it = profiles.find(profileId);
  return it != profiles.end() ? &it->second : nullptr;
}

const Profile *MapManager::GetProfile(const std::string &profileId) const {
  std::lock_guard<std::mutex> lock(profileMutex);
  auto it = profiles.find(profileId);
  return it != profiles.end() ? &it->second : nullptr;
}

std::vector<std::string> MapManager::GetProfileIds() const {
  std::lock_guard<std::mutex> lock(profileMutex);
  std::vector<std::string> ids;
  for (const auto &[id, _] : profiles) {
    ids.push_back(id);
  }
  return ids;
}

void MapManager::SetActiveProfile(const std::string &profileId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  if (profiles.find(profileId) == profiles.end()) {
    havel::error("Profile not found: {}", profileId);
    return;
  }

  // Clear current profile mappings
  if (!activeProfileId.empty()) {
    ClearAllMappings();
  }

  activeProfileId = profileId;
  havel::info("Activated profile: {}", profileId);

  // Apply new profile
  ApplyProfile(profileId);
}

Profile *MapManager::GetActiveProfile() { return GetProfile(activeProfileId); }

void MapManager::AddMapping(const std::string &profileId,
                            const Mapping &mapping) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto it = profiles.find(profileId);
  if (it == profiles.end()) {
    havel::error("Profile not found: {}", profileId);
    return;
  }

  it->second.mappings.push_back(mapping);

  // Register if this is the active profile
  if (profileId == activeProfileId) {
    RegisterMapping(profileId, it->second.mappings.back());
  }

  havel::info("Added mapping '{}' to profile '{}'", mapping.name, profileId);
}

void MapManager::RemoveMapping(const std::string &profileId,
                               const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end())
    return;

  auto &profile = profileIt->second;
  auto mappingIt = std::find_if(
      profile.mappings.begin(), profile.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });

  if (mappingIt != profile.mappings.end()) {
    if (profileId == activeProfileId) {
      UnregisterMapping(profileId, *mappingIt);
    }
    profile.mappings.erase(mappingIt);
    havel::info("Removed mapping: {}", mappingId);
  }
}

void MapManager::UpdateMapping(const std::string &profileId,
                               const Mapping &mapping) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end())
    return;

  auto &profile = profileIt->second;
  auto mappingIt =
      std::find_if(profile.mappings.begin(), profile.mappings.end(),
                   [&mapping](const Mapping &m) { return m.id == mapping.id; });

  if (mappingIt != profile.mappings.end()) {
    // Unregister old
    if (profileId == activeProfileId) {
      UnregisterMapping(profileId, *mappingIt);
    }

    // Update
    *mappingIt = mapping;

    // Re-register
    if (profileId == activeProfileId) {
      RegisterMapping(profileId, *mappingIt);
    }

    havel::info("Updated mapping: {}", mapping.id);
  }
}

Mapping *MapManager::GetMapping(const std::string &profileId,
                                const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end())
    return nullptr;

  auto &profile = profileIt->second;
  auto mappingIt = std::find_if(
      profile.mappings.begin(), profile.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });

  return mappingIt != profile.mappings.end() ? &(*mappingIt) : nullptr;
}

std::vector<Mapping *> MapManager::GetMappings(const std::string &profileId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  std::vector<Mapping *> result;
  auto profileIt = profiles.find(profileId);
  if (profileIt != profiles.end()) {
    for (auto &mapping : profileIt->second.mappings) {
      result.push_back(&mapping);
    }
  }
  return result;
}

void MapManager::EnableProfile(const std::string &profileId, bool enable) {
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

void MapManager::EnableMapping(const std::string &profileId,
                               const std::string &mappingId, bool enable) {
  auto mapping = GetMapping(profileId, mappingId);
  if (mapping) {
    mapping->enabled = enable;

    if (profileId == activeProfileId) {
      if (enable) {
        RegisterMapping(profileId, *mapping);
      } else {
        UnregisterMapping(profileId, *mapping);
      }
    }
  }
}

void MapManager::ApplyProfile(const std::string &profileId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto it = profiles.find(profileId);
  if (it == profiles.end() || !it->second.enabled)
    return;

  havel::info("Applying profile: {}", it->second.name);

  for (auto &mapping : it->second.mappings) {
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
  for (auto &[profileId, hotkeyIds] : profileHotkeyIds) {
    for (int id : hotkeyIds) {
      io->UngrabHotkey(id);
    }
  }
  profileHotkeyIds.clear();
}

void MapManager::NextProfile() {
  std::lock_guard<std::mutex> lock(profileMutex);

  if (profiles.empty())
    return;

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

  if (profiles.empty())
    return;

  auto ids = GetProfileIds();
  auto it = std::find(ids.begin(), ids.end(), activeProfileId);

  if (it == ids.end() || it == ids.begin()) {
    SetActiveProfile(ids.back());
  } else {
    SetActiveProfile(*(--it));
  }
}

void MapManager::SetProfileSwitchHotkey(const std::string &hotkey) {
  if (profileSwitchHotkeyId != -1) {
    io->UngrabHotkey(profileSwitchHotkeyId);
  }

  profileSwitchHotkeyId = io->Hotkey(hotkey, [this]() {
    NextProfile();
    auto profile = GetActiveProfile();
    if (profile) {
      havel::info("Switched to profile: {}", profile->name);
    }
  });
}

void MapManager::StartMacroRecording(const std::string &macroName) {
  macroRecording = true;
  currentMacroName = macroName;
  recordedMacro.clear();
  lastMacroEvent = std::chrono::steady_clock::now();
  havel::info("Started macro recording: {}", macroName);
}

void MapManager::StopMacroRecording() {
  macroRecording = false;
  havel::info("Stopped macro recording: {} ({} events)", currentMacroName,
       recordedMacro.size());
}

void MapManager::SaveMacro(const std::string &profileId,
                           const std::string &mappingId) {
  auto mapping = GetMapping(profileId, mappingId);
  if (mapping) {
    mapping->macroSequence = recordedMacro;
    havel::info("Saved macro to mapping: {}", mappingId);
  }
}

void MapManager::RecordMacroEvent(const std::string &key) {
  if (!macroRecording)
    return;

  auto now = std::chrono::steady_clock::now();
  int delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - lastMacroEvent)
                  .count();

  recordedMacro.push_back({key, delay});
  lastMacroEvent = now;
}

// ============================================================================
// Internal Helpers
// ============================================================================

void MapManager::RegisterMapping(const std::string &profileId,
                                 Mapping &mapping) {
  if (!mapping.ShouldActivate())
    return;

  std::string hotkeyStr = "@" + mapping.sourceKey;

  int hotkeyId = io->Hotkey(hotkeyStr, [this, &mapping, profileId]() {
    ExecuteMapping(mapping, true);

    // Update stats
    auto &stat = stats[profileId][mapping.id];
    stat.activationCount++;
    stat.lastActivation = std::chrono::steady_clock::now();
  });

  if (hotkeyId >= 0) {
    profileHotkeyIds[profileId].push_back(hotkeyId);
    havel::debug("Registered mapping: {} -> {}", mapping.sourceKey,
          mapping.targetKeys.empty() ? "action" : mapping.targetKeys[0]);
  }
}

void MapManager::UnregisterMapping(const std::string &profileId,
                                   const Mapping &mapping) {
  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto &profile = profileIt->second;
  auto mappingIt =
      std::find_if(profile.mappings.begin(), profile.mappings.end(),
                   [&mapping](const Mapping &m) { return m.id == mapping.id; });

  if (mappingIt != profile.mappings.end()) {
    // Find and remove the hotkey ID for this mapping
    auto &hotkeyIds = profileHotkeyIds[profileId];
    auto hotkeyIt = std::find_if(hotkeyIds.begin(), hotkeyIds.end(),
                                 [&mapping](int hotkeyId) {
                                   // This would need to be implemented to track
                                   // hotkey IDs per mapping
                                   return false; // Placeholder
                                 });
    if (hotkeyIt != hotkeyIds.end()) {
      io->UngrabHotkey(*hotkeyIt);
      hotkeyIds.erase(hotkeyIt);
    }
  }
}

void MapManager::ExecuteMapping(Mapping &mapping, bool down) {
  mapping.active = down;

  switch (mapping.actionType) {
  case ActionType::Press:
    if (down && !mapping.targetKeys.empty()) {
      io->Send(mapping.targetKeys[0]);
    }
    break;

  case ActionType::Hold:
    for (const auto &key : mapping.targetKeys) {
      if (down) {
        io->SendX11Key(key, true); // KeyDown
      } else {
        io->SendX11Key(key, false); // KeyUp
      }
    }
    break;

  case ActionType::Toggle:
    if (down) {
      mapping.toggleState = !mapping.toggleState;
      for (const auto &key : mapping.targetKeys) {
        if (mapping.toggleState) {
          io->SendX11Key(key, true); // KeyDown
        } else {
          io->SendX11Key(key, false); // KeyUp
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

  case ActionType::AutopressToggle:
    if (down) {
      // Find the profile ID for this mapping
      for (const auto &[profileId, profile] : profiles) {
        for (const auto &profileMapping : profile.mappings) {
          if (profileMapping.id == mapping.id) {
            ExecuteConfigurableAutofire(profileId,
                                        const_cast<Mapping &>(profileMapping));
            return;
          }
        }
      }
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

void MapManager::ExecuteAutofire(Mapping &mapping) {
  if (mapping.autofireMode.empty() || mapping.autofireMode == "normal") {
    // Use legacy autofire for backward compatibility
    auto now = std::chrono::steady_clock::now();
    int interval =
        mapping.turbo ? mapping.turboInterval : mapping.autofireInterval;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - mapping.lastFireTime)
                       .count();

    if (elapsed >= interval) {
      for (const auto &key : mapping.targetKeys) {
        io->Send(key);
      }
      mapping.lastFireTime = now;
    }
  } else {
    // Use configurable autofire system
    for (const auto &[profileId, profile] : profiles) {
      for (const auto &profileMapping : profile.mappings) {
        if (profileMapping.id == mapping.id) {
          ExecuteConfigurableAutofire(profileId,
                                      const_cast<Mapping &>(profileMapping));
          return;
        }
      }
    }
  }
}

void MapManager::ExecuteMacro(const Mapping &mapping) {
  std::thread([this, mapping]() {
    for (const auto &[key, delay] : mapping.macroSequence) {
      io->Send(key);
      if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      }
    }
  }).detach();
}

void MapManager::ExecuteMouseMovement(const Mapping &mapping, float axisValue) {
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

void MapManager::SaveProfiles(const std::string &filepath) {
  json j = json::array();

  std::lock_guard<std::mutex> lock(profileMutex);
  for (const auto &[id, profile] : profiles) {
    j.push_back(ExportProfileToJson(id));
  }

  std::ofstream file(filepath);
  file << j.dump(2);
  havel::info("Saved {} profiles to {}", profiles.size(), filepath);
}

void MapManager::LoadProfiles(const std::string &filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    havel::error("Failed to open profiles file: {}", filepath);
    return;
  }

  json j;
  file >> j;

  for (const auto &profileJson : j) {
    ImportProfileFromJson(profileJson.dump());
  }

  havel::info("Loaded profiles from {}", filepath);
}

std::string
MapManager::ExportProfileToJson(const std::string &profileId) const {
  auto profile = GetProfile(profileId);
  if (!profile)
    return "{}";

  json j;
  j["id"] = profile->id;
  j["name"] = profile->name;
  j["description"] = profile->description;
  j["enabled"] = profile->enabled;

  j["mappings"] = json::array();
  for (const auto &mapping : profile->mappings) {
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

void MapManager::ImportProfileFromJson(const std::string &jsonStr) {
  try {
    json j = json::parse(jsonStr);

    Profile profile;
    profile.id = j["id"];
    profile.name = j["name"];
    profile.description = j.value("description", "");
    profile.enabled = j.value("enabled", true);

    for (const auto &m : j["mappings"]) {
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
  } catch (const std::exception &e) {
    havel::error("Failed to import profile: {}", e.what());
  }
}

const MapManager::MappingStats *
MapManager::GetMappingStats(const std::string &profileId,
                            const std::string &mappingId) const {

  auto profileIt = stats.find(profileId);
  if (profileIt == stats.end())
    return nullptr;

  auto mappingIt = profileIt->second.find(mappingId);
  if (mappingIt == profileIt->second.end())
    return nullptr;

  return &mappingIt->second;
}

void MapManager::ResetStats() { stats.clear(); }

// ============================================================================
// Key Recording Implementation
// ============================================================================

void MapManager::StartKeyRecording() {
  if (!io) {
    havel::error("MapManager: IO not available for key recording");
    return;
  }

  // Clear any previous recording
  ClearRecordedKey();

  // Set recording state
  keyRecording = true;
  recordingStartTime = std::chrono::steady_clock::now();

  // Set up key recording callbacks using EventListener
  auto *eventListener = io->GetEventListener();
  if (eventListener) {
    // Set up key down callback for recording
    eventListener->SetKeyDownCallback([this](int keyCode) {
      if (keyRecording && lastRecordedKey.keyCode == 0) {
        RecordKeyEvent(keyCode, true);
      }
    });

    // Set up mouse button callback for recording
    eventListener->SetInputEventCallback([this](const InputEvent &event) {
      if (keyRecording && lastRecordedKey.keyCode == 0) {
        if (event.kind == InputEventKind::Key) {
          RecordKeyEvent(event.code, event.value > 0);
        } else if (event.kind == InputEventKind::MouseWheel) {
          RecordMouseWheelEvent(event.code, event.value);
        }
      }
    });

    havel::info("MapManager: Started key recording");
  } else {
    havel::error("MapManager: EventListener not available for key recording");
    keyRecording = false;
  }
}

void MapManager::StopKeyRecording() {
  keyRecording = false;

  // Clear callbacks to avoid interference
  if (io) {
    auto *eventListener = io->GetEventListener();
    if (eventListener) {
      eventListener->SetKeyDownCallback(nullptr);
      eventListener->SetInputEventCallback(nullptr);
    }
  }

  havel::info("MapManager: Stopped key recording");
}

MapManager::RecordedKey MapManager::GetLastRecordedKey() const {
  return lastRecordedKey;
}

void MapManager::ClearRecordedKey() {
  lastRecordedKey = RecordedKey{};
  lastRecordedKey.keyName = "";
  lastRecordedKey.keyCode = 0;
  lastRecordedKey.source = "";
  lastRecordedKey.modifiers = "";
  lastRecordedKey.isMouse = false;
  lastRecordedKey.isJoystick = false;
}

std::string MapManager::DetectInputSource(int keyCode) {
  // Detect input source based on key code ranges
  if (keyCode >= BTN_MOUSE && keyCode < BTN_JOYSTICK) {
    return "mouse";
  } else if (keyCode >= BTN_JOYSTICK && keyCode < BTN_DIGI) {
    return "joystick";
  } else if (keyCode >= 0 && keyCode <= KEY_MAX) {
    return "evdev";
  }
  return "unknown";
}

std::string MapManager::KeyCodeToString(int keyCode,
                                        const std::string &source) {
  if (source == "mouse") {
    switch (keyCode) {
    case BTN_LEFT:
      return "Mouse1";
    case BTN_RIGHT:
      return "Mouse2";
    case BTN_MIDDLE:
      return "Mouse3";
    case BTN_SIDE:
      return "Mouse4";
    case BTN_EXTRA:
      return "Mouse5";
    default:
      return "Mouse" + std::to_string(keyCode - BTN_MOUSE + 1);
    }
  } else if (source == "joystick") {
    return "Joy" + std::to_string(keyCode - BTN_JOYSTICK + 1);
  } else if (source == "evdev") {
    // Use Linux key codes to names
    switch (keyCode) {
    case KEY_A:
      return "A";
    case KEY_B:
      return "B";
    case KEY_C:
      return "C";
    case KEY_D:
      return "D";
    case KEY_E:
      return "E";
    case KEY_F:
      return "F";
    case KEY_G:
      return "G";
    case KEY_H:
      return "H";
    case KEY_I:
      return "I";
    case KEY_J:
      return "J";
    case KEY_K:
      return "K";
    case KEY_L:
      return "L";
    case KEY_M:
      return "M";
    case KEY_N:
      return "N";
    case KEY_O:
      return "O";
    case KEY_P:
      return "P";
    case KEY_Q:
      return "Q";
    case KEY_R:
      return "R";
    case KEY_S:
      return "S";
    case KEY_T:
      return "T";
    case KEY_U:
      return "U";
    case KEY_V:
      return "V";
    case KEY_W:
      return "W";
    case KEY_X:
      return "X";
    case KEY_Y:
      return "Y";
    case KEY_Z:
      return "Z";
    case KEY_0:
      return "0";
    case KEY_1:
      return "1";
    case KEY_2:
      return "2";
    case KEY_3:
      return "3";
    case KEY_4:
      return "4";
    case KEY_5:
      return "5";
    case KEY_6:
      return "6";
    case KEY_7:
      return "7";
    case KEY_8:
      return "8";
    case KEY_9:
      return "9";
    case KEY_F1:
      return "F1";
    case KEY_F2:
      return "F2";
    case KEY_F3:
      return "F3";
    case KEY_F4:
      return "F4";
    case KEY_F5:
      return "F5";
    case KEY_F6:
      return "F6";
    case KEY_F7:
      return "F7";
    case KEY_F8:
      return "F8";
    case KEY_F9:
      return "F9";
    case KEY_F10:
      return "F10";
    case KEY_F11:
      return "F11";
    case KEY_F12:
      return "F12";
    case KEY_UP:
      return "Up";
    case KEY_DOWN:
      return "Down";
    case KEY_LEFT:
      return "Left";
    case KEY_RIGHT:
      return "Right";
    case KEY_SPACE:
      return "Space";
    case KEY_ENTER:
      return "Enter";
    case KEY_ESC:
      return "Escape";
    case KEY_TAB:
      return "Tab";
    case KEY_BACKSPACE:
      return "Backspace";
    case KEY_DELETE:
      return "Delete";
    case KEY_HOME:
      return "Home";
    case KEY_END:
      return "End";
    case KEY_PAGEUP:
      return "PageUp";
    case KEY_PAGEDOWN:
      return "PageDown";
    case KEY_LEFTCTRL:
      return "Ctrl";
    case KEY_RIGHTCTRL:
      return "Ctrl";
    case KEY_LEFTSHIFT:
      return "Shift";
    case KEY_RIGHTSHIFT:
      return "Shift";
    case KEY_LEFTALT:
      return "Alt";
    case KEY_RIGHTALT:
      return "Alt";
    case KEY_LEFTMETA:
      return "Meta";
    case KEY_RIGHTMETA:
      return "Meta";
    default:
      return "Key" + std::to_string(keyCode);
    }
  }
  return "Unknown" + std::to_string(keyCode);
}

MapManager::RecordedKey MapManager::RecordCurrentInput() {
  // Simulate recording a key for testing
  RecordedKey simulated;
  simulated.keyCode = 30; // 'A' key
  simulated.source = "evdev";
  simulated.keyName = KeyCodeToString(simulated.keyCode, simulated.source);
  simulated.modifiers = "";
  simulated.isMouse = false;
  simulated.isJoystick = false;

  lastRecordedKey = simulated;
  return simulated;
}

void MapManager::RecordKeyEvent(int keyCode, bool isDown) {
  if (!isDown || lastRecordedKey.keyCode != 0) {
    return; // Only record on key down and if not already recorded
  }

  lastRecordedKey.keyCode = keyCode;
  lastRecordedKey.source = DetectInputSource(keyCode);
  lastRecordedKey.keyName = KeyCodeToString(keyCode, lastRecordedKey.source);
  lastRecordedKey.isMouse = (lastRecordedKey.source == "mouse");
  lastRecordedKey.isJoystick = (lastRecordedKey.source == "joystick");

  // Get current modifiers
  if (io && io->GetEventListener()) {
    int modifiers = io->GetEventListener()->GetCurrentModifiersMask();
    if (modifiers & EventListener::Modifier::Ctrl) {
      lastRecordedKey.modifiers += "Ctrl+";
    }
    if (modifiers & EventListener::Modifier::Shift) {
      lastRecordedKey.modifiers += "Shift+";
    }
    if (modifiers & EventListener::Modifier::Alt) {
      lastRecordedKey.modifiers += "Alt+";
    }
    if (modifiers & EventListener::Modifier::Meta) {
      lastRecordedKey.modifiers += "Meta+";
    }
  }

  // Construct full key name with modifiers
  if (!lastRecordedKey.modifiers.empty()) {
    lastRecordedKey.keyName =
        lastRecordedKey.modifiers + lastRecordedKey.keyName;
  }

  havel::info("MapManager: Recorded key: {} (code: {}, source: {})",
               lastRecordedKey.keyName, lastRecordedKey.keyCode,
               lastRecordedKey.source);
}

void MapManager::RecordMouseWheelEvent(int wheelCode, int value) {
  if (lastRecordedKey.keyCode != 0) {
    return; // Already recorded something
  }

  lastRecordedKey.keyCode = wheelCode;
  lastRecordedKey.source = "mouse";
  lastRecordedKey.isMouse = true;
  lastRecordedKey.isJoystick = false;

  // Determine wheel direction
  if (wheelCode == REL_WHEEL) {
    lastRecordedKey.keyName = (value > 0) ? "WheelUp" : "WheelDown";
  } else if (wheelCode == REL_HWHEEL) {
    lastRecordedKey.keyName = (value > 0) ? "WheelRight" : "WheelLeft";
  }

  // Get current modifiers
  if (io && io->GetEventListener()) {
    int modifiers = io->GetEventListener()->GetCurrentModifiersMask();
    if (modifiers & EventListener::Modifier::Ctrl) {
      lastRecordedKey.modifiers += "Ctrl+";
    }
    if (modifiers & EventListener::Modifier::Shift) {
      lastRecordedKey.modifiers += "Shift+";
    }
    if (modifiers & EventListener::Modifier::Alt) {
      lastRecordedKey.modifiers += "Alt+";
    }
    if (modifiers & EventListener::Modifier::Meta) {
      lastRecordedKey.modifiers += "Meta+";
    }
  }

  // Construct full key name with modifiers
  if (!lastRecordedKey.modifiers.empty()) {
    lastRecordedKey.keyName =
        lastRecordedKey.modifiers + lastRecordedKey.keyName;
  }

  havel::info("MapManager: Recorded mouse wheel: {} (code: {}, value: {})",
               lastRecordedKey.keyName, lastRecordedKey.keyCode, value);
}

// ============================================================================
// Autopress Toggle Implementation
// ============================================================================

void MapManager::AddAutopressToggleMapping(
    const std::string &profileId, const std::string &sourceKey,
    const std::vector<std::string> &targetKeys, int intervalMs, int timeoutMs,
    const std::string &condition) {
  std::lock_guard<std::mutex> lock(profileMutex);

  Mapping mapping;
  mapping.id = GenerateId();
  mapping.name = "Autopress Toggle: " + sourceKey;
  mapping.enabled = true;
  mapping.sourceKey = sourceKey;
  mapping.sourceCode = KeyMap::FromString(sourceKey);
  mapping.actionType = ActionType::Press;
  mapping.targetKeys = targetKeys;
  mapping.targetCodes = {};
  for (const auto &key : targetKeys) {
    mapping.targetCodes.push_back(KeyMap::FromString(key));
  }

  // Autopress toggle settings
  mapping.autopressToggle = true;
  mapping.autopressInterval = intervalMs;
  mapping.autopressTimeout = timeoutMs;
  mapping.autopressCondition = !condition.empty();
  mapping.autopressConditionExpr = condition;

  // Add to profile
  auto &profile = profiles[profileId];
  profile.mappings.push_back(mapping);

  // Register with IO system
  RegisterMapping(profileId, mapping);

  havel::info("MapManager: Added autopress toggle mapping: {} -> {} "
               "(interval: {}ms, timeout: {}ms)",
               sourceKey, targetKeys[0], intervalMs, timeoutMs);
}

void MapManager::SetAutopressToggleInterval(const std::string &profileId,
                                            const std::string &mappingId,
                                            int intervalMs) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autopressInterval = intervalMs;
    havel::info("MapManager: Set autopress interval to {}ms for mapping {}",
                 intervalMs, mappingId);
  }
}

void MapManager::SetAutopressToggleTimeout(const std::string &profileId,
                                           const std::string &mappingId,
                                           int timeoutMs) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autopressTimeout = timeoutMs;
    havel::info("MapManager: Set autopress timeout to {}ms for mapping {}",
                 timeoutMs, mappingId);
  }
}

void MapManager::SetAutopressToggleCondition(const std::string &profileId,
                                             const std::string &mappingId,
                                             const std::string &condition) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autopressCondition = !condition.empty();
    mappingIt->autopressConditionExpr = condition;
    havel::info("MapManager: Set autopress condition for mapping {}: {}",
                 mappingId, condition);
  }
}

bool MapManager::IsAutopressToggleActive(const std::string &profileId,
                                         const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = autopressToggleActive.find(profileId);
  if (profileIt == autopressToggleActive.end()) {
    return false;
  }

  auto mappingIt = profileIt->second.find(mappingId);
  return mappingIt != profileIt->second.end() && mappingIt->second.load();
}

void MapManager::StopAutopressToggle(const std::string &profileId,
                                     const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  // Stop the autopress thread
  auto profileIt = autopressToggleThreads.find(profileId);
  if (profileIt != autopressToggleThreads.end()) {
    auto mappingIt = profileIt->second.find(mappingId);
    if (mappingIt != profileIt->second.end()) {
      // Set active flag to false
      auto activeIt = autopressToggleActive.find(profileId);
      if (activeIt != autopressToggleActive.end()) {
        auto activeMappingIt = activeIt->second.find(mappingId);
        if (activeMappingIt != activeIt->second.end()) {
          activeMappingIt->second.store(false);
        }
      }

      // Join and remove thread
      if (mappingIt->second.joinable()) {
        mappingIt->second.join();
      }
      profileIt->second.erase(mappingIt);
    }
  }

  havel::info("MapManager: Stopped autopress toggle for mapping {}",
               mappingId);
}

void MapManager::ExecuteAutopressToggle(const std::string &profileId,
                                        Mapping &mapping) {
  if (!mapping.autopressToggle) {
    return;
  }

  // Check conditions
  if (mapping.autopressCondition && !EvaluateAutopressCondition(mapping)) {
    return;
  }

  // Initialize active flag
  auto &activeFlag = autopressToggleActive[profileId][mapping.id];
  activeFlag.store(true);

  // Record start time
  auto startTime = std::chrono::steady_clock::now();
  autopressToggleStartTimes[profileId][mapping.id] = startTime;

  // Start autopress thread
  autopressToggleThreads[profileId][mapping.id] =
      std::thread([this, profileId, mapping, startTime, &activeFlag]() {
        auto interval = std::chrono::milliseconds(mapping.autopressInterval);
        auto timeout = std::chrono::milliseconds(mapping.autopressTimeout);

        while (activeFlag.load()) {
          // Check timeout
          if (mapping.autopressTimeout > 0) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed >= timeout) {
              break;
            }
          }

          // Check conditions
          if (mapping.autopressCondition &&
              !EvaluateAutopressCondition(mapping)) {
            std::this_thread::sleep_for(interval);
            continue;
          }

          // Execute the target keys
          for (const auto &targetKey : mapping.targetKeys) {
            io->Send(targetKey);
          }

          // Update statistics
          //UpdateMappingStats(profileId, mapping.id, true);

          // Sleep for interval
          std::this_thread::sleep_for(interval);
        }

        // Clean up
        activeFlag.store(false);
        havel::info("MapManager: Autopress toggle stopped for mapping {}",
                     mapping.id);
      });
}

bool MapManager::EvaluateAutopressCondition(const Mapping &mapping) {
  if (!mapping.autopressCondition || mapping.autopressConditionExpr.empty()) {
    return true;
  }

  // Simple condition evaluation (can be extended)
  const std::string &condition = mapping.autopressConditionExpr;

  // Basic condition checks
  if (condition == "always") {
    return true;
  } else if (condition == "never") {
    return false;
  } else if (condition.find("window:") == 0) {
    // Window-specific condition
    std::string windowName = condition.substr(7);
    if (io) {
      std::string currentWindow = io->GetActiveWindowTitle();
      return currentWindow.find(windowName) != std::string::npos;
    }
  } else if (condition.find("time:") == 0) {
    // Time-based condition
    std::string timeStr = condition.substr(5);
    int targetHour = std::stoi(timeStr);
    auto now = std::chrono::system_clock::now();
    auto timeInfo = std::chrono::system_clock::to_time_t(now);
    auto localTime = std::localtime(&timeInfo);
    return localTime->tm_hour == targetHour;
  }

  return true; // Default to true for unknown conditions
}

// ============================================================================
// Configurable Autofire Implementation
// ============================================================================

void MapManager::SetAutofireRate(const std::string &profileId,
                                 const std::string &mappingId, int rateMs) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autofireRate = rateMs;
    havel::info("MapManager: Set autofire rate to {}ms for mapping {}", rateMs,
                 mappingId);
  }
}

void MapManager::SetAutofireBurstCount(const std::string &profileId,
                                       const std::string &mappingId,
                                       int burstCount) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autofireBurstCount = burstCount;
    havel::info("MapManager: Set autofire burst count to {} for mapping {}",
                 burstCount, mappingId);
  }
}

void MapManager::SetAutofireBurstDelay(const std::string &profileId,
                                       const std::string &mappingId,
                                       int burstDelayMs) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autofireBurstDelay = burstDelayMs;
    havel::info("MapManager: Set autofire burst delay to {}ms for mapping {}",
                 burstDelayMs, mappingId);
  }
}

void MapManager::SetAutofireMode(const std::string &profileId,
                                 const std::string &mappingId,
                                 const std::string &mode) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autofireMode = mode;
    havel::info("MapManager: Set autofire mode to '{}' for mapping {}", mode,
                 mappingId);
  }
}

void MapManager::SetAutofireCondition(const std::string &profileId,
                                      const std::string &mappingId,
                                      const std::string &condition) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    mappingIt->autofireCondition = condition;
    havel::info("MapManager: Set autofire condition for mapping {}: {}",
                 mappingId, condition);
  }
}

int MapManager::GetAutofireRate(const std::string &profileId,
                                const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return 0;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    return mappingIt->autofireRate;
  }

  return 0;
}

int MapManager::GetAutofireBurstCount(const std::string &profileId,
                                      const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return 0;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    return mappingIt->autofireBurstCount;
  }

  return 0;
}

int MapManager::GetAutofireBurstDelay(const std::string &profileId,
                                      const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return 0;
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    return mappingIt->autofireBurstDelay;
  }

  return 0;
}

std::string MapManager::GetAutofireMode(const std::string &profileId,
                                        const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = profiles.find(profileId);
  if (profileIt == profiles.end()) {
    return "";
  }

  auto mappingIt = std::find_if(
      profileIt->second.mappings.begin(), profileIt->second.mappings.end(),
      [&mappingId](const Mapping &m) { return m.id == mappingId; });
  if (mappingIt != profileIt->second.mappings.end()) {
    return mappingIt->autofireMode;
  }

  return "";
}

bool MapManager::IsAutofireActive(const std::string &profileId,
                                  const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  auto profileIt = autofireActive.find(profileId);
  if (profileIt == autofireActive.end()) {
    return false;
  }

  auto mappingIt = profileIt->second.find(mappingId);
  return mappingIt != profileIt->second.end() && mappingIt->second.load();
}

void MapManager::StopAutofire(const std::string &profileId,
                              const std::string &mappingId) {
  std::lock_guard<std::mutex> lock(profileMutex);

  // Stop the autofire thread
  auto profileIt = autofireThreads.find(profileId);
  if (profileIt != autofireThreads.end()) {
    auto mappingIt = profileIt->second.find(mappingId);
    if (mappingIt != profileIt->second.end()) {
      // Set active flag to false
      auto activeIt = autofireActive.find(profileId);
      if (activeIt != autofireActive.end()) {
        auto activeMappingIt = activeIt->second.find(mappingId);
        if (activeMappingIt != activeIt->second.end()) {
          activeMappingIt->second.store(false);
        }
      }

      // Join and remove thread
      if (mappingIt->second.joinable()) {
        mappingIt->second.join();
      }
      profileIt->second.erase(mappingIt);
    }
  }

  havel::info("MapManager: Stopped autofire for mapping {}", mappingId);
}

void MapManager::ExecuteConfigurableAutofire(const std::string &profileId,
                                             Mapping &mapping) {
  if (!mapping.autofire) {
    return;
  }

  // Check conditions
  if (!mapping.autofireCondition.empty() &&
      !EvaluateAutofireCondition(mapping)) {
    return;
  }

  // Initialize active flag and burst counter
  auto &activeFlag = autofireActive[profileId][mapping.id];
  auto &burstCounter = autofireBurstCounters[profileId][mapping.id];
  activeFlag.store(true);
  burstCounter = 0;

  // Determine effective rate based on mode
  int effectiveRate = mapping.autofireRate;
  if (mapping.autofireMode == "burst" && mapping.autofireBurstCount > 0) {
    effectiveRate =
        mapping.autofireBurstDelay; // Use burst delay for burst mode
  }

  // Start autofire thread
  autofireThreads[profileId][mapping.id] = std::thread(
      [this, profileId, mapping, effectiveRate, &activeFlag, &burstCounter]() {
        auto interval = std::chrono::milliseconds(effectiveRate);
        auto burstDelay = std::chrono::milliseconds(mapping.autofireBurstDelay);

        while (activeFlag.load()) {
          // Check conditions
          if (!mapping.autofireCondition.empty() &&
              !EvaluateAutofireCondition(mapping)) {
            std::this_thread::sleep_for(interval);
            continue;
          }

          // Handle different autofire modes
          if (mapping.autofireMode == "normal") {
            // Normal continuous autofire
            for (const auto &key : mapping.targetKeys) {
              io->Send(key);
            }
          } else if (mapping.autofireMode == "burst") {
            // Burst mode: fire N shots, then pause
            if (burstCounter < mapping.autofireBurstCount ||
                mapping.autofireBurstCount == 0) {
              for (const auto &key : mapping.targetKeys) {
                io->Send(key);
              }
              burstCounter++;

              // Update statistics would go here
            } else {
              // Burst completed, wait for burst delay
              std::this_thread::sleep_for(burstDelay);
              burstCounter = 0;
            }
          } else if (mapping.autofireMode == "hold") {
            // Hold mode: keep keys held while trigger is pressed
            for (const auto &key : mapping.targetKeys) {
              io->Send(key);
            }
          } else if (mapping.autofireMode == "smart") {
            // Smart mode: adaptive rate based on key usage patterns
            auto adaptiveInterval = interval;
            // Simple adaptive logic: faster rate if key hasn't been used
            // recently
            auto timeSinceLastFire =
                std::chrono::steady_clock::now() - mapping.lastFireTime;
            if (timeSinceLastFire < std::chrono::seconds(1)) {
              adaptiveInterval =
                  std::chrono::milliseconds(effectiveRate / 2); // Faster rate
            }

            for (const auto &key : mapping.targetKeys) {
              io->Send(key);
            }

            std::this_thread::sleep_for(adaptiveInterval);
          }

          // Update last fire time would go here (mapping is const)

          // Sleep for base interval
          std::this_thread::sleep_for(interval);
        }

        // Clean up
        activeFlag.store(false);
        havel::info("MapManager: Configurable autofire stopped for mapping {}",
                     mapping.id);
      });
}

bool MapManager::EvaluateAutofireCondition(const Mapping &mapping) {
  if (mapping.autofireCondition.empty()) {
    return true;
  }

  // Use same condition evaluation as autopress toggle
  const std::string &condition = mapping.autofireCondition;

  if (condition == "always") {
    return true;
  } else if (condition == "never") {
    return false;
  } else if (condition.find("window:") == 0) {
    std::string windowName = condition.substr(7);
    if (io) {
      std::string currentWindow = io->GetActiveWindowTitle();
      return currentWindow.find(windowName) != std::string::npos;
    }
  } else if (condition.find("time:") == 0) {
    std::string timeStr = condition.substr(5);
    int targetHour = std::stoi(timeStr);
    auto now = std::chrono::system_clock::now();
    auto timeInfo = std::chrono::system_clock::to_time_t(now);
    auto localTime = std::localtime(&timeInfo);
    return localTime->tm_hour == targetHour;
  }

  return true; // Default to true for unknown conditions
}

} // namespace havel
