---
title: "System Module"
description: "System information: platform, hardware, environment, and process info."
---

# System Module

```hv
use sys
```

**Source**: `src/havel-lang/stdlib/SysModule.cpp` (native `sys.*` host
functions) plus `modules/std/sys.hv`, whose convenience layer is merged
into the same namespace object. `use sys` therefore gives you both surfaces
under one name.

---

## System Info (sys.*)

### Platform & Architecture

| Function | Returns | Description |
|----------|---------|-------------|
| `sys.platform()` | `str` | "linux", "macos", "windows", "unknown" |
| `sys.arch()` | `str` | "x86_64", "aarch64", "x86", "arm", "unknown" |
| `sys.version()` | `str` | Havel version (e.g., "0.1.0") |

```hv
print(sys.platform())  // "linux"
print(sys.arch())      // "x86_64"
```

### Command Line & Environment

| Function | Returns | Description |
|----------|---------|-------------|
| `sys.argv()` | `array` | Command line arguments |
| `sys.env(name)` | `str?` | Get environment variable |
| `sys.envAll()` | `object` | All environment variables |

```hv
print(sys.argv())           // ["havel", "script.hv", "--flag"]
print(sys.env("HOME"))      // "/home/user"
print(sys.envAll().PATH)    // "/usr/bin:/bin"
```

### Process Info

| Function | Returns | Description |
|--------|---------|-------------|
| `sys.cwd()` | `str` | Current working directory |
| `sys.pid()` | `int` | Current process ID |
| `sys.ppid()` | `int` | Parent process ID |
| `sys.exit(code)` | `nil` | Exit with code |

```hv
print(sys.cwd())      // "/home/user/project"
print(sys.pid())      // 12345
print(sys.ppid())     // 1234
sys.exit(0)
```

### User & System

| Function | Returns | Description |
|--------|---------|-------------|
| `sys.hostname()` | `str` | Hostname |
| `sys.username()` | `str` | Username |
| `sys.home()` | `str` | Home directory |
| `sys.tmpdir()` | `str` | Temp directory |
| `sys.shell()` | `str` | User's shell |
| `sys.uptime()` | `num` | System uptime in seconds |

```hv
print(sys.hostname())   // "mycomputer"
print(sys.username())   // "user"
print(sys.home())       // "/home/user"
print(sys.uptime())     // 3600.5
```

---

## Convenience Layer (from `modules/std/sys.hv`)

These delegate to `system.detect()` and `system.hardware()`, and are only
reachable because the native registration merges the Havel module into the
`sys` namespace. Everything below is a **function** — call it.

| Function | Returns | Description |
|----------|---------|-------------|
| `sys.isLinux()` | `bool` | `sys.platform() == "Linux"` |
| `sys.isMac()` | `bool` | `sys.platform() == "MacOS"` |
| `sys.isWindows()` | `bool` | `sys.platform() == "Windows"` |
| `sys.isBSD()` | `bool` | `sys.platform() == "BSD"` |
| `sys.os()` | `str` | Detected OS: `"Linux"`, `"Windows"`, ... |
| `sys.kernel()` | `str` | Kernel release. |
| `sys.windowManager()` | `str` | **Window manager**, e.g. `"Cinnamon"`, `"GNOME"`, `"i3"` |
| `sys.desktopEnv()` | `str` | Raw `XDG_CURRENT_DESKTOP`. |
| `sys.displayProtocol()` | `str` | `"X11"`, `"Wayland"`, `"Unknown"` |
| `sys.detect()` | `obj` | The full `system.detect()` object. |
| `sys.hardware()` | `obj` | The full `system.hardware()` object. |
| `sys.cpu()` ... `sys.bios()` | various | One-liners over `system.hardware()`. |
| `sys.summary()` | `obj` | A formatted multi-line summary. |

```hv
use sys

// The short form of a platform branch:
if sys.isLinux() {
  print("${sys.os()} on ${sys.kernel()}, ${sys.displayProtocol()}")
  print("WM: ${sys.windowManager()}")     // "Cinnamon"  <- note the ()
}

// Without the parens this is the FUNCTION, so the method call yields null:
print("WM: ${sys.windowManager}")         // "WM: null"
```

### `windowManager` is the one people trip on

