# Config Operator (>>) and Brightness Debugging

## Summary

Implemented enhanced `>>` config operator support and added comprehensive debugging to BrightnessManager temperature functions.

## 1. Config Operator (>>) Enhancement ✅

### Original Implementation
The `>>` operator existed but had limited functionality:
- Only supported: `value >> "config.key"` (SET)
- GET operation was not implemented

### New Implementation

**File:** `src/havel-lang/runtime/Interpreter.cpp`

**Supported Operations:**

#### 1.1 SET Config Value (String)
```havel
"myvalue" >> "General.Setting"
```

#### 1.2 SET Config Value (Number)
```havel
80 >> "Brightness.Default"
```

#### 1.3 SET Config Value (Boolean)
```havel
true >> "Debug.Enabled"
```

#### 1.4 GET Config Value (Reverse Direction)
```havel
"Brightness.Default" >> defaultBrightness  // Gets config value
```

#### 1.5 SET Config from Object/Block
```havel
{ defaultBrightness: 75, step: 1.0 } >> "Brightness"
```

#### 1.6 SET Config from Nested Object
```havel
{
  defaultBrightness: 75,
  step: 1.0,
  time: {
    startTime: "8:00",
    endTime: "22:00"
  }
} >> "Brightness"
```

### Implementation Details

```cpp
case ast::BinaryOperator::ConfigAppend: {
    auto& config = Configs::Get();
    
    if (right.isObject()) {
        // value >> {config} - merge object into config
    } else if (right.isString()) {
        // value >> "config.key" - SET config value
        if (left.isString()) { /* string value */ }
        else if (left.isNumber()) { /* number value */ }
        else if (left.isBool()) { /* bool value */ }
        else if (left.isObject()) { /* merge object */ }
    } else {
        // config.path >> variable - GET config value
        auto configVal = config.Get<std::string>(configKey, "");
        // Returns string, number, or nullptr
    }
}
```

### Usage Examples

```havel
// Set brightness config
75 >> "Brightness.Default"

// Get brightness config
"Brightness.Default" >> currentBrightness
print("Current brightness:", currentBrightness)

// Set multiple values at once
{
  defaultBrightness: 80,
  step: 0.05,
  autoAdjust: true
} >> "Brightness"

// Get nested config
"Brightness.Time.startTime" >> startTime
print("Start time:", startTime)
```

---

## 2. BrightnessManager Temperature Debugging ✅

### Files Modified
- `src/core/BrightnessManager.cpp`

### Added Debug Logging

#### 2.1 setTemperature(int kelvin)
```cpp
debug("BrightnessManager::setTemperature({}) - clamped to {}K", kelvin, kelvin);
debug("Current display method: {}", displayMethod);
debug("Using gammastep for Wayland temperature control");
debug("  Set monitor '{}' temperature to {}K", monitor, kelvin);
debug("Temperature set successfully via gammastep");
debug("gammastep failed, falling through to applyAllSettings");
```

#### 2.2 setTemperature(monitor, kelvin)
```cpp
debug("BrightnessManager::setTemperature('{}', {}) - clamped to {}K", monitor, kelvin, kelvin);
debug("Using gammastep for Wayland temperature control on monitor '{}'", monitor);
debug("Temperature set successfully to {}K on monitor '{}'", kelvin, monitor);
debug("Converted {}K to RGB: ({}, {}, {})", kelvin, rgb.red, rgb.green, rgb.blue);
```

#### 2.3 getTemperature() variants
```cpp
debug("BrightnessManager::getTemperature() - No monitors found, returning default 6500K");
debug("BrightnessManager::getTemperature() - Primary monitor '{}' temperature: {}K", monitor, temp);
debug("BrightnessManager::getTemperature('{}') - Found cached temperature: {}K", monitor, it->second);
debug("BrightnessManager::getTemperature('{}') - No cached temperature, returning default 6500K", monitor);
```

#### 2.4 increaseTemperature/decreaseTemperature
```cpp
debug("BrightnessManager::increaseTemperature({}) - Primary monitor: {}K -> {}K", amount, currentTemp, newTemp);
debug("BrightnessManager::increaseTemperature('{}', {}) - {}K -> {}K", monitor, amount, currentTemp, newTemp);
debug("BrightnessManager::decreaseTemperature({}) - Primary monitor: {}K -> {}K", amount, currentTemp, newTemp);
```

### Debug Output Example

```
[BRIGHTNESS] BrightnessManager::setTemperature(5500) - clamped to 5500K
[BRIGHTNESS] Current display method: wayland
[BRIGHTNESS] Using gammastep for Wayland temperature control
[BRIGHTNESS]   Set monitor 'eDP-1' temperature to 5500K
[BRIGHTNESS] Temperature set successfully via gammastep

[BRIGHTNESS] BrightnessManager::getTemperature() - Primary monitor 'eDP-1' temperature: 5500K

[BRIGHTNESS] BrightnessManager::increaseTemperature(500) - Primary monitor: 5500K -> 6000K
[BRIGHTNESS] BrightnessManager::setTemperature(6000) - clamped to 6000K
```

---

## 3. Testing

### Test Config GET/SET
```havel
// Test SET
75 >> "Brightness.Default"
print("Set brightness to 75")

// Test GET
"Brightness.Default" >> value
print("Got brightness:", value)

// Test object merge
{ step: 0.1, auto: true } >> "Brightness"
"Brightness.step" >> step
print("Step:", step)
```

### Test Temperature Debugging
Enable debug logging:
```havel
debug(true)  // Enable debug output
brightnessManager.setTemperature(5500)
brightnessManager.increaseTemperature(500)
let temp = brightnessManager.getTemperature()
print("Temperature:", temp)
```

---

## 4. Build Status

```
[100%] Built target havel
```

All changes compile successfully.

---

## 5. Files Modified

1. **`src/havel-lang/runtime/Interpreter.cpp`**
   - Enhanced `ConfigAppend` binary operator
   - Added GET support (reverse direction)
   - Added object/block merge support
   - Added type-safe value handling (string, number, bool, object)

2. **`src/core/BrightnessManager.cpp`**
   - Added debug logging to `setTemperature()` (both overloads)
   - Added debug logging to `getTemperature()` (all 3 overloads)
   - Added debug logging to `increaseTemperature()` (both overloads)
   - Added debug logging to `decreaseTemperature()` (both overloads)

---

## 6. Benefits

### Config Operator
- **Bidirectional**: Both GET and SET operations
- **Type-safe**: Handles string, number, bool, and object values
- **Batch operations**: Merge entire config objects at once
- **Nested support**: Supports dot notation for nested keys

### Brightness Debugging
- **Troubleshooting**: Easy to diagnose temperature issues
- **Visibility**: See exactly what values are being set
- **Flow tracking**: Follow temperature changes through the system
- **Backend info**: See which backend (X11/Wayland/gammastep) is being used

---

## 7. Future Enhancements

1. **Config validation** - Validate config values before setting
2. **Config schema** - Define allowed config keys and types
3. **Config events** - Callbacks when config changes
4. **Config persistence** - Auto-save config changes
5. **Brightness calibration** - Calibrate temperature to actual display output
