# MapManager - Advanced Input Mapping System

## Overview

MapManager is a comprehensive input mapping system similar to JoyToKey, allowing you to create complex input mappings with profiles, conditions, autofire, macros, and more.

## Features

### 🎮 Core Features
- **Multiple Profiles** - Create different mapping profiles for different games/applications
- **Conditional Mappings** - Activate mappings based on active window, process, or custom conditions
- **Autofire/Turbo** - Rapid-fire key presses while holding a button
- **Toggle Mode** - Press once to activate, press again to deactivate
- **Macro Recording** - Record and playback complex key sequences
- **Combo Support** - Multi-key combinations
- **Mouse Movement** - Map joystick axes to mouse movement
- **Profile Switching** - Switch between profiles with hotkeys
- **Statistics** - Track mapping usage and performance

### 🗺️ Mapping Types
1. **KeyToKey** - Remap keyboard keys
2. **KeyToMouse** - Keyboard key triggers mouse action
3. **MouseToKey** - Mouse button triggers keyboard key
4. **JoyToKey** - Joystick button to keyboard key
5. **JoyToMouse** - Joystick button to mouse button
6. **JoyAxisToMouse** - Joystick axis controls mouse movement
7. **Combo** - Multi-key combo to action
8. **Macro** - Recorded sequence playback

### ⚡ Action Types
1. **Press** - Single key press
2. **Hold** - Hold key while source is held
3. **Toggle** - Toggle on/off state
4. **Autofire** - Rapid repeated presses
5. **Turbo** - Faster autofire mode
6. **Macro** - Execute recorded macro

## Usage Examples

### Basic Setup

```cpp
#include "core/io/MapManager.hpp"
#include "core/IO.hpp"

// Create IO and MapManager
IO io;
MapManager mapManager(&io);

// Create a profile
Profile gamingProfile;
gamingProfile.id = "gaming-001";
gamingProfile.name = "Gaming Profile";
gamingProfile.description = "Mappings for FPS games";

mapManager.AddProfile(gamingProfile);
```

### Simple Key Remapping

```cpp
// Remap CapsLock to Ctrl
Mapping capsToCtrl;
capsToCtrl.id = "caps-to-ctrl";
capsToCtrl.name = "CapsLock → Ctrl";
capsToCtrl.type = MappingType::KeyToKey;
capsToCtrl.sourceKey = "capslock";
capsToCtrl.targetKeys = {"lctrl"};
capsToCtrl.actionType = ActionType::Hold;

mapManager.AddMapping("gaming-001", capsToCtrl);
```

### Autofire Mapping

```cpp
// Autofire on mouse button
Mapping autofire;
autofire.id = "autofire-lbutton";
autofire.name = "Autofire Left Click";
autofire.type = MappingType::MouseToMouse;
autofire.sourceKey = "lbutton";
autofire.targetKeys = {"lbutton"};
autofire.actionType = ActionType::Autofire;
autofire.autofire = true;
autofire.autofireInterval = 50;  // 50ms = 20 clicks/second

mapManager.AddMapping("gaming-001", autofire);
```

### Turbo Mode

```cpp
// Turbo fire with joystick button
Mapping turbo;
turbo.id = "turbo-joya";
turbo.name = "Turbo A Button";
turbo.type = MappingType::JoyToKey;
turbo.sourceKey = "joya";
turbo.targetKeys = {"space"};
turbo.actionType = ActionType::Turbo;
turbo.turbo = true;
turbo.turboInterval = 25;  // 25ms = 40 presses/second

mapManager.AddMapping("gaming-001", turbo);
```

### Toggle Mapping

```cpp
// Toggle crouch
Mapping toggleCrouch;
toggleCrouch.id = "toggle-crouch";
toggleCrouch.name = "Toggle Crouch";
toggleCrouch.type = MappingType::KeyToKey;
toggleCrouch.sourceKey = "c";
toggleCrouch.targetKeys = {"lctrl"};
toggleCrouch.actionType = ActionType::Toggle;
toggleCrouch.toggleMode = true;

mapManager.AddMapping("gaming-001", toggleCrouch);
```

### Conditional Mapping

```cpp
// Only active in specific window
Mapping conditionalMap;
conditionalMap.id = "game-specific";
conditionalMap.name = "Game-Specific Mapping";
conditionalMap.sourceKey = "f";
conditionalMap.targetKeys = {"e"};

// Add window title condition
MappingCondition condition;
condition.type = ConditionType::WindowTitle;
condition.pattern = ".*Counter-Strike.*";  // Regex pattern
conditionalMap.conditions.push_back(condition);

mapManager.AddMapping("gaming-001", conditionalMap);
```

### Joystick to Mouse Movement