`sys.windowManager` is a **function**, so `sys.windowManager` without `()`
is the function object, and calling a string method on it silently yields
`null` rather than erroring. Write `sys.windowManager()`.

The full-fat alternative, which returns a plain value, is
`system.detect().windowManager`.

---

## Hardware shortcuts

| Function | Returns |
|----------|---------|
| `sys.cpu()` `sys.cpuCores()` `sys.cpuThreads()` `sys.cpuFreq()` `sys.cpuTemp()` | `str`/`int`/`float` |
| `sys.gpu()` | `str` |
| `sys.ramTotal()` `sys.ramUsed()` `sys.ramFree()` | `int` (bytes) |
| `sys.motherboard()` `sys.bios()` | `str` |
| `sys.storage()` | `array` of disk objects |

```hv
hw = sys.hardware()                       // or
print(sys.cpu(), sys.ramTotal() / 1024 / 1024 / 1024, "GB")
```

---

## JIT Module (jit.*)

| Function | Returns | Description |
|--------|---------|-------------|
| `jit.last_error()` | `str` | Last JIT compilation error |
| `jit.clear_error()` | `nil` | Clear JIT error |

```hv
print(jit.last_error())
jit.clear_error()
```

---

## System Detection (system.*)

### `system.detect()` -> object

Comprehensive system detection:

```hv
detected = system.detect()
print(detected.os)              // "Linux"
print(detected.kernel)          // "6.8.0-arch"
print(detected.arch)            // "x86_64"
print(detected.hostname)        // "mycomputer"
print(detected.displayProtocol) // "X11" or "Wayland"
print(detected.display)         // ":0"
print(detected.windowManager)   // "hyprland", "gnome", "i3", "kde plasma", etc.
print(detected.desktopEnv)      // "Hyprland", "GNOME", etc.
print(detected.uptime)          // 3600.5 (seconds)
print(detected.shell)           // "/bin/bash"
print(detected.user)            // "user"
print(detected.home)            // "/home/user"
```

### `system.hardware()` -> object

Detailed hardware information:

```hv
hw = system.hardware()
print(hw.cpu)              // "AMD Ryzen 9 7950X"
print(hw.cpuCores)         // 16
print(hw.cpuThreads)       // 32
print(hw.cpuFrequency)     // 4500.0 (MHz)
print(hw.cpuUsage)         // 0.0 (not implemented)
print(hw.gpu)              // "NVIDIA RTX 4090"
print(hw.gpuTemperature)   // 0.0 (not implemented)
print(hw.ramTotal)         // 65536000000 (bytes)
print(hw.ramUsed)          // 16384000000
print(hw.ramFree)          // 49152000000
print(hw.swapTotal)        // 8589934592
print(hw.swapUsed)         // 0
print(hw.swapFree)         // 8589934592
print(hw.motherboard)      // "ASUS ROG CROSSHAIR X670E HERO"
print(hw.bios)             // "3006"
print(hw.cpuTemperature)   // 45.0 (if available)
print(hw.storage)          // Array of disk objects
```

Storage array elements:
```hv
{
  name: "nvme0n1",
  model: "Samsung SSD 990 PRO 2TB",
  size: 2000398934016,
  used: 500000000000,
  free: 1500398934016,
  type: "NVMe",
  mountPoint: "/",
  filesystem: "ext4"
}
```

---

## Exit Cleanup

```hv
sys.registerExitCleanup(fn() {
    print("Cleaning up...")
})
```

Registers a function to run on exit.

---

## Example Usage

```hv
use sys

// Platform info
print("OS: " + sys.platform() + " " + sys.arch())

// Environment
home = sys.env("HOME") ?? sys.home()
print("Home: " + home)

// Process
print("PID: " + sys.pid())
print("PPID: " + sys.ppid())

// Hardware
hw = system.hardware()
print("CPU: " + hw.cpu + " (" + hw.cpuCores + " cores)")
print("RAM: " + (hw.ramTotal / 1024 / 1024 / 1024) + " GB")

// Detect
detected = system.detect()
print("WM: " + detected.windowManager + " (" + detected.displayProtocol + ")")
```

---

**Previous:** [Shell Module](/stdlib/shell)
**Next:** [Clipboard Module →](/stdlib/clipboard)
