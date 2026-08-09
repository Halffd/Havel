---
title: "System Module"
description: "System information: platform, hardware, environment, and process info."
---

# System Module

```hv
use sys
```

**Source**: `src/havel-lang/stdlib/SysModule.cpp`

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
| `sys.exit(code)` | `never` | Exit with code |

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
**Next:** [Network/HTTP Module →](/stdlib/network)