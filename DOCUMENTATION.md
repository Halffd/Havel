# HvC Project Documentation

## 1. Project Overview

HvC (Hotkey vs Control) is a comprehensive system for managing hotkeys, window control, and media playback on Linux systems. It provides a unified interface for controlling various aspects of the desktop environment through customizable hotkeys and scripting capabilities.

## 2. Class Reference

### Core Classes

#### HotkeyManager
**Purpose**: Central component for registering and managing hotkeys throughout the system.
**Key Features**:
- Registration of global and contextual hotkeys
- Support for multiple modifier keys
- Context-aware hotkey activation based on window properties
- Mode-based hotkey profiles (default, gaming, etc.)
**Key Methods**:
- `RegisterDefaultHotkeys()`: Sets up standard system hotkeys
- `AddHotkey(string, callback)`: Registers a new hotkey with callback
- `AddContextualHotkey(string, condition, trueAction, falseAction)`: Registers a context-dependent hotkey
- `evaluateCondition(string)`: Evaluates a condition string for contextual hotkeys

#### IO
**Purpose**: Provides low-level input/output functionality for keyboard and mouse events.
**Key Features**:
- Keyboard event handling and simulation
- Mouse movement and button control
- Hotkey registration and monitoring
- Timer functionality
**Key Methods**:
- `Send(Key, bool)`: Sends a key press or release event
- `Send(string)`: Sends a string of characters
- `MouseMove(int, int)`: Moves the mouse cursor
- `MouseClick(int)`: Simulates a mouse button click
- `Hotkey(string, function)`: Registers a hotkey with callback

#### ConfigManager
**Purpose**: Manages configuration settings for the application.
**Key Features**:
- JSON-based configuration storage
- Default value fallbacks
- Configuration change notifications
**Key Methods**:
- `Load(string)`: Loads configuration from a file
- `Get<T>(string, T)`: Gets a typed configuration value with default
- `Set<T>(string, T)`: Sets a configuration value
- `Watch<T>(string, function)`: Watches for configuration changes

#### ScriptEngine
**Purpose**: Provides Lua scripting capabilities for extending functionality.
**Key Features**:
- Lua script loading and execution
- API binding for system functions
- Hotkey registration from scripts
**Key Methods**:
- `LoadScript(string)`: Loads and executes a Lua script file
- `ExecuteString(string)`: Executes a Lua code string
- `AddHotkey(string, string)`: Registers a hotkey with a Lua action

#### MPVController
**Purpose**: Provides an interface for controlling media playback.
**Key Features**:
- Basic playback control (play, pause, stop)
- Volume adjustment and muting
- Seeking and position control
**Key Methods**:
- `Play()`, `Pause()`, `TogglePause()`: Playback control
- `SetVolume(int)`, `VolumeUp()`, `VolumeDown()`: Volume control
- `Seek(int)`: Seeks to a position in the current media

#### BrightnessManager
**Purpose**: Controls display brightness and gamma settings.
**Key Features**:
- Brightness adjustment
- Gamma control
- Preset management
**Key Methods**:
- `setBrightness(float)`: Sets display brightness
- `setGamma(float)`: Sets display gamma
- `setStartupValues()`: Sets initial brightness and gamma values

#### SocketServer
**Purpose**: Provides network communication for remote control.
**Key Features**:
- TCP socket server
- Command parsing and execution
- Asynchronous operation
**Key Methods**:
- `Start()`: Starts the socket server
- `Stop()`: Stops the socket server
- `HandleCommand(string)`: Processes received commands

#### AutoClicker
**Purpose**: Provides automated mouse clicking functionality.
**Key Features**:
- Configurable click intervals
- Multiple click patterns
- Conditional activation
**Key Methods**:
- `Start(int, int)`: Starts auto-clicking at specified interval
- `Stop()`: Stops auto-clicking
- `IsRunning()`: Checks if auto-clicker is active

#### EmergencySystem
**Purpose**: Provides safety mechanisms for critical failures.
**Key Features**:
- Crash detection and recovery
- Emergency shutdown procedures
- System state preservation
**Key Methods**:
- `RegisterEmergencyHandler(function)`: Registers emergency handler
- `TriggerEmergency(int)`: Triggers emergency procedure
- `ResetEmergencyState()`: Resets emergency state

#### SequenceDetector
**Purpose**: Detects sequences of key presses.
**Key Features**:
- Pattern recognition
- Timeout handling
- Callback execution on match
**Key Methods**:
- `AddSequence(vector<Key>, function)`: Adds a key sequence to detect
- `ProcessKey(Key)`: Processes a key press for sequence detection
- `Reset()`: Resets the sequence detector state

#### MacroSystem
**Purpose**: Records and plays back input sequences.
**Key Features**:
- Input recording
- Playback with timing
- Macro storage and management
**Key Methods**:
- `StartRecording()`: Begins recording a macro
- `StopRecording()`: Stops recording and saves the macro
- `PlayMacro(string)`: Plays back a saved macro

#### MouseGesture
**Purpose**: Detects and processes mouse gestures.
**Key Features**:
- Gesture pattern recognition
- Direction-based actions
- Customizable gesture mappings
**Key Methods**:
- `StartTracking()`: Begins tracking mouse movement
- `StopTracking()`: Stops tracking and evaluates gesture
- `RegisterGesture(string, function)`: Registers a gesture pattern

### Window Management Classes

#### WindowManager
**Purpose**: Provides functionality for detecting, manipulating, and tracking windows.
**Key Features**:
- Active window detection and information retrieval
- Window movement, resizing, and positioning
- Multi-monitor support
**Key Methods**:
- `GetActiveWindow()`: Returns the currently active window
- `FindByClass(string)`: Finds a window by its class name
- `MoveWindow(int, int)`: Moves a window in a specified direction
- `ToggleAlwaysOnTop()`: Toggles the always-on-top state of a window

#### WindowRules
**Purpose**: Manages rule-based window behavior configuration.
**Key Features**:
- Condition-based window matching
- Automatic window actions
- Rule priority management
**Key Methods**:
- `AddRule(string, function)`: Adds a window rule
- `EvaluateRules(Window)`: Evaluates rules for a window
- `ClearRules()`: Clears all window rules

#### WindowManagerDetector
**Purpose**: Detects the current window manager type.
**Key Features**:
- Window manager identification
- Feature compatibility checking
- Environment detection
**Key Methods**:
- `Detect()`: Detects the current window manager
- `IsSupported()`: Checks if the window manager is supported
- `GetWMType()`: Returns the window manager type

#### Window
**Purpose**: Represents a window in the system.
**Key Features**:
- Window properties and state
- Window manipulation methods
- Event handling
**Key Methods**:
- `Move(int, int)`: Moves the window
- `Resize(int, int)`: Resizes the window
- `Focus()`: Brings the window to focus

#### WindowMonitor
**Purpose**: Monitors window creation and destruction events.
**Key Features**:
- Window event detection
- Callback notification system
- Window state tracking
**Key Methods**:
- `StartMonitoring()`: Begins monitoring window events
- `StopMonitoring()`: Stops monitoring window events
- `RegisterCallback(function)`: Registers an event callback

### GUI Classes

#### GUI
**Purpose**: Provides a graphical user interface framework.
**Key Features**:
- Window creation and management
- Widget hierarchy
- Event handling
**Key Methods**:
- `CreateWindow(string, int, int)`: Creates a new window
- `AddWidget(Widget)`: Adds a widget to the interface
- `ProcessEvents()`: Processes GUI events

#### Widget
**Purpose**: Base class for GUI elements.
**Key Features**:
- Position and size properties
- Event handling
- Rendering
**Key Methods**:
- `SetPosition(int, int)`: Sets widget position
- `SetSize(int, int)`: Sets widget size
- `Render()`: Renders the widget

#### Button
**Purpose**: Interactive button widget.
**Key Features**:
- Click events
- Visual states (normal, hover, pressed)
- Label and icon support
**Key Methods**:
- `SetLabel(string)`: Sets button label
- `SetCallback(function)`: Sets click callback
- `SetEnabled(bool)`: Enables or disables the button

#### Label
**Purpose**: Text display widget.
**Key Features**:
- Text rendering
- Font and color customization
- Text alignment
**Key Methods**:
- `SetText(string)`: Sets label text
- `SetFont(string, int)`: Sets font and size
- `SetAlignment(int)`: Sets text alignment

#### Screen
**Purpose**: Represents a display screen.
**Key Features**:
- Screen properties and capabilities
- Multi-monitor support
- Resolution and DPI information
**Key Methods**:
- `GetResolution()`: Gets screen resolution
- `GetDPI()`: Gets screen DPI
- `GetWorkArea()`: Gets available work area

#### Theme
**Purpose**: Manages GUI theming.
**Key Features**:
- Color schemes
- Widget styling
- Theme switching
**Key Methods**:
- `LoadTheme(string)`: Loads a theme from file
- `SetColor(string, Color)`: Sets a theme color
- `GetColor(string)`: Gets a theme color

#### ImageHandler
**Purpose**: Manages image loading and rendering.
**Key Features**:
- Image loading from files
- Image scaling and transformation
- Rendering to GUI
**Key Methods**:
- `LoadImage(string)`: Loads an image from file
- `ScaleImage(Image, int, int)`: Scales an image
- `RenderImage(Image, int, int)`: Renders an image

#### Clipboard
**Purpose**: Manages clipboard operations.
**Key Features**:
- Text and image clipboard access
- Copy and paste operations
- Format conversion
**Key Methods**:
- `SetText(string)`: Sets clipboard text
- `GetText()`: Gets clipboard text
- `HasFormat(int)`: Checks if clipboard has a specific format

### Utility Classes

