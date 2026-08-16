---
title: "Brightness Module"
description: "Brightness control via XRandR: get/set brightness, color temperature, gamma, and monitor-specific control."
---

# Brightness Module

```hv
use brightness
```

**Source**: `modules/app/brightness.hv` (Havel FFI-based implementation using XRandR)

---

## Basic Brightness

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness.get()` | `() -> num` | Get brightness (0.0 - 1.0) |
| `brightness.set(value)` | `(num) -> nil` | Set brightness (0.0 - 1.0) |
| `brightness.increase(delta)` | `(num) -> nil` | Increase brightness by delta |
| `brightness.decrease(delta)` | `(num) -> nil` | Decrease brightness by delta |

```hv
current = brightness.get()  // 0.75
brightness.set(0.5)         // 50%
brightness.increase(0.1)    // +10%
brightness.decrease(0.05)   // -5%
```

---

## Color Temperature

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness.temperature()` | `() -> int` | Get color temperature (Kelvin, 1000-25000) |
| `brightness.setTemperature(kelvin)` | `(int) -> nil` | Set color temperature |

```hv
temp = brightness.temperature()   // 6500
brightness.setTemperature(4000)   // Warmer (more red)
brightness.setTemperature(8000)   // Cooler (more blue)
```

---

## Gamma

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness.gamma()` | `() -> num` | Get gamma (0.1 - 5.0) |
| `brightness.setGamma(value)` | `(num) -> nil` | Set gamma |

```hv
g = brightness.gamma()   // 1.0
brightness.setGamma(1.2)
```

---

## Shadow Lift

Shadow lift enhances contrast in darker parts of the image without blowing out highlights.

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness.shadowLift()` | `() -> num` | Get shadow lift (0.0 - 4.0) |
| `brightness.setShadowLift(value, monitor?)` | `(num, str?) -> nil` | Set shadow lift (0.0 - 4.0) |
| `brightness.increaseShadowLift(delta, monitor?)` | `(num, str?) -> nil` | Increase shadow lift |
| `brightness.decreaseShadowLift(delta, monitor?)` | `(num, str?) -> nil` | Decrease shadow lift |

```hv
sl = brightness.shadowLift()            // 0.0
brightness.setShadowLift(1.0)           // Lift shadows on all monitors
brightness.setShadowLift(1.0, "HDMI-0") // Lift shadows on specific monitor
brightness.increaseShadowLift(0.2)      // Increase on all monitors
```

---
## Monitor-Specific Control

All operations support an optional monitor name (2nd argument). **When no monitor is specified, the operation applies to ALL connected monitors.**

| Function | Signature |
|----------|-----------|
| `brightness.getMonitor(name)` | `(str) -> num` |
| `brightness.setMonitor(name, value)` | `(str, num) -> nil` |
| `brightness.getTemperatureMonitor(name)` | `(str) -> int` |
| `brightness.setTemperatureMonitor(name, kelvin)` | `(str, int) -> nil` |
| `brightness.getGammaMonitor(name)` | `(str) -> num` |
| `brightness.setGammaMonitor(name, value)` | `(str, num) -> nil` |
| `brightness.getShadowLiftMonitor(name)` | `(str) -> num` |
| `brightness.setShadowLiftMonitor(name, value)` | `(str, num) -> nil` |

```hv
brightness.setMonitor("HDMI-0", 0.8)              // Specific monitor
brightness.setTemperatureMonitor("DP-1", 5000)    // Specific monitor
brightness.set(0.7)                               // ALL monitors
brightness.setTemperature(4000)                   // ALL monitors
brightness.setShadowLift(1.0)                     // ALL monitors
brightness.setGamma(1.2)                          // ALL monitors
```

---

## Presets

```hv
brightness.presets()       // -> array of preset names
brightness.applyPreset(name)  // -> nil
```

```hv
brightness.presets()       // ["day", "night", "reading", "gaming"]
brightness.applyPreset("night")
```

---

## Step Configuration

```hv
brightness.step()          // -> num
brightness.setStep(value)  // -> nil
```

```hv
brightness.setStep(0.05)  // 5% steps for hotkeys
```

---

## Config Integration

The brightness module integrates with the config system:

```hv
brightness {
    step = 0.05
    current = 0.7
    temperature = 6500
    gamma = 1.0
}

monitor "HDMI-0" {
    brightness = 0.8
    temperature = 5500
}

monitor "DP-1" {
    brightness = 0.6
}
```

Access in scripts:
```hv
print(config.brightness.step)              // 0.05
print(config.monitor["HDMI-0"].brightness) // 0.8
```

---

## Implementation Details

- **Backend**: XRandR gamma ramps (via X11/FFI)
- **Native library**: `libgamma_ramp.so` for fast gamma ramp computation
- **Fallback**: Pure Havel gamma calculation if native lib not found
- **Caching**: Gamma ramps cached by monitor + parameters
- **Original gamma capture**: Saves original gamma on first use for restoration

---

## Example Usage

```hv
use brightness

# Basic control
print("Brightness: " + brightness.get())
brightness.set(0.7)

# Temperature
brightness.setTemperature(4500)  # Warm for evening
brightness.setTemperature(6500)  # Daylight

# Monitor-specific
brightness.setMonitor("HDMI-0", 0.8)
brightness.setMonitor("DP-1", 0.6)

# Hotkey integration
^+F1 => { brightness.decrease(0.1) }
^+F2 => { brightness.increase(0.1) }
^+F3 => { brightness.setTemperature(4000) }
^+F4 => { brightness.setTemperature(6500) }

# Presets
brightness.applyPreset("night")
```

---

**Previous:** [Window Module](/stdlib/window)
**Next:** [Hotkey Module →](/stdlib/hotkey)