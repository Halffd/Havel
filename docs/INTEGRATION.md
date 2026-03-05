# Havel Integration Guide

Integrating Havel into your Window Manager or Wayland Compositor.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Integration Options](#integration-options)
- [Step-by-Step: X11 WM Integration](#step-by-step-x11-wm-integration)
- [Step-by-Step: Wayland Compositor Integration](#step-by-step-wayland-compositor-integration)
- [Embedding the Interpreter](#embedding-the-interpreter)
- [Custom Builtins](#custom-builtins)
- [Hotkey Management](#hotkey-management)
- [Configuration](#configuration)
- [Examples](#examples)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Your WM / Compositor                     │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Hotkey     │  │   Window    │  │   Custom Builtins   │  │
│  │  Manager    │  │   Manager   │  │   (brightness, etc) │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                      │            │
│         └────────────────┼──────────────────────┘            │
│                          │                                   │
│                 ┌────────▼────────┐                          │
│                 │  Havel Runtime  │                          │
│                 │  (Interpreter)  │                          │
│                 └────────┬────────┘                          │
│                          │                                   │
│                 ┌────────▼────────┐                          │
│                 │  Havel Scripts  │                          │
│                 │  (.hv files)    │                          │
│                 └─────────────────┘                          │
└─────────────────────────────────────────────────────────────┘
```

---

## Integration Options

### Option 1: Full Integration (Recommended)

Embed Havel directly into your WM/compositor binary.

**Pros:**
- Single binary deployment
- Direct access to WM internals
- Best performance
- No IPC overhead

**Cons:**
- Requires recompilation for Havel updates
- Tighter coupling

### Option 2: Sidecar Process

Run Havel as a separate process that communicates with your WM.

**Pros:**
- Independent update cycles
- Cleaner separation of concerns
- Can restart Havel without restarting WM

**Cons:**
- IPC complexity
- Slight latency
- Two processes to manage

### Option 3: Hybrid

Embed core Havel runtime, run complex scripts as sidecar.

---

## Step-by-Step: X11 WM Integration

### 1. Add Havel as Dependency

```cmake
# In your CMakeLists.txt
add_subdirectory(/path/to/havel/src/havel-lang havel_lang)

target_link_libraries(your_wm PRIVATE havel_lang)
```

### 2. Initialize Havel Runtime

```cpp
// In your WM initialization code
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/Engine.hpp"

class MyWM {
public:
    void initialize() {
        // Create IO system
        io = std::make_unique<IO>();
        
        // Create Havel interpreter
        interpreter = std::make_unique<havel::Interpreter>(
            *io,
            *windowManager,
            hotkeyManager,
            brightnessManager,
            audioManager,
            nullptr,  // GUI manager (optional)
            nullptr,  // Screenshot manager (optional)
            nullptr,  // Clipboard manager (optional)
            nullptr,  // Pixel automation (optional)
            cliArgs
        );
        
        // Load and run startup script
        interpreter->runFile("~/.config/mywm/init.hv");
    }
    
private:
    std::unique_ptr<IO> io;
    std::unique_ptr<havel::Interpreter> interpreter;
};
```

### 3. Register Custom Builtins

```cpp
// Register WM-specific functions
interpreter->environment->Define(
    "wm.tile",
    havel::BuiltinFunction([this](const std::vector<havel::HavelValue> &args) {
        // Your tiling logic here
        windowManager->tileWindows();
        return havel::HavelValue(nullptr);
    })
);

interpreter->environment->Define(
    "wm.gap",
    havel::BuiltinFunction([this](const std::vector<havel::HavelValue> &args) {
        if (args.size() >= 1) {
            int gap = args[0].isNumber() ? args[0].asNumber() : 0;
            windowManager->setGapSize(gap);
        }
        return havel::HavelValue(nullptr);
    })
);
```

### 4. Hook Into X11 Events

```cpp
// In your X11 event loop
void MyWM::handleEvent(XEvent &ev) {
    switch (ev.type) {
    case KeyPress:
        // Let Havel handle hotkeys first
        if (hotkeyManager->handleX11Event(ev)) {
            return;  // Event consumed by Havel
        }
        // Fall through to WM handling
        break;
        
    case PropertyNotify:
        // Notify Havel of window changes
        interpreter->triggerEvent("window.property", ev.xproperty.window);
        break;
    }
    
    // Normal WM event handling
    handleWMEvent(ev);
}
```

### 5. Create Default Scripts

```havel
// ~/.config/mywm/init.hv

// Keybindings
Super+Return => { run("alacritty") }
Super+D => { run("rofi -show drun") }
Super+Q => { window.close() }

// Tiling
Super+T => { wm.tile() }
Super+F => { window.toggleFullscreen() }

// Gaps
Super+0 => { wm.gap(0) }
Super+1 => { wm.gap(10) }
Super+2 => { wm.gap(20) }

// Volume bindings
XF86AudioRaiseVolume => { audio.setVolume(audio.getVolume() + 5) }
XF86AudioLowerVolume => { audio.setVolume(audio.getVolume() - 5) }
XF86AudioMute => { audio.toggleMute() }

// Brightness
XF86MonBrightnessUp => { brightnessManager.setBrightness(brightnessManager.getBrightness() + 0.1) }
XF86MonBrightnessDown => { brightnessManager.setBrightness(brightnessManager.getBrightness() - 0.1) }
```

---

## Step-by-Step: Wayland Compositor Integration

### 1. For wlroots-based Compositors

```cpp
// In your compositor initialization
#include "havel-lang/runtime/Interpreter.hpp"

class MyCompositor {
public:
    void initialize() {
        // Create minimal IO (no X11)
        io = std::make_unique<IO>(IO::Mode::Wayland);
        
        // Create interpreter
        interpreter = std::make_unique<havel::Interpreter>(
            *io,
            *this,  // Your compositor implements WindowManager interface
            &hotkeyManager,
            nullptr,  // Brightness (use wlr-brightness or similar)
            nullptr,  // Audio (use libpipewire)
            nullptr, nullptr, nullptr, nullptr,
            cliArgs
        );
        
        // Load scripts
        interpreter->runFile("~/.config/mycompositor/init.hv");
    }
    
    // wlroots keyboard handlers
    void handleKeyPress(wlr_keyboard *keyboard, uint32_t keycode) {
        if (hotkeyManager->handleWaylandKey(keycode)) {
            return;  // Consumed by Havel
        }
        // Pass to client
        passKeyToFocusedClient(keycode);
    }
    
private:
    std::unique_ptr<IO> io;
    std::unique_ptr<havel::Interpreter> interpreter;
};
```

### 2. For Early-Stage Compositors (Minimal Integration)

If your compositor is still in early development, start with minimal integration:

```cpp
// Minimal integration - just script execution
class EarlyCompositor {
public:
    void runScript(const std::string &path) {
        havel::Interpreter interpreter({}, true);  // Pure mode
        interpreter.runFile(path);
    }
};
```

### 3. Expose Compositor State

```cpp
// Make compositor state accessible to scripts
interpreter->environment->Define(
    "compositor",
    havel::HavelValue(std::make_shared<std::unordered_map<std::string, havel::HavelValue>>(
        std::unordered_map<std::string, havel::HavelValue>{
            {"focusedClient", getClientValue(focusedClient)},
            {"outputCount", static_cast<double>(outputs.size())},
            {"activeLayout", currentLayout}
        }
    ))
);
```

---

## Embedding the Interpreter

### Minimal Embedding

```cpp
#include "havel-lang/runtime/Interpreter.hpp"

// Run a script without full WM integration
havel::Interpreter interpreter({}, true);  // Pure mode, no IO
interpreter.runFile("script.hv");
```

### Full Embedding

```cpp
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/Engine.hpp"

class EmbeddedHavel {
public:
    EmbeddedHavel() {
        io = std::make_unique<IO>();
        interpreter = std::make_unique<havel::Interpreter>(
            *io, windowManager, hotkeyManager,
            brightnessManager, audioManager,
            guiManager, screenshotManager,
            clipboardManager, pixelAutomation,
            cliArgs
        );
    }
    
    void runScript(const std::string &path) {
        interpreter->runFile(path);
    }
    
    void registerFunction(const std::string &name, std::function<havel::HavelResult(const std::vector<havel::HavelValue>&)> fn) {
        interpreter->environment->Define(name, havel::BuiltinFunction(fn));
    }
    
private:
    std::unique_ptr<IO> io;
    std::unique_ptr<havel::Interpreter> interpreter;
};
```

---

## Custom Builtins

### Simple Function

```cpp
interpreter->environment->Define(
    "mywm.notify",
    havel::BuiltinFunction([](const std::vector<havel::HavelValue> &args) {
        std::string title = args.size() > 0 ? args[0].asString() : "Notification";
        std::string message = args.size() > 1 ? args[1].asString() : "";
        
        // Send notification (using your WM's notification system)
        sendNotification(title, message);
        
        return havel::HavelValue(nullptr);
    })
);
```

### Function with Options

```cpp
interpreter->environment->Define(
    "mywm.moveWindow",
    havel::BuiltinFunction([this](const std::vector<havel::HavelValue> &args) {
        if (args.size() < 2) {
            return havel::HavelRuntimeError("moveWindow requires (x, y)");
        }
        
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        
        // Optional: target window
        havel::HavelValue target;
        if (args.size() >= 3) {
            target = args[2];
        } else {
            target = havel::HavelValue(windowManager->getActiveWindow());
        }
        
        windowManager->moveWindow(target.asWindow(), x, y);
        return havel::HavelValue(nullptr);
    })
);
```

### Async Function

```cpp
interpreter->environment->Define(
    "mywm.animate",
    havel::BuiltinFunction([this](const std::vector<havel::HavelValue> &args) {
        // Start animation in background
        std::thread([args, this]() {
            // Animation logic here
            for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
                // Update window position/size
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }).detach();
        
        return havel::HavelValue(nullptr);
    })
);
```

---

## Hotkey Management

### Using Havel's HotkeyManager

```cpp
// Let Havel manage hotkeys
hotkeyManager->registerHotkey("Super+Return", []() {
    runTerminal();
});

hotkeyManager->registerHotkey("Super+Q", []() {
    closeActiveWindow();
});
```

### Custom Hotkey Integration

```cpp
// Your WM manages hotkeys, Havel provides actions
class MyWM {
public:
    void bindHotkey(const std::string &keycombo, const std::string &script) {
        hotkeys[keycombo] = script;
    }
    
    void handleHotkey(const std::string &keycombo) {
        auto it = hotkeys.find(keycombo);
        if (it != hotkeys.end()) {
            // Execute Havel script
            interpreter->eval(it->second);
        }
    }
    
private:
    std::unordered_map<std::string, std::string> hotkeys;
};
```

### Conditional Hotkeys

```cpp
// Context-aware hotkeys
hotkeyManager->addConditionalHotkey(
    "Super+W",
    [this]() { return isWorkspaceMode; },  // Condition
    [this]() { switchWorkspace(); }         // Action
);
```

---

## Configuration

### Config File Location

```
~/.config/<your-wm>/
├── init.hv          # Main startup script
├── bindings.hv      # Keybindings
├── rules.hv         # Window rules
└── config.hv        # Configuration
```

### Loading Config

```cpp
void MyWM::loadConfig() {
    std::string configDir = getConfigDir();
    
    // Load in order
    interpreter->runFile(configDir + "config.hv");
    interpreter->runFile(configDir + "bindings.hv");
    interpreter->runFile(configDir + "rules.hv");
    interpreter->runFile(configDir + "init.hv");
}
```

### Hot Reloading

```cpp
// Watch config files for changes
interpreter->config->startFileWatching("init.hv");

// Or manually reload
void MyWM::reloadConfig() {
    interpreter->config->reload();
    loadConfig();  // Re-run scripts
}
```

---

## Examples

### Complete X11 WM Integration

See `src/main.cpp` in the Havel repository for a complete example.

### Minimal Wayland Compositor

```cpp
#include <wayland-server-core.h>
#include "havel-lang/runtime/Interpreter.hpp"

struct my_compositor {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    havel::Interpreter *interpreter;
};

static bool handle_key(struct my_compositor *comp, xkb_keysym_t sym) {
    // Let Havel handle hotkeys
    if (comp->interpreter->handleKey(sym)) {
        return true;
    }
    return false;
}
```

### Early Compositor Scripting

```havel
// For early-stage compositors - basic scripting only

// Output configuration
output "HDMI-A-1" {
    mode "1920x1080@60"
    position 0 0
}

// Basic keybindings
Super+Return => run("foot")
Super+Escape => compositor.exit()

// Window rules
onWindowCreate => {
    if window.class == "firefox" {
        window.moveToOutput("HDMI-A-1")
    }
}
```

---

## Troubleshooting

### Havel Not Finding My Builtins

Ensure builtins are registered before running scripts:

```cpp
// WRONG
interpreter->runFile("init.hv");  // Script can't find mywm.notify
registerBuiltin("mywm.notify", ...);

// RIGHT
registerBuiltin("mywm.notify", ...);
interpreter->runFile("init.hv");  // Works!
```

### Hotkeys Not Working

Check that your event loop passes events to Havel:

```cpp
// In X11 event loop
if (hotkeyManager->handleX11Event(ev)) {
    return;  // Event consumed
}
```

### Config Not Reloading

Ensure file watching is started:

```cpp
interpreter->config->startFileWatching("init.hv");
```

---

## API Reference

### Interpreter Methods

| Method | Description |
|--------|-------------|
| `runFile(path)` | Execute a script file |
| `eval(code)` | Execute code string |
| `registerBuiltin(name, fn)` | Register custom function |
| `handleKey(keycode)` | Process keyboard event |
| `triggerEvent(name, data)` | Fire custom event |

### Config Methods

| Method | Description |
|--------|-------------|
| `get(key, default)` | Get config value |
| `set(key, value, save)` | Set config value |
| `startFileWatching(file)` | Watch for changes |
| `reload()` | Reload config file |
| `requestSave()` | Debounced save |
| `forceSave()` | Immediate save |

---

## License

Havel is licensed under the MIT License. See LICENSE for details.