#### Logger
**Purpose**: Provides a logging system for debugging and diagnostics.
**Key Features**:
- Multiple log levels
- File and console output
- Timestamp and category support
**Key Methods**:
- `info(string)`: Logs an info message
- `error(string)`: Logs an error message
- `debug(string)`: Logs a debug message

#### Notifier
**Purpose**: Provides desktop notification functionality.
**Key Features**:
- System notification integration
- Customizable appearance
- Timeout and action support
**Key Methods**:
- `ShowNotification(string, string)`: Shows a notification
- `ShowActionNotification(string, string, function)`: Shows a notification with action
- `CloseNotification(int)`: Closes a notification

#### Util/Utils
**Purpose**: Provides common utility functions.
**Key Features**:
- String manipulation
- File operations
- Conversion utilities
**Key Methods**:
- `StringToLower(string)`: Converts string to lowercase
- `StringSplit(string, char)`: Splits a string
- `FileExists(string)`: Checks if a file exists

#### Rect
**Purpose**: Represents a rectangle with position and size.
**Key Features**:
- Coordinate system
- Intersection and containment testing
- Transformation operations
**Key Methods**:
- `Contains(int, int)`: Checks if point is in rectangle
- `Intersects(Rect)`: Checks if rectangles intersect
- `Union(Rect)`: Returns union of rectangles

#### Printer
**Purpose**: Provides formatted text output capabilities.
**Key Features**:
- Formatted text output
- Color and style support
- Output redirection
**Key Methods**:
- `Print(string)`: Prints text
- `PrintFormatted(string, ...)`: Prints formatted text
- `SetOutputTarget(int)`: Sets output target

## 3. Project Architecture

### 3.1 Core Components

1. **Core System (`src/core/`)**
   - **HotkeyManager**: Central component for registering and managing hotkeys
   - **IO**: Low-level input/output handling for keyboard and mouse events
   - **ConfigManager**: Configuration loading and management
   - **ScriptEngine**: Lua-based scripting for custom functionality
   - **MPVController**: Media player control interface
   - **BrightnessManager**: Display brightness and gamma control
   - **SocketServer**: Network communication for remote control
   - **AutoClicker**: Automated mouse clicking functionality
   - **EmergencySystem**: Safety mechanisms for critical failures
   - **SequenceDetector**: Detection of key sequences
   - **MacroSystem**: Recording and playback of input sequences

2. **Window Management (`src/window/`)**
   - **WindowManager**: Window detection, manipulation, and state tracking
   - **WindowRules**: Rule-based window behavior configuration
   - **WindowManagerDetector**: Detection of the current window manager type

3. **Media Control (`src/media/`)**
   - **MPVController**: Interface to the MPV media player
   - Media hotkey handling for playback control
   - Volume and playback position management

4. **GUI System (`src/gui/`)**
   - GTK-based user interface
   - Theme management and customization
   - Configuration dialogs and settings panels

5. **Utility Layer (`src/utils/`)**
   - **Logger**: Comprehensive logging system
   - **Notifier**: Desktop notification system
   - **Utils**: Common utility functions and helpers

### 3.2 Design Patterns

1. **Singleton Pattern**
   - Used for global managers (e.g., `WindowManager`, `HotkeyManager`)
   - Implemented using static instance methods
   - Thread-safe initialization for concurrent access

2. **Observer Pattern**
   - Used for event handling and notification
   - Callback-based system for responding to state changes
   - Event queue management for asynchronous processing

3. **Factory Pattern**
   - Used for creating window objects
   - Platform-specific implementations behind common interfaces
   - Abstract base classes for extensibility

4. **Command Pattern**
   - Used for hotkey actions and callbacks
   - Encapsulates operations as objects
   - Supports undo/redo functionality

## 4. Core Components in Detail

### 4.1 HotkeyManager

The `HotkeyManager` class is responsible for registering, managing, and triggering hotkeys throughout the system.

#### Key Features:
- Registration of global and contextual hotkeys
- Support for multiple modifier keys
- Context-aware hotkey activation based on window properties
- Mode-based hotkey profiles (default, gaming, etc.)
- Debug settings for troubleshooting

#### Key Methods:
- `RegisterDefaultHotkeys()`: Sets up standard system hotkeys
- `RegisterMediaHotkeys()`: Sets up media control hotkeys
- `RegisterWindowHotkeys()`: Sets up window management hotkeys
- `AddHotkey(string, callback)`: Registers a new hotkey with callback
- `AddContextualHotkey(string, condition, trueAction, falseAction)`: Registers a context-dependent hotkey
- `evaluateCondition(string)`: Evaluates a condition string for contextual hotkeys

### 4.2 IO System

The `IO` class provides low-level input/output functionality for keyboard and mouse events.

#### Key Features:
- Keyboard event handling and simulation
- Mouse movement and button control
- Hotkey registration and monitoring
- Timer functionality

#### Key Methods:
- `Send(Key, bool)`: Sends a key press or release event
- `Send(string)`: Sends a string of characters
- `MouseMove(int, int)`: Moves the mouse cursor
- `MouseClick(int)`: Simulates a mouse button click
- `Hotkey(string, function)`: Registers a hotkey with callback
- `SetTimer(int, function)`: Sets a timed callback

### 4.3 WindowManager

The `WindowManager` class provides functionality for detecting, manipulating, and tracking windows.

#### Key Features:
- Active window detection and information retrieval
- Window movement, resizing, and positioning
- Multi-monitor support
- Window grouping and classification

#### Key Methods:
- `GetActiveWindow()`: Returns the currently active window
- `FindByClass(string)`: Finds a window by its class name
- `FindByTitle(string)`: Finds a window by its title
- `MoveWindow(int, int)`: Moves a window in a specified direction
- `ResizeWindow(int, int)`: Resizes a window
- `ToggleAlwaysOnTop()`: Toggles the always-on-top state of a window

### 4.4 ScriptEngine

The `ScriptEngine` class provides Lua scripting capabilities for extending functionality.

#### Key Features:
- Lua script loading and execution
- API binding for system functions
- Hotkey registration from scripts
- Contextual script execution

#### Key Methods:
- `LoadScript(string)`: Loads and executes a Lua script file
- `ExecuteString(string)`: Executes a Lua code string
- `AddHotkey(string, string)`: Registers a hotkey with a Lua action
- `AddContextualHotkey(string, string, string)`: Registers a context-dependent hotkey

### 4.5 MPVController

The `MPVController` class provides an interface for controlling media playback.

#### Key Features:
- Basic playback control (play, pause, stop)
- Volume adjustment and muting
- Seeking and position control
- File loading and information retrieval

#### Key Methods:
- `Play()`, `Pause()`, `TogglePause()`: Playback control
- `SetVolume(int)`, `VolumeUp()`, `VolumeDown()`: Volume control
- `Seek(int)`: Seeks to a position in the current media
- `LoadFile(string)`: Loads a media file for playback

## 5. Hotkey System

### 5.1 Hotkey Registration

The hotkey system allows for registering global and contextual hotkeys.

#### Global Hotkeys:
```cpp
// Register a global hotkey with a callback function
hotkeyManager.AddHotkey("ctrl+shift+a", []() {
    // Action to perform when hotkey is triggered
    WindowManager::ToggleAlwaysOnTop();
});

// Register a global hotkey with an action string
hotkeyManager.AddHotkey("ctrl+shift+m", "ToggleMute");
```

#### Contextual Hotkeys:
```cpp
// Register a contextual hotkey that only works in specific windows
hotkeyManager.AddContextualHotkey("ctrl+b", "Window.Active('class:Firefox')",
    []() {
        // Action when condition is true
        std::cout << "Firefox is active" << std::endl;
    },
    []() {
        // Action when condition is false
        std::cout << "Firefox is not active" << std::endl;
    }
);
```

### 5.2 Hotkey Format

Hotkeys are specified using a string format:

- Modifier keys: `^` (Ctrl), `!` (Alt), `+` (Shift), `#` (Super/Win)
- Special keys: `F1`-`F12`, `Tab`, `Escape`, `Space`, etc.
- Mouse buttons: `Button1` (LMB), `Button2` (RMB), `Button3` (MMB)
- Key combinations: `^+a` (Ctrl+Shift+A)

### 5.3 Window Conditions

Window conditions are used for contextual hotkeys:

- `Window.Active('title:WindowName')`: Checks if the active window title contains "WindowName"
- `Window.Active('class:ClassName')`: Checks if the active window class is "ClassName"
- `currentMode == 'gaming'`: Checks if the current mode is set to "gaming"

## 5. Configuration System

### 5.1 Configuration File

The system uses a JSON-based configuration file (`config.json`) for storing settings.

#### Example Configuration:
```json
{
    "Window": {
        "MoveSpeed": 10
    },
    "Network": {
        "Port": 8765
    },
    "UI": {
        "Theme": "dark"
    },
    "Hotkeys": {
        "MediaControls": true,
        "WindowControls": true
    }
}
```

### 5.2 Accessing Configuration

Configuration values can be accessed using the `Configs` singleton:

```cpp
// Get a configuration value with a default fallback
int moveSpeed = havel::Configs::Get().Get<int>("Window.MoveSpeed", 10);

// Watch for configuration changes
havel::Configs::Get().Watch<std::string>("UI.Theme", [](auto oldVal, auto newVal) {
    lo.info("Theme changed from " + oldVal + " to " + newVal);
});
```

## 6. Testing Framework

### 6.1 Test Structure

Tests are located in the `src/tests/` directory and focus on specific components.

#### Example Test:
```cpp
// Test for hotkey functionality
void test_volume_up() {
    std::cout << "Volume Up hotkey triggered!" << std::endl;
}

int main() {
    // Initialize the IO system
    havel::IO io;
    
    // Register test hotkeys
    io.Hotkey("^F10", test_volume_up);
    
    // Main loop for testing
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### 6.2 Running Tests

Tests can be run individually or as part of a test suite:

```bash
# Run a specific test
./hotkey_test

# Run all tests
./run_tests.sh
```

## 7. Build System

### 7.1 Dependencies

The project requires the following dependencies:

- X11 development libraries
- GTK3 development libraries
- Lua development libraries
- MPV development libraries

### 7.2 Build Instructions

```bash
# Install dependencies
sudo apt-get install libx11-dev libgtk-3-dev liblua5.3-dev libmpv-dev

# Build the project
mkdir build && cd build
cmake ..
make
```

## 8. Extending the System

### 8.1 Adding New Hotkeys

To add new hotkeys to the system:

1. Define a callback function for the hotkey action
2. Register the hotkey using `HotkeyManager::AddHotkey()`
3. Optionally, add the hotkey to the configuration file

### 8.2 Adding New Window Rules

To add new window rules:

1. Define a condition function that checks window properties
2. Create a rule using `WindowRules::AddRule()`
3. Specify actions to take when the rule is matched

### 8.3 Creating Scripts

To create custom scripts:

1. Create a Lua script file with desired functionality
2. Use the provided API functions to interact with the system
3. Load the script using `ScriptEngine::LoadScript()`

## 9. Debugging and Troubleshooting

### 9.1 Logging

The system uses a comprehensive logging system for debugging:

```cpp
lo.info("Starting HvC...");
lo.error("Failed to initialize: " + errorMessage);
lo.debug("Hotkey registered: " + hotkeyStr);
```

### 9.2 Debug Settings

Debug settings can be enabled for troubleshooting:

```cpp
// Enable verbose logging for key events
hotkeyManager.setVerboseKeyLogging(true);

// Enable verbose logging for window events
hotkeyManager.setVerboseWindowLogging(true);

// Enable verbose logging for condition evaluation
hotkeyManager.setVerboseConditionLogging(true);
```

## 10. Version Control and Contribution Guidelines

### 10.1 Git Workflow

1. Use feature branches for development
2. Create pull requests for code review
3. Follow the project's coding style and conventions
4. Include tests for new functionality
5. Update documentation for significant changes

### 10.2 Code Review Process

1. All code changes must be reviewed before merging
2. Reviewers should check for:
   - Code quality and style
   - Test coverage
   - Documentation updates
   - Performance considerations
   - Security implications

## 11. License and Credits

HvC is released under the [LICENSE] license.

## 12. Appendix

### 12.1 Key Mapping Reference

The system uses the following key mapping for hotkeys:

| Symbol | Modifier Key |
|--------|------------|
| ^      | Ctrl         |
| !      | Alt          |
| +      | Shift        |
| #      | Super/Win    |

### 12.2 Common Window Classes

| Application | Window Class    |
|-------------|----------------|
| Firefox     | Firefox-esr    |
| Chrome      | Google-chrome  |
| Terminal    | XTerm          |
| File Manager| Nautilus       |

### 12.3 Configuration Reference

| Configuration Key    | Type   | Description                   |
|----------------------|--------|-------------------------------|
| Window.MoveSpeed     | int    | Speed for window movement     |
| Network.Port         | int    | Port for socket server        |
| UI.Theme             | string | UI theme (light/dark)         |
| Hotkeys.MediaControls| bool   | Enable media control hotkeys  |

## 13. Implementation Details

### 13.1 X11 Integration

The HvC system integrates deeply with the X11 window system on Linux to provide window management and hotkey functionality.

#### Key Binding Implementation

```cpp
void IO::GrabKeyWithVariants(KeyCode keycode, unsigned int modifiers, Window root, bool exclusive) {
    // Grab the key with the exact modifiers
    XGrabKey(display, keycode, modifiers, root, False, GrabModeAsync, GrabModeAsync);
    
    // Also grab with Num Lock (Mod2Mask)
    XGrabKey(display, keycode, modifiers | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
    
    // Also grab with Caps Lock (LockMask)
    XGrabKey(display, keycode, modifiers | LockMask, root, False, GrabModeAsync, GrabModeAsync);
    
    // Also grab with both Num Lock and Caps Lock
    XGrabKey(display, keycode, modifiers | Mod2Mask | LockMask, root, False, GrabModeAsync, GrabModeAsync);
}
```

#### Window Detection

The system uses X11 properties to detect and identify windows:

```cpp
XWindow WindowManager::FindByClass(cstr className) {
    Display* display = XOpenDisplay(NULL);
    if (!display) return 0;
    
    Window root = DefaultRootWindow(display);
    Window rootReturn, parentReturn, *children;
    unsigned int numChildren;
    
    XQueryTree(display, root, &rootReturn, &parentReturn, &children, &numChildren);
    
    for (unsigned int i = 0; i < numChildren; i++) {
        XClassHint classHint;
        if (XGetClassHint(display, children[i], &classHint)) {
            if (std::string(classHint.res_class) == className) {
                XFree(classHint.res_name);
                XFree(classHint.res_class);
                XFree(children);
                return children[i];
            }
            XFree(classHint.res_name);
            XFree(classHint.res_class);
        }
    }
    
    XFree(children);
    XCloseDisplay(display);
    return 0;
}
```

### 13.2 Lua Scripting Engine

The scripting engine uses the Sol2 library to provide Lua integration:

```cpp
ScriptEngine::ScriptEngine(IO& io, WindowManager& windowManager) : io(io), windowManager(windowManager) {
    // Initialize Lua environment
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);
    
    // Register API functions
    RegisterFunctions();
}

void ScriptEngine::RegisterFunctions() {
    // Register IO functions
    lua.set_function("send", [this](const std::string& keys) { io.Send(keys); });
    lua.set_function("mouseMove", [this](int x, int y) { io.MouseMove(x, y); });
    lua.set_function("mouseClick", [this](int button) { io.MouseClick(button); });
    
    // Register window functions
    lua.set_function("getActiveWindow", &WindowManager::GetActiveWindow);
    lua.set_function("findWindow", &WindowManager::Find);
    lua.set_function("moveWindow", &WindowManager::MoveWindow);
    
    // Register utility functions
    lua.set_function("sleep", [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); });
    lua.set_function("print", [](const std::string& msg) { std::cout << msg << std::endl; });
}
```

### 13.3 Hotkey Parsing

The system parses hotkey strings to convert them into key codes and modifiers:

```cpp
std::string HotkeyManager::parseHotkeyString(const std::string& hotkeyStr) {
    std::string result;
    int modifiers = 0;
    
    // Check for modifiers
    if (hotkeyStr.find("^") != std::string::npos) modifiers |= ControlMask;
    if (hotkeyStr.find("!") != std::string::npos) modifiers |= Mod1Mask; // Alt
    if (hotkeyStr.find("+") != std::string::npos) modifiers |= ShiftMask;
    if (hotkeyStr.find("#") != std::string::npos) modifiers |= Mod4Mask; // Super/Win
    
    // Extract the key part (last character or special key name)
    std::string keyPart;
    size_t lastModPos = hotkeyStr.find_last_of("^!+#");
    
    if (lastModPos != std::string::npos) {
        keyPart = hotkeyStr.substr(lastModPos + 1);
    } else {
        keyPart = hotkeyStr;
    }
    
    // Convert key name if needed
    std::string keyName = convertKeyName(keyPart);
    
    // Format result
    result = std::to_string(modifiers) + "," + keyName;
    return result;
}
```

## 14. Advanced Usage

### 14.1 Custom Hotkey Configurations

You can create custom hotkey configurations using JSON files:

```json
{
    "Hotkeys": [
        {
            "Key": "ctrl+alt+t",
            "Action": "LaunchTerminal",
            "Condition": "currentMode == 'default'"
        },
        {
            "Key": "ctrl+shift+g",
            "Action": "ToggleGameMode",
            "Condition": ""
        },
        {
            "Key": "alt+1",
            "Action": "SwitchToWorkspace1",
            "Condition": "Window.Active('class:Firefox')"
        }
    ]
}
```

### 14.2 Lua Script Examples

#### Window Management Script

```lua
-- window_manager.lua
-- Script for managing window positions and sizes

-- Define monitor layouts
monitors = {
    primary = {x = 0, y = 0, width = 1920, height = 1080},
    secondary = {x = 1920, y = 0, width = 1920, height = 1080}
}

-- Function to move a window to a specific monitor
function moveToMonitor(monitorName)
    local monitor = monitors[monitorName]
    if not monitor then
        print("Monitor not found: " .. monitorName)
        return
    end
    
    local window = getActiveWindow()
    if window == 0 then
        print("No active window")
        return
    end
    
    -- Center the window on the monitor
    local x = monitor.x + (monitor.width / 2) - 400
    local y = monitor.y + (monitor.height / 2) - 300
    moveWindow(window, x, y)
    resizeWindow(window, 800, 600)
    
    print("Moved window to " .. monitorName)
end

-- Function to tile windows on the current monitor
function tileWindows()
    -- Implementation of window tiling logic
    print("Tiling windows")
    -- ...
end

-- Register hotkeys
registerHotkey("ctrl+alt+1", function() moveToMonitor("primary") end)
registerHotkey("ctrl+alt+2", function() moveToMonitor("secondary") end)
registerHotkey("ctrl+alt+t", tileWindows)
```

#### Automation Script

```lua
-- automation.lua
-- Script for automating repetitive tasks