```cpp
// Right stick controls mouse
Mapping stickToMouse;
stickToMouse.id = "stick-mouse";
stickToMouse.name = "Right Stick → Mouse";
stickToMouse.type = MappingType::JoyAxisToMouse;
stickToMouse.sourceKey = "joy_axis_rx";  // Right stick X
stickToMouse.mouseMovement = true;
stickToMouse.sensitivity = 2.0f;         // 2x speed
stickToMouse.deadzone = 0.15f;           // 15% deadzone
stickToMouse.acceleration = true;        // Quadratic acceleration

mapManager.AddMapping("gaming-001", stickToMouse);
```

### Macro Recording

```cpp
// Record a macro
mapManager.StartMacroRecording("build-combo");

// User presses keys: Q, W, E, R (with delays)
// ... keys are automatically recorded ...

mapManager.StopMacroRecording();

// Save to mapping
Mapping macroMap;
macroMap.id = "build-macro";
macroMap.name = "Build Combo";
macroMap.sourceKey = "f1";
macroMap.actionType = ActionType::Macro;

mapManager.SaveMacro("gaming-001", macroMap.id);
mapManager.AddMapping("gaming-001", macroMap);
```

### Complex Combo

```cpp
// Multi-key combo
Mapping combo;
combo.id = "ultimate-combo";
combo.name = "Ultimate Ability";
combo.type = MappingType::Combo;
combo.sourceKey = "q & e";  // Q and E together
combo.targetKeys = {"r"};   // Triggers R
combo.actionType = ActionType::Press;

mapManager.AddMapping("gaming-001", combo);
```

### Profile Management

```cpp
// Create multiple profiles
Profile desktopProfile;
desktopProfile.id = "desktop-001";
desktopProfile.name = "Desktop Profile";
mapManager.AddProfile(desktopProfile);

Profile browserProfile;
browserProfile.id = "browser-001";
browserProfile.name = "Browser Profile";
mapManager.AddProfile(browserProfile);

// Switch profiles
mapManager.SetActiveProfile("gaming-001");

// Cycle through profiles with hotkey
mapManager.SetProfileSwitchHotkey("@^!P");  // Ctrl+Alt+P

// Or manually
mapManager.NextProfile();
mapManager.PreviousProfile();
```

### Save/Load Profiles

```cpp
// Save all profiles
mapManager.SaveProfiles("my_profiles.json");

// Load profiles
mapManager.LoadProfiles("my_profiles.json");

// Export single profile
std::string json = mapManager.ExportProfileToJson("gaming-001");
// ... save to file or share ...

// Import profile
mapManager.ImportProfileFromJson(json);
```

### Statistics

```cpp
// Get mapping statistics
auto* stats = mapManager.GetMappingStats("gaming-001", "autofire-lbutton");
if (stats) {
    std::cout << "Activations: " << stats->activationCount << "\n";
    std::cout << "Total duration: " << stats->totalDurationMs << "ms\n";
}

// Reset statistics
mapManager.ResetStats();
```

## GUI Usage

### Opening MapManager Window

```cpp
#include "gui/MapManagerWindow.hpp"

// Create and show window
MapManagerWindow* window = new MapManagerWindow(&mapManager, &io);
window->show();
```

### GUI Features

#### Profile Panel (Left)
- **Profile List** - Shows all available profiles
- **New** - Create new profile
- **Delete** - Remove selected profile
- **Duplicate** - Copy existing profile
- **Activate** - Set as active profile
- **Active Indicator** - Shows currently active profile

#### Mapping Table (Middle)
- **Mapping List** - Shows all mappings in selected profile
- **Columns**: Name, Source, Target, Type, Enabled
- **New Mapping** - Add new mapping
- **Edit** - Modify selected mapping
- **Delete** - Remove mapping
- **Duplicate** - Copy mapping
- **Double-click** - Quick edit

#### Editor Panel (Right)
Tabbed interface for detailed configuration:

**1. Basic Tab**
- Mapping name
- Enabled checkbox
- Mapping type dropdown
- Action type dropdown

**2. Source/Target Tab**
- Source key capture
- Target keys list
- Add/remove target keys
- Live key capture button

**3. Autofire Tab**
- Enable autofire checkbox
- Autofire interval (ms)
- Turbo mode checkbox
- Turbo interval (ms)

**4. Advanced Tab**
- Toggle mode
- Sensitivity slider
- Deadzone slider
- Mouse acceleration

**5. Conditions Tab**
- Condition list
- Add/edit/remove conditions
- Window title/class matching
- Process name matching
- Custom condition functions

**6. Macro Tab**
- Macro sequence table
- Record/stop buttons
- Clear macro
- Edit delays

### Hotkey Capture

The GUI includes a special hotkey capture widget:

```cpp
// In your dialog
HotkeyCapture* capture = new HotkeyCapture();
connect(capture, &HotkeyCapture::keyCaptured, [](const QString& key) {
    std::cout << "Captured: " << key.toStdString() << "\n";
});

// Start capturing
capture->startCapture();
// User presses key...
// Signal emitted with key name
```

### Toolbar Actions
- **Import Profile** - Load profile from JSON file
- **Export Profile** - Save profile to JSON file
- **Save All** - Save all profiles to file
- **Load All** - Load all profiles from file
- **Statistics** - View usage statistics