-- Function to automate a login sequence
function autoLogin(username, password)
    -- Find the login window
    local loginWindow = findWindow("class:LoginDialog")
    if loginWindow == 0 then
        print("Login window not found")
        return
    end
    
    -- Focus the window
    focusWindow(loginWindow)
    sleep(500)  -- Wait for window to gain focus
    
    -- Enter username
    send("tab")  -- Focus username field
    sleep(100)
    send(username)
    
    -- Enter password
    send("tab")  -- Move to password field
    sleep(100)
    send(password)
    
    -- Submit form
    send("return")
    
    print("Login sequence completed")
end

-- Register hotkey to trigger the login sequence
registerHotkey("ctrl+alt+l", function() autoLogin("user", "password") end)
```

### 14.3 Advanced Window Rules

You can create complex window rules using the WindowRules class:

```cpp
// Add rules for specific applications
WindowRules::AddRule("class:Firefox", [](XWindow window) {
    // Always open Firefox on the second monitor
    WindowManager::SendToMonitor(window, 1);
    
    // Make it take up the right half of the screen
    WindowManager::SnapWindowWithPadding(window, WindowManager::RIGHT, 10);
    
    // Set specific properties
    WindowManager::SetAlwaysOnTop(window, false);
});

// Add rules for dialog windows
WindowRules::AddRule("type:Dialog", [](XWindow window) {
    // Center dialog windows
    WindowManager::CenterWindow(window);
    
    // Make them always on top
    WindowManager::SetAlwaysOnTop(window, true);
});
```

## 15. Performance Optimization

### 15.1 Event Handling

The HvC system uses an optimized event handling approach to minimize CPU usage:

```cpp
void IO::MonitorHotkeys() {
    XEvent event;
    
    while (true) {
        // Wait for an event instead of polling
        XNextEvent(display, &event);
        
        // Process only key events
        if (event.type == KeyPress || event.type == KeyRelease) {
            HandleKeyEvent(event);
        } else if (event.type == ButtonPress || event.type == ButtonRelease) {
            HandleMouseEvent(event);
        }
        
        // Short sleep to prevent CPU hogging
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

### 15.2 Memory Management

The system uses smart pointers and RAII principles to manage resources:

```cpp
// Use of shared_ptr for component management
auto windowManager = std::make_shared<WindowManager>();
auto hotkeyManager = std::make_shared<HotkeyManager>(*io, *windowManager, *mpv, *scriptEngine);

// RAII for X11 resources
class XResourceGuard {
public:
    XResourceGuard(Display* display, XID resource) : display(display), resource(resource) {}
    ~XResourceGuard() {
        if (display && resource) {
            XFree(resource);
        }
    }
private:
    Display* display;
    XID resource;
};
```

### 15.3 Conditional Hotkey Evaluation

The system optimizes conditional hotkey evaluation by caching results:

```cpp
bool HotkeyManager::evaluateCondition(const std::string& condition) {
    // Check cache first
    auto it = conditionCache.find(condition);
    if (it != conditionCache.end()) {
        // Only re-evaluate if cache timeout has expired
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.timestamp).count() < CONDITION_CACHE_TIMEOUT_MS) {
            return it->second.result;
        }
    }
    
    // Evaluate the condition
    bool result = false;
    
    // ... condition evaluation logic ...
    
    // Update cache
    conditionCache[condition] = {result, std::chrono::steady_clock::now()};
    
    return result;
}
```

## 16. Security Considerations

### 16.1 Input Validation

All user input, especially in the scripting engine, should be validated:

```cpp
bool ScriptEngine::ExecuteString(const std::string& code) {
    // Check for potentially harmful operations
    if (code.find("os.execute") != std::string::npos ||
        code.find("io.popen") != std::string::npos) {
        lo.error("Script contains potentially unsafe system calls");
        return false;
    }
    
    try {
        auto result = lua.script(code);
        return result.valid();
    } catch (const sol::error& e) {
        lo.error("Lua error: " + std::string(e.what()));
        return false;
    }
}
```

### 16.2 Privilege Management

The system should run with the minimum required privileges:

```cpp
// Check for required X11 permissions
bool IO::CheckPermissions() {
    Display* testDisplay = XOpenDisplay(NULL);
    if (!testDisplay) {
        lo.error("Cannot open X display. Check DISPLAY environment variable.");
        return false;
    }
    
    // Try to grab a test key to verify permissions
    Window root = DefaultRootWindow(testDisplay);
    int testKeycode = XKeysymToKeycode(testDisplay, XK_F24);  // Use a rarely used key
    
    Bool success = XGrabKey(testDisplay, testKeycode, 0, root, False, GrabModeAsync, GrabModeAsync);
    if (success) {
        XUngrabKey(testDisplay, testKeycode, 0, root);
    }
    
    XCloseDisplay(testDisplay);
    
    if (!success) {
        lo.error("Insufficient permissions to grab keys. Try running with appropriate privileges.");
        return false;
    }
    
    return true;
}
```

## 17. Troubleshooting Guide

### 17.1 Common Issues

#### Hotkeys Not Working

1. **Check X11 Permissions**
   - Ensure the application has appropriate permissions to grab keys
   - Try running with elevated privileges if necessary

2. **Check for Conflicts**
   - Another application may have grabbed the same hotkey
   - Use `xev` to check if key events are being detected

3. **Check Window Focus**
   - Contextual hotkeys only work when the specified window has focus
   - Use `xprop` to verify window properties

#### Window Management Issues

1. **Window Manager Compatibility**
   - Some window managers may not support all operations
   - Check the detected window manager type with `WindowManager::GetCurrentWMName()`

2. **X11 Server Issues**
   - Restart the X server if window operations are failing
   - Check X11 error logs for details

### 17.2 Diagnostic Tools

#### Debug Mode

Enable debug mode for detailed logging:

```cpp
// Enable debug mode
lo.setLogLevel(LogLevel::DEBUG);
hotkeyManager.setVerboseKeyLogging(true);
hotkeyManager.setVerboseWindowLogging(true);
hotkeyManager.setVerboseConditionLogging(true);
```

#### Hotkey Tester

Use the included hotkey test utility:

```bash
# Run the hotkey tester
./hotkey_test
```

#### X11 Tools

Use standard X11 tools for diagnostics:

```bash
# Monitor X events
xev

# Get window properties
xprop

# List window hierarchy
xwininfo -root -tree
```

## 18. Future Development

### 18.1 Planned Features

1. **Wayland Support**
   - Add support for Wayland display server
   - Implement Wayland-specific window management

2. **Improved GUI**
   - Modernize the configuration interface
   - Add visual hotkey mapping tool

3. **Plugin System**
   - Create a plugin architecture for extensions
   - Support third-party plugins

4. **Remote Control**
   - Enhance network control capabilities
   - Add mobile app integration

### 18.2 Contributing

Contributions to the HvC project are welcome. Here's how to get started:

1. **Fork the Repository**
   - Create your own fork of the project

2. **Set Up Development Environment**
   - Follow the build instructions in Section 7
   - Install development tools and dependencies

3. **Choose an Issue**
   - Look for open issues or feature requests
   - Discuss your approach before starting work

4. **Submit a Pull Request**
   - Follow the coding standards
   - Include tests for new functionality
   - Update documentation as needed

5. **Code Review**
   - Address feedback from maintainers
   - Make necessary changes

### 18.3 Roadmap

#### Short-term (0-6 months)
- Bug fixes and stability improvements
- Performance optimizations
- Documentation enhancements

#### Medium-term (6-12 months)
- Wayland support (initial implementation)
- Plugin system architecture
- Enhanced scripting capabilities

#### Long-term (12+ months)
- Complete Wayland support
- Mobile integration
- Machine learning for window management predictions

## 19. Frequently Asked Questions

### 19.1 General Questions

**Q: Can HvC run on non-Linux systems?**
A: Currently, HvC is designed specifically for Linux with X11. Support for other platforms may be added in the future.

**Q: Does HvC work with all window managers?**
A: HvC works with most popular window managers, but some features may be limited depending on the window manager's capabilities.

**Q: How much memory does HvC use?**
A: HvC is designed to be lightweight, typically using less than 50MB of RAM during normal operation.

### 19.2 Development Questions

**Q: How do I add support for a new window manager?**
A: Implement a new detector in `WindowManagerDetector` and add specific handling in `WindowManager`.

**Q: Can I use other scripting languages besides Lua?**
A: Currently only Lua is supported, but the `ScriptEngine` could be extended to support other languages.

**Q: How do I debug my Lua scripts?**
A: Use the `print()` function in your scripts and enable verbose logging in HvC.

### 19.3 Usage Questions

**Q: How do I create a custom hotkey?**
A: Use the `HotkeyManager::AddHotkey()` method or add it to your hotkey configuration file.

**Q: Can I use mouse gestures instead of hotkeys?**
A: Yes, the `MouseGesture` class provides support for mouse gesture recognition.

**Q: How do I automate window positioning?**
A: Use window rules or create a Lua script that positions windows based on their properties.

## 20. Glossary

| Term | Definition |
|------|------------|
| Hotkey | A keyboard shortcut that triggers an action |
| Window Manager | Software that controls the placement and appearance of windows |
| X11 | The X Window System, version 11, a windowing system for bitmap displays |
| Wayland | A computer display server protocol intended to replace X11 |
| Lua | A lightweight, high-level scripting language |
| Contextual Hotkey | A hotkey that only works in specific contexts or windows |
| Window Rule | A set of conditions and actions applied to windows matching certain criteria |
| Macro | A recorded sequence of inputs that can be played back |
| MPV | A free and open-source media player |
| GTK | GIMP Toolkit, a multi-platform toolkit for creating graphical user interfaces |

2. Write descriptive commit messages
3. Review changes before merging
4. Keep master branch stable