## Configuration File Format

Profiles are saved in JSON format:

```json
{
  "id": "gaming-001",
  "name": "Gaming Profile",
  "description": "FPS game mappings",
  "enabled": true,
  "mappings": [
    {
      "id": "caps-to-ctrl",
      "name": "CapsLock → Ctrl",
      "enabled": true,
      "sourceKey": "capslock",
      "actionType": 1,
      "targetKeys": ["lctrl"],
      "autofire": false,
      "autofireInterval": 100,
      "turbo": false,
      "toggleMode": false
    },
    {
      "id": "autofire-lbutton",
      "name": "Autofire Left Click",
      "enabled": true,
      "sourceKey": "lbutton",
      "actionType": 3,
      "targetKeys": ["lbutton"],
      "autofire": true,
      "autofireInterval": 50,
      "turbo": false,
      "toggleMode": false
    }
  ]
}
```

## Advanced Features

### Custom Conditions

```cpp
// Custom condition function
MappingCondition customCond;
customCond.type = ConditionType::Custom;
customCond.customCheck = []() {
    // Your custom logic
    return someGlobalState.isInCombat;
};

mapping.conditions.push_back(customCond);
```

### Profile-Level Settings

```cpp
Profile profile;
profile.globalSensitivity = 1.5f;  // 150% sensitivity for all mappings
profile.enableAutofire = true;     // Allow autofire
profile.enableMacros = true;       // Allow macros
```

### Mapping Statistics

Track how often mappings are used:

```cpp
auto* stats = mapManager.GetMappingStats(profileId, mappingId);
std::cout << "Used " << stats->activationCount << " times\n";
std::cout << "Last used: " << /* format time */ << "\n";
```

## Best Practices

### 1. Profile Organization
- Create separate profiles for different applications
- Use descriptive names
- Add detailed descriptions
- Group related mappings

### 2. Autofire Settings
- Start with conservative intervals (100ms+)
- Test in-game to avoid detection
- Use turbo mode sparingly
- Consider game-specific limits

### 3. Conditions
- Use window title matching for game-specific mappings
- Combine multiple conditions for precision
- Test conditions before deploying
- Use regex for flexible matching

### 4. Macros
- Keep macros short and simple
- Add appropriate delays between keys
- Test macros thoroughly
- Document macro purpose

### 5. Performance
- Disable unused mappings
- Avoid excessive autofire rates
- Use conditions to limit activation
- Monitor statistics for optimization

## Troubleshooting

### Mapping Not Working
1. Check if profile is active
2. Verify mapping is enabled
3. Check conditions are satisfied
4. Verify source key name is correct
5. Check IO system is initialized

### Autofire Too Fast/Slow
- Adjust `autofireInterval` value
- Test different intervals
- Consider game tick rate
- Check for input lag

### Condition Not Triggering
- Verify window title/class name
- Test regex pattern separately
- Check process name spelling
- Add debug logging

### GUI Not Responding
- Check Qt event loop
- Verify MapManager pointer
- Check for threading issues
- Review error logs

## API Reference

### MapManager Methods

```cpp
// Profile management
void AddProfile(const Profile& profile);
void RemoveProfile(const std::string& profileId);
void SetActiveProfile(const std::string& profileId);
Profile* GetActiveProfile();

// Mapping management
void AddMapping(const std::string& profileId, const Mapping& mapping);
void RemoveMapping(const std::string& profileId, const std::string& mappingId);
void UpdateMapping(const std::string& profileId, const Mapping& mapping);

// Enable/disable
void EnableProfile(const std::string& profileId, bool enable);
void EnableMapping(const std::string& profileId, const std::string& mappingId, bool enable);

// Apply
void ApplyProfile(const std::string& profileId);
void ApplyActiveProfile();
void ClearAllMappings();

// Profile switching
void NextProfile();
void PreviousProfile();
void SetProfileSwitchHotkey(const std::string& hotkey);

// Macro recording
void StartMacroRecording(const std::string& macroName);
void StopMacroRecording();
void SaveMacro(const std::string& profileId, const std::string& mappingId);

// Save/Load
void SaveProfiles(const std::string& filepath);
void LoadProfiles(const std::string& filepath);
std::string ExportProfileToJson(const std::string& profileId) const;
void ImportProfileFromJson(const std::string& json);

// Statistics
const MappingStats* GetMappingStats(const std::string& profileId, 
                                    const std::string& mappingId) const;
void ResetStats();
```

## Integration with EventListener

MapManager works seamlessly with the new EventListener system:

```cpp
// Enable EventListener
io.SetConfig("IO.UseNewEventListener", true);

// MapManager automatically uses EventListener
MapManager mapManager(&io);

// All mappings benefit from:
// - Unified input handling
// - Better performance
// - Universal combo support
// - Mouse/joystick support
```

## Examples Directory

See `examples/mapmanager_examples.cpp` for complete working examples.

---

**Status:** ✅ Complete and ready for use  
**Version:** 1.0  
**Last Updated:** 2025-11-02
