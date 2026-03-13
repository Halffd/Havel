HAVEL PROGRAMMING LANGUAGE
Design Document v2.0
----

📋 Summary
Havel is a declarative automation scripting language designed for hotkey management and workflow automation. Named after the resilient knight from Dark Souls, Havel embodies strength, reliability, and unwavering functionality in automation tasks.

**Key Innovation**: AHK-inspired ergonomic syntax that combines the speed of macro languages with the power of programming languages.

## Navigation
- [Quick Syntax Reference](#quick-syntax-reference)
- [Keywords](#keywords)
- [Ergonomic Syntax Features](#ergonomic-syntax-features)
- [Conditional Hotkeys](#conditional-hotkeys)
- [Built-in Help](#built-in-help)
- [Type System](#type-system-gradual-typing)
- [CLI Usage](#cli-usage)
- [Language Server (LSP)](#language-server-protocol-lsp)
- [Examples](#examples)
- [New Features](#new-features-latest)

----

🎯 Language Overview
Core Philosophy
	* Declarative Simplicity - Define what you want, not how to achieve it
	* Functional Flow - Data flows through transformation pipelines
	* Automation First - Built specifically for hotkeys and workflow automation
	* Platform Agnostic - Write once, run everywhere
	* **AHK-Inspired Ergonomics** - Fast typing beats academic purity

## Ergonomic Syntax Features

### 1. Global Core Verbs (Fast Path)
For maximum productivity, core operations are available as global functions:

```havel
// Fast typing, zero ceremony
print("Hello")
sleep(1000)
send("ctrl+c")
play("music.mp3")
exit()
read("/tmp/file.txt")
write("/tmp/file.txt", "content")
click()
mouseMove(100, 200)
```

**Available Globals:**
- `print()` - Output text
- `sleep(ms|duration)` - Delay execution (accepts milliseconds or duration strings like "30s", "1h30m")
- `sleepUntil(time)` - Sleep until specific time (e.g., "13:10", "thursday 8:00")
- `send(keys)` - Send keystrokes
- `play()` - Media control
- `exit()` - Exit application
- `read(path)` - File operations
- `write(path, content)` - File operations
- `click([button])` - Mouse actions
- `mouseMove(x, y)` - Mouse positioning

### 2. Use Statement Flattening (Structured Path)
Import module functions directly into current scope:

```havel
use io, media, filemanager

// Direct access without module.method()
mouseMove(10, 0)     // instead of io.mouseMove(10, 0)
play("song.mp3")     // instead of media.play("song.mp3")
read("/tmp/file.txt") // instead of filemanager.read("/tmp/file.txt")
```

**Available Modules:**
- `io` - Input/output operations
- `media` - Media playback control
- `filemanager` - File system operations
- `clipboard` - Clipboard operations
- `text` - Text processing
- `window` - Window management

### 3. With Blocks (Contextual Scoping)
Create temporary scope with object members available:

```havel
with io {
    mouseMove(10, 0)     // Direct access inside with block
    click()               // No io. prefix needed
    send("hello")         // All functions available
}
// Outside with block: back to normal scope
```

**Benefits:**
- Zero typing overhead inside blocks
- Clean, readable syntax
- Automatic scope management
- Perfect for complex operations

## Quick Syntax Reference

### Basic Hotkey Mapping
```
hotkey => action
```

### Pipeline Transformations
```
data | transform1 | transform2
```

### Blocks
```
hotkey => {
    // multiple statements
}
```

### Variables
```
let variable_name = value
```

### Conditional Logic
```
if condition { ... } else { ... }
```

### Control Flow Statements

#### Do-While Loop
Execute body at least once, then repeat while condition is true:

```havel
do {
    brightness += 0.1
} while getKey("alt")

// Counter example
let counter = 0
do {
    print("Iteration: " + counter)
    counter++
} while counter < 5
```

#### Switch Statement
Pattern matching with first-match-wins semantics:

```havel
// Basic switch
let level = 0
+numpad1 => {
    level++
    switch level {
        -1   { zoomOut() }
         0   { resetZoom() }
         1   { zoomIn() }
         2   { toggleZoom() }
         3   { disableZoom() }
        >3   { level = -1 }
        else { level = 3 }
    }
}

// String matching
let mode = "gaming"
switch mode {
    "gaming" => { print("Gaming mode activated") }
    "work"   => { print("Work mode activated") }
    else     => { print("Unknown mode: " + mode) }
}

// Relational operators
let value = 5
switch value {
    <0   => { print("Negative") }
    >10  => { print("Large positive") }
    else => { print("Medium or zero") }
}
```

**Switch Features:**
- **First match wins** - No fallthrough possible
- **Relational operators** - `>3`, `<=2`, `<0`, `>=10`
- **Else clause** - Optional but recommended
- **Type support** - Numbers, strings, booleans
- **Clean syntax** - No parentheses, visually scannable

**Parsing Rules:**
- Left side: literal (`0`, `-1`) or relational (`>3`, `<=2`)
- Right side: block `{ }`
- Evaluated top → bottom
- `else` optional but recommended

### Exception Handling
Havel provides structured exception handling with try/catch/finally blocks:

#### Basic Try-Catch
```havel
try {
    // Risky operation
    throw "Something went wrong"
} catch error {
    print("Caught: " + error)
}
```

#### Try with Finally
```havel
try {
    // Operation that needs cleanup
    file = open("/tmp/data.txt")
} finally {
    // Always runs, even if error occurs
    if file {
        file.close()
    }
}
```

#### Complete Exception Handling
```havel
try {
    // Main operation
    data = read("/config.json")
    config = parse(data)
} catch parseError {
    print("Failed to parse config: " + parseError)
} finally {
    print("Config loading attempt complete")
}
```

#### Throw Values
Any value can be thrown:
```havel
throw "Error message"           // String
throw 42                        // Number  
throw {type: "custom", code: 123} // Object
```

**Exception Features:**
- **Single catch handler** - Simple error handling model
- **Optional finally block** - Always runs for cleanup
- **Any value type** - Throw strings, numbers, objects
- **Structured error propagation** - Clean control flow
- **Nested try blocks** - Support for complex error handling

## Built-in Help

### Help Command
Get documentation for any module or function:

```havel
help()              // Show all available modules
help("http")        // Show HTTP module documentation
help("browser")     // Show browser module documentation
help("mouse")       // Show mouse module documentation
help("http.get")    // Show specific function documentation
```

### Available Modules

| Module | Description | Example |
|--------|-------------|---------|
| `http` | HTTP client for REST APIs | `http.get(url)` |
| `browser` | Browser automation via CDP | `browser.setZoom(1.5)` |
| `mouse` | Mouse control | `mouse.click("left")` |
| `audio` | Audio/volume control | `audio.setVolume(0.5)` |
| `window` | Window management | `window.maximize()` |
| `process` | Process management | `process.find("chrome")` |
| `clipboard` | Clipboard operations | `clipboard.get()` |
| `text` | Text processing | `text.upper("hello")` |
| `system` | System integration | `system.notify("msg")` |

### Quick Reference

#### HTTP Module
```havel
// GET request
response = http.get("https://api.example.com")
print(response.statusCode)  // 200
print(response.body)        // Response content
print(response.ok)          // true if 2xx

// POST with JSON
data = "{\"key\":\"value\"}"
response = http.post("https://api.example.com", data)

// Download file
http.download("https://example.com/file.zip", "/tmp/file.zip")
```

#### Browser Module
```havel
// Setup: Start Chrome with --remote-debugging-port=9222
browser.connect()
browser.open("https://example.com")

// Zoom control
browser.setZoom(1.5)   // 150%
zoom = browser.getZoom()
browser.resetZoom()

// Interact with page
browser.click("#button")
browser.type("#input", "text")
result = browser.eval("document.title")
```

#### Mouse Module
```havel
// Clicking
mouse.click()          // Left click
mouse.click("right")   // Right click
mouse.down("left")     // Press and hold
mouse.up("left")       // Release

// Movement
mouse.move(100, 200)       // Absolute position
mouse.moveRel(10, -5)      // Relative movement

// Scrolling
mouse.wheel(3)         // Scroll up
mouse.wheel(-3)        // Scroll down
```

## Keywords

| Keyword | Purpose | Example |
|---------|---------|---------|
| `let` | Variable declaration | `let x = 5` |
| `if/else` | Conditional execution | `if x > 0 { ... } else { ... }` |
| `do/while` | Do-while loop | `do { ... } while condition` |
| `switch` | Pattern matching | `switch x { 1 => { ... } else => { ... } }` |
| `when` | Conditional block | `when condition { ... }` |
| `fn` | Function definition | `fn name(args) => ...` |
| `return` | Function return | `return value` |
| `import` | Module import | `import module from "path"` |
| `use` | Module flattening | `use io, media` |
| `with` | Contextual scoping | `with io { ... }` |
| `config` | Configuration block | `config { debug = true }` |
| `devices` | Device configuration | `devices { ... }` |
| `modes` | Mode configuration | `modes { ... }` |
| `try` | Exception handling | `try { ... } catch { ... } finally { ... }` |
| `catch` | Exception handler | `catch error { ... }` |
| `finally` | Cleanup block | `finally { ... }` |
| `throw` | Raise exception | `throw value` |
| `on start` | Execute on script start | `on start { init() }` |
| `on reload` | Execute on script reload | `on reload { reloadConfig() }` |
| `runOnce` | Run command once only | `runOnce("id", "command")` |

## Script Auto-Reload

Havel supports automatic script reloading when the script file changes, perfect for development and dynamic configuration.

### Enabling Auto-Reload

```havel
// Enable auto-reload for current script
app.enableReload()

// Disable auto-reload
app.disableReload()

// Toggle auto-reload state
app.toggleReload()

// Check if auto-reload is enabled
if app.reload {
    print("Auto-reload is enabled")
}
```

### Lifecycle Hooks

#### on start Block
Executes **only once** when the script first loads. Does NOT execute on reload.

```havel
on start {
    print("Script initialized")
    // Initialize database connections
    // Load configuration files
    // Setup one-time resources
}
```

#### on reload Block
Executes **only** when the script reloads due to file changes.

```havel
on reload {
    print("Script reloaded")
    // Reload configuration
    // Refresh cached data
    // Reinitialize dynamic resources
}
```

### runOnce Function
Execute a shell command only once, never again on reloads.

```havel
// Run command once with unique ID
runOnce("setup-db", "sqlite3 /tmp/app.db 'CREATE TABLE IF NOT EXISTS users'")

// This won't run again even after reload
runOnce("init-config", "cp /etc/app/config.default /etc/app/config")

// Multiple runOnce calls with different IDs
runOnce("log-start", "echo 'App started' >> /var/log/app.log")
runOnce("check-deps", "which ffmpeg || echo 'FFmpeg not found'")
```

**Key Features:**
- `runOnce(id, command)` - Command executes only once per unique ID
- Uses `Launcher::runShell()` for command execution
- Returns `true` on success, `false` on failure
- State persists across reloads

### Complete Example

```havel
// Lifecycle hooks
on start {
    print("=== Application Starting ===")
    runOnce("init", "mkdir -p /tmp/myapp")
    runOnce("setup", "touch /tmp/myapp/initialized")
}

on reload {
    print("=== Configuration Reloaded ===")
    // Reload settings without restarting
    loadConfig()
}

// Enable auto-reload
app.enableReload()
print("Auto-reload enabled for: " + app.getScriptPath())

// Your main script logic
hotkey "F1" => {
    print("Hotkey pressed")
}
```

### Configuration Block Enhancements

Config blocks now support both `=` and `:` as separators, and nested blocks for hierarchical configuration:

```havel
// Using = separator (recommended)
config {
    debug = true
    logKeys = true
    timeout = 5000
    
    // Nested blocks create hierarchical keys
    window {
        monitoring = true
        printWindows = false
        opacity = 0.9
    }
    
    hotkeys {
        enableGlobal = true
        prefix = "ctrl+alt"
    }
}

// Using : separator (legacy support)
config {
    debug: true
    window: {
        monitoring: true
    }
}

// Mixed separators (allowed but not recommended)
config {
    debug = true
    window: {
        monitoring = true
    }
}
```

**Config Key Hierarchy:**
- `Havel.debug = true`
- `Havel.logKeys = true`
- `Havel.window.monitoring = true`
- `Havel.window.printWindows = false`
- `Havel.hotkeys.enableGlobal = true`

**Accessing Config Values:**
```havel
// Config values are stored with Havel. prefix
use config
debugMode = config.get("Havel.debug")
windowMonitoring = config.get("Havel.window.monitoring")
```

## Standard Library Modules

### Built-in Modules
Havel provides comprehensive built-in modules for common automation tasks:

#### IO Module (Input/Output)
```havel
// Direct access
use io
mouseMove(100, 200)     // Move mouse
click()                  // Click mouse
send("hello")            // Send keystrokes
keyDown("ctrl")          // Key down
keyUp("ctrl")            // Key up
suspend()                // Toggle hotkey suspend

// Global convenience functions (no module prefix needed)
click()                  // Left click
doubleClick()            // Double click
mousePress()             // Press mouse button
mouseRelease()           // Release mouse button
mouseMove(x, y)          // Move to position
mouseMoveRel(dx, dy)     // Relative movement
scroll(dy, dx)           // Scroll
```

#### Media Module (Media Control)
```havel
// Direct access
use media
play()                   // Play/pause
pause()                  // Pause playback
stop()                   // Stop playback
next()                   // Next track
previous()               // Previous track
```

#### Filemanager Module (File Operations)
```havel
// Direct access
use filemanager
read("/path/to/file")           // Read file
write("/path/to/file", "content") // Write file
exists("/path/to/file")         // Check existence
size("/path/to/file")           // Get file size
```

#### Clipboard Module (Clipboard Operations)
```havel
// Direct access
use clipboard
get()                    // Get clipboard content
set("text")              // Set clipboard content
clear()                  // Clear clipboard
send()                   // Send clipboard as keystrokes
send("hello")            // Send text as keystrokes

// Global variable
$Clipboard = "text"      // Set clipboard
print($Clipboard)        // Get clipboard
```

#### Text Module (Text Processing)
```havel
// Direct access
use text
upper("hello")             // Uppercase
lower("HELLO")             // Lowercase
trim("  text  ")            // Remove whitespace
replace("a", "b", "text")  // Replace text
```

#### Math Module (Mathematical Functions)
```havel
// Direct access
use math

// Basic arithmetic
abs(-5.5)                  // Absolute value: 5.5
ceil(4.2)                  // Round up: 5.0
floor(4.8)                 // Round down: 4.0
round(4.6)                 // Round to nearest: 5.0

// Trigonometric functions (radians)
sin(PI/2)                  // Sine: 1.0
cos(0)                     // Cosine: 1.0
tan(PI/4)                  // Tangent: 1.0
asin(0.5)                  // Arc sine
acos(0.5)                  // Arc cosine
atan(1)                   // Arc tangent
atan2(y, x)               // Angle from (x, y)

// Hyperbolic functions
sinh(1.0)                  // Hyperbolic sine
cosh(1.0)                  // Hyperbolic cosine
tanh(1.0)                  // Hyperbolic tangent

// Exponential and logarithmic
exp(1)                     // e^1: 2.718...
log(E)                     // Natural logarithm
log10(100)                 // Base-10 logarithm: 2.0
log2(8)                    // Base-2 logarithm: 3.0
sqrt(16)                   // Square root: 4.0
cbrt(27)                   // Cube root: 3.0
pow(2, 3)                  // Power: 8.0

// Mathematical constants
PI                         // 3.14159...
E                          // 2.71828...
TAU                        // 2*PI: 6.28318...
SQRT2                      // √2: 1.41421...
LN2                        // ln(2): 0.69314...
LN10                       // ln(10): 2.30258...

// Utility functions
min(3, 1, 4, 2)            // Minimum: 1.0
max(3, 1, 4, 2)            // Maximum: 4.0
clamp(2.5, 1, 3)           // Clamp value: 2.5
lerp(0, 10, 0.5)           // Linear interpolation: 5.0

// Random numbers
random()                   // Random float [0, 1)
random(10)                 // Random float [0, 10)
random(5, 10)              // Random float [5, 10)
randint(10)                // Random integer [0, 10]
randint(5, 10)             // Random integer [5, 10]

// Angle conversion
deg2rad(180)               // Degrees to radians: π
rad2deg(PI)                // Radians to degrees: 180

// Special functions
sign(5)                    // Sign: 1.0
sign(-5)                   // Sign: -1.0
sign(0)                    // Sign: 0.0
fract(3.7)                 // Fractional part: 0.7
mod(7, 3)                  // Modulo: 1.0

// Geometry functions
distance(0, 0, 3, 4)       // 2D distance: 5.0
hypot(3, 4)                // Hypotenuse: 5.0
hypot(1, 2, 2)             // Multi-dimensional hypotenuse
```

#### Array Methods

Arrays provide built-in methods for transformation and manipulation:

```havel
let arr = [3, 1, 4, 1, 5]

// Sorting
arr.sort()                        // Sort in place: [1, 1, 3, 4, 5]
arr.sort((a, b) => b - a)         // Custom comparator: descending
let sorted = sorted(arr)          // Non-mutating sort
let rev = sorted(arr, (a,b)=>b-a) // Non-mutating with comparator

// Object array sorting
let people = [{name: "Bob", age: 30}, {name: "Alice", age: 25}]
people.sortByKey("age")           // Sort by key ascending
people.sortByKey("name", (a,b) => b > a)  // Custom comparator

// Transform
arr.map(x => x * 2)               // [2, 2, 6, 8, 10]
arr.filter(x => x > 2)            // [3, 4, 5]
arr.reduce((acc, x) => acc + x, 0)  // Sum: 14

// Query
arr.find(x => x > 3)              // First match: 4
arr.some(x => x > 10)             // Any match: false
arr.every(x => x > 0)             // All match: true
arr.includes(3)                   // Contains: true
arr.indexOf(4)                    // Index: 3

// Structural
arr.push(6)                       // Add to end
arr.pop()                         // Remove from end
arr.insert(0, 0)                  // Insert at index
arr.removeAt(0)                   // Remove at index
arr.slice(1, 3)                   // Subarray: [1, 4]
arr.concat([6, 7])                // Concatenate
arr.swap(0, 1)                    // Swap elements
```

#### Function and Module Overriding

Havel allows overriding both built-in functions and module functions to provide custom implementations:

```havel
// Override built-in function directly
function abs(x) {
    return "custom abs: " + x;
}

// Override module function
function math.abs(x) {
    return "custom math.abs: " + x;
}

// Override with different signature
function math.sin(x, y) {
    return "custom sin with 2 args: " + x + ", " + y;
}
```

**Overriding Rules:**

1. **Direct Function Override**: Define a function with the same name as a built-in function
   - `abs(-5)` calls custom `abs()` instead of built-in
   - Takes precedence over built-in functions

2. **Module Function Override**: Define a function with qualified module name
   - `math.abs(-5)` calls custom `math.abs()` instead of built-in
   - `io.print("msg")` calls custom `io.print()` instead of built-in
   - `gui.message("title", "msg")` calls custom `gui.message()` instead of built-in

3. **Signature Flexibility**: Override with different parameter counts
   - Built-in `math.sin(x)` can be overridden with `math.sin(x, y)`
   - Allows extending functionality while maintaining compatibility

4. **Built-in Access**: Non-overridden functions still work normally
   - `math.cos(0)` still calls built-in cosine if not overridden
   - Module functions can call other built-in functions internally

**Use Cases:**
- **Custom Behavior**: Replace built-in implementations with custom logic
- **Logging**: Wrap built-in functions to add logging/debugging
- **Validation**: Add input validation before calling built-in functions
- **Feature Extension**: Add parameters to existing functions

#### Window Module (Window Management)
```havel
// Direct access
use window
active()                  // Get active window
list()                    // List all windows
focus("title")             // Focus window
min()                     // Minimize window
max()                     // Maximize window
```

## Conditional Hotkeys

### Basic Hotkey
```havel
F3 => {
    print("hello")
}
```

### Hotkey with When Condition
```havel
F3 when mode gaming => {
    print("gaming mode!")
}
```

### Multiple Conditions (AND)
```havel
F1 when mode gaming && title Game => {
    print("Gaming in game window")
}

F2 when mode work && class code => {
    print("Coding at work")
}
```

### When Blocks (Group Hotkeys by Condition)
```havel
when mode gaming {
    F1 => { print("Attack!") }
    F2 => { print("Defend!") }
    F3 => { print("Special!") }
}

when mode work {
    F1 => { print("Build project") }
    F2 => { print("Run tests") }
}
```

### Nested When Blocks
```havel
when mode gaming {
    when title Steam {
        F1 => { print("Steam overlay") }
    }
}
```

### Condition Types

**Mode conditions:**
```havel
F1 when mode gaming => { ... }
F2 when mode work => { ... }
```

**Window title conditions:**
```havel
F1 when title Firefox => { ... }
F2 when title Chrome => { ... }
```

**Window class conditions:**
```havel
F1 when class code => { ... }
F2 when class firefox => { ... }
```

**Process conditions:**
```havel
F1 when process steam => { ... }
```

**Window group conditions:**
```havel
F1 when group browsers => { ... }
F2 when group terminals => { ... }
```

**Combined conditions:**
```havel
F1 when mode gaming && title Steam => { ... }
F2 when mode work && class code && title Visual => { ... }
```

**All conditions are evaluated dynamically at runtime**, allowing hotkeys to activate/deactivate based on current state.

Target Use Cases
	* Hotkey automation and mapping
	* Clipboard transformation workflows
	* Window management automation
	* Text processing and manipulation
	* Cross-application workflow orchestration

----

⚙️ Technical Specifications
File Extension
.hv - Concise, memorable, and directly tied to the language name

Syntax Paradigm
	* Pipeline-based using | operator for data flow
	* Arrow function style hotkey mapping with =>
	* Block structure using {} for complex actions
	* Functional composition for transformation chains

Core Language Features
// Basic hotkey mapping
F1 => send "Hello World!"

// Pipeline transformations
clipboard.get 
    | text.upper 
    | text.replace " " "_"
    | send

// Complex workflows
^V => {
    clipbooard.get
        | text.trim
        | text.sanitize
        | window.paste
}

// Conditional logic
F2 => {
    if clipbooard.get | text.contains "error" {
        send "DEBUG: "
    }
    clipbooard.get | send
}

// Dynamic conditional hotkeys
F1 if mode == "gaming" => send "Quick attack!"
^!A if window.title.contains("Chrome") => send "Ctrl+F5"

// When blocks for grouped conditions
when mode == "gaming" {
    ^!A => send "Attack!"
    ^!B => send "Defend!"
    ^!C => send "Special ability!"
}

// Prefix conditions
when mode == "gaming" {
    F1 if health < 50 => send "Heal potion!"
    ^!D if ammo == 0 => send "Reload!"
}

----


🎨 Visual Identity
Logo Design
    ⚙️ HAVEL ⚙️
   ╭─────────────╮
   │  ⚙️ ⚒️ ⚙️  │
   │  ⚒️ H ⚒️   │  
   │  ⚙️ ⚒️ ⚙️  │
   ╰─────────────╯

Primary Logo Elements:

	* Steel gear iconography - Representing mechanical precision
	* Interlocking gears - Symbolizing automation workflows
	* Bold "H" centerpiece - Clear language identification
	* Metallic color palette - Steel gray, iron black, chrome silver

Brand Colors
	* Primary: Steel Gray #708090
	* Secondary: Iron Black #2F2F2F
	* Accent: Chrome Silver #C0C0C0
	* Highlight: Forge Orange #FF4500

Typography
	* Primary Font: Industrial/mechanical sans-serif
	* Code Font: JetBrains Mono or similar monospace
	* Logo Font: Heavy, bold industrial typeface

----

🖥️ Platform Support
Target Platforms
	* Linux (Ubuntu, Fedora, Arch)
	* Windows 10/11
	* macOS 12+ (Secondary)
Architecture Support
	* x86_64 (Primary)
	* ARM64 (Apple Silicon, ARM Linux)
	* x86 (Legacy Windows support)

Installation Methods
# Package managers
winget install havel-lang
brew install havel-lang  
apt install havel-lang
pacman -S havel-lang

# Direct download
https://havel-lang.org/download/

# Docker container
docker run havel-lang/runtime:latest

----

🔧 Core Components
Language Runtime
	* Interpreter Engine - Fast execution of Havel scripts
	* Platform Abstraction Layer - Unified API across operating systems
	* Security Sandbox - Safe execution of automation scripts
	* Hot Reload - Live script editing and testing

Standard Library Modules
Clipboard Module
clipboard.get        // Get clipboard content
clipboard.set(text)  // Set clipboard content
clipboard.clear()    // Clear clipboard
clipboard.send([text]) // Send clipboard/text as keystrokes
clipboard.history    // Access clipboard history
$Clipboard           // Global clipboard variable (get/set)

Text Processing Module
text.upper           // Convert to uppercase
text.lower           // Convert to lowercase
text.trim            // Remove whitespace
text.replace(a, b)   // Replace text
text.split(delim)    // Split into array
text.join(delim)     // Join array to string

Window Management Module
window.active        // Get active window
window.list          // List all windows
window.focus(name)   // Focus specific window
window.min()         // Minimize window
window.max()         // Maximize window
window.getMonitors() // Get array of monitor info objects
window.getMonitorArea() // Get combined area of all monitors
window.getGroups()   // Get all window group names
window.getGroupWindows(group) // Get windows in group
window.isWindowInGroup(title, group) // Check if window in group
window.findInGroup(group) // Find first window in group

System Integration Module
system.run(cmd)      // Execute system command
system.notify(msg)   // Show notification
system.beep()        // System beep
system.sleep(ms|duration)  // Delay execution (e.g., 1000, "30s", "1h30m")

Pixel and Image Automation Module
pixel.get(x, y)              // Get pixel color (returns {r,g,b,a,hex})
pixel.match(x, y, color, tol)  // Check if pixel matches color
pixel.wait(x, y, color, tol, timeout)  // Wait for pixel color
pixel.region(x, y, w, h)     // Create region object

image.find(path, region, threshold)    // Find image on screen
image.wait(path, region, timeout, threshold)  // Wait for image
image.exists(path, region, threshold)  // Check if image exists
image.count(path, region, threshold)   // Count image occurrences
image.findAll(path, region, threshold) // Find all matches

ocr.read(region, language, whitelist)  // Read text from screen

Process Management Module
process.find(name)           // Find processes by name
process.exists(pid|name)     // Check if process exists
process.kill(pid, signal)    // Send signal to process
process.nice(pid, value)     // Set CPU priority (-20 to 19)
process.ionice(pid, class, data)  // Set I/O priority

Process Discovery and Control
// Find all Firefox processes
let firefox_procs = process.find("firefox")
for proc in firefox_procs {
    print("PID: ${proc.pid}, CPU: ${proc.cpu_usage}%")
}

// Process lifecycle management
if process.exists("chrome") {
    let chrome = process.find("chrome")
    process.kill(chrome[0].pid, "SIGTERM")
}

// Priority control
process.nice(1234, 10)           // Lower CPU priority
process.ionice(1234, 2, 4)      // Best-effort I/O, priority 4

Process Information Object
When using process.find(), each process object contains:
- pid: Process ID
- ppid: Parent process ID
- name: Process name
- command: Full command line
- user: Process owner
- cpu_usage: CPU usage percentage
- memory_usage: Memory usage in bytes

HTTP Client Module
http.get(url)              // GET request
http.post(url, data)       // POST request with body
http.put(url, data)        // PUT request with body
http.delete(url)           // DELETE request
http.patch(url, data)      // PATCH request with body
http.download(url, path)   // Download file to path
http.upload(url, path)     // Upload file
http.setTimeout(ms)        // Set request timeout (default: 30000ms)

Response Object
All HTTP methods return an object with:
- statusCode: HTTP status code (200, 404, etc.)
- body: Response body as string
- ok: true if statusCode is 2xx
- error: Error message (empty on success)

REST API Integration
// Simple GET request
response = http.get("https://api.github.com/users/halffd")
if (response.ok) {
    print("User found: " + response.body)
} else {
    print("Error: " + response.error)
}

// POST with JSON data
data = "{\"name\":\"test\",\"value\":123}"
response = http.post("https://api.example.com/items", data)

// Download a file
if (http.download("https://example.com/image.png", "/tmp/image.png")) {
    print("Download complete")
}

// Set custom timeout for slow endpoints
http.setTimeout(60000)  // 60 seconds
response = http.get("https://slow-api.example.com/data")

Browser Automation Module (CDP/Marionette)
// Connection
browser.connect(url)           // Connect to Chrome (default: localhost:9222)
browser.connectFirefox(port)   // Connect to Firefox (default port: 2828)
browser.disconnect()           // Disconnect from browser
browser.isConnected()          // Check connection status
browser.getBrowserType()       // Get type: "chrome", "firefox", "chromium"
browser.setPort(port)          // Set CDP port number
browser.getPort()              // Get current CDP port

// Browser Discovery
browser.getOpenBrowsers()      // List all running browser instances
browser.getDefaultBrowser()    // Get system default browser info

// Navigation
browser.open(url)              // Open URL in new tab
browser.newTab(url)            // Create new tab
browser.goto(url)              // Navigate to URL
browser.back()                 // Go back
browser.forward()              // Go forward
browser.reload(ignoreCache)    // Reload page

// Tab Management
browser.listTabs()             // List all open tabs
browser.activate(tabId)        // Activate tab
browser.closeTab(tabId)        // Close tab (-1 for current)
browser.closeAll()             // Close all tabs

// Window Management
browser.listWindows()          // List all browser windows
browser.setWindowSize(id, w, h)    // Set window dimensions
browser.setWindowPosition(id, x, y) // Set window position
browser.maximizeWindow(id)     // Maximize window
browser.minimizeWindow(id)     // Minimize window
browser.fullscreenWindow(id)   // Toggle fullscreen

// Extension Management (Chrome only)
browser.listExtensions()       // List installed extensions
browser.enableExtension(id)    // Enable extension
browser.disableExtension(id)   // Disable extension

// Element Interaction
browser.click(selector)        // Click element
browser.type(selector, text)   // Type text into input
browser.setZoom(level)         // Set zoom (0.5 - 3.0)
browser.getZoom()              // Get current zoom level
browser.resetZoom()            // Reset zoom to 100%
browser.eval(js)               // Execute JavaScript
browser.screenshot(path)       // Take screenshot
browser.getCurrentUrl()        // Get current URL
browser.getTitle()             // Get page title

Browser Setup
// Chrome/Chromium - Start with remote debugging
google-chrome --remote-debugging-port=9222
# or
chromium-browser --remote-debugging-port=9222

// Firefox - Start with Marionette
firefox --marionette --marionette-port 2828

Browser Automation Examples
// Connect and navigate
browser.connect()  // Connects to localhost:9222
browser.open("https://example.com")

// Zoom control (as requested)
browser.setZoom(1.5)   // Set zoom to 150%
currentZoom = browser.getZoom()
browser.resetZoom()    // Reset to 100%

// Element interaction
browser.click("#submit-button")
browser.type("#username", "myuser")
browser.type("#password", "mypassword")

// JavaScript execution
title = browser.eval("document.title")
browser.eval("document.body.style.zoom = '150%'")

// Tab management
tabs = browser.listTabs()
for tab in tabs {
    print("Tab: " + tab.title + " - " + tab.url)
}
browser.activate(tabs[0].id)
browser.close(-1)  // Close current tab

// Screenshot
browser.screenshot("/tmp/page.png")

// Firefox support
browser.connectFirefox(2828)
browser.open("https://firefox.com")

// Browser discovery
browsers = browser.getOpenBrowsers()
for b in browsers {
    print(b.type + " PID: " + b.pid + " Port: " + b.cdpPort)
}

// Get default browser
default = browser.getDefaultBrowser()
print("Default: " + default.name + " at " + default.path)

// Window management
windows = browser.listWindows()
if (windows.length > 0) {
    browser.maximizeWindow(windows[0].id)
    browser.setWindowSize(windows[0].id, 1920, 1080)
}

// Extension management (Chrome)
extensions = browser.listExtensions()
for ext in extensions {
    print(ext.name + " v" + ext.version + " (enabled: " + ext.enabled + ")")
}
browser.disableExtension(extensions[0].id)

// Custom port
browser.setPort(9223)
browser.connect()

Mouse Control Module
mouse.click(button)        // Click mouse button (left, right, middle)
mouse.down(button)         // Press and hold button
mouse.up(button)           // Release button
mouse.move(x, y)           // Move to absolute position
mouse.moveRel(dx, dy)      // Move relative to current position
mouse.wheel(amount)        // Scroll wheel (positive=up, negative=down)
mouse.getPosition()        // Get current position {x, y}
mouse.setSpeed(speed)      // Set mouse speed (default: 5)
mouse.setAccel(accel)      // Set acceleration (default: 1.0)

Mouse Button Values
- "left" or "L" or 1 - Left button
- "right" or "R" or 2 - Right button
- "middle" or "M" or 3 - Middle button
- "back" or "B" or 4 - Back button
- "forward" or "F" or 5 - Forward button

Mouse Control Examples
// Basic clicking
mouse.click("left")
mouse.click("right")
mouse.click()  // Default: left button

// Drag and drop
mouse.move(100, 100)
mouse.down("left")
mouse.move(200, 200)
mouse.up("left")

// Scrolling
mouse.wheel(3)    // Scroll up 3 notches
mouse.wheel(-5)   // Scroll down 5 notches

// Precise positioning
mouse.move(500, 300)
pos = mouse.getPosition()
print("Mouse at: " + pos.x + ", " + pos.y)

// Speed and acceleration
mouse.setSpeed(10)     // Faster movement
mouse.setAccel(2.0)    // More acceleration

🛠️ Development Tools
IDE Integration
	* VS Code Extension - Syntax highlighting, IntelliSense, debugging
	* Language Server Protocol - Universal editor support
	* Vim/Neovim Plugin - Community-driven support

Development Workflow
# Create new project
havel init myproject

# Run script
havel run automation.hv

# Watch for changes
havel watch automation.hv

# Format code
havel fmt automation.hv

# Check syntax
havel check automation.hv

# Package for distribution
havel build --target windows-x64

Debugging & Testing
	* Interactive REPL - Test code snippets
	* Step Debugger - Debug automation flows
	* Unit Testing Framework - Built-in testing capabilities
	* Performance Profiler - Optimize automation scripts

Getting Started
# Quick start
curl -sSL https://install.havel-lang.org | sh
havel --version
echo 'F1 => send "Hello, Havel!"' > hello.hv
havel run hello.hv

----

🏰 Built with the strength of Havel, the reliability of steel, and the precision of gears.

"In automation, as in battle, preparation and reliability triumph over complexity."

---

## New Features (Latest)

### Hotkey Self-Management with `this` Context

Hotkeys can now manage themselves from within their action blocks using the `this` context:

```havel
// Self-disable based on window title
Enter if mode == "default" => { 
    if window.title == "vscode" {
        print("Disabling " + this.alias)
        this.disable()
    } else {
        clipboard.send()
    }
}

// Self-remove after first use
F1 => {
    print("This hotkey only works once!")
    this.remove()  // Remove myself
}

// Toggle based on state
F2 => {
    this.toggle()
    print("Hotkey is now " + (isEnabled ? "enabled" : "disabled"))
}
```

**Available `this` Properties:**
- `this.id` - Hotkey ID
- `this.alias` - Hotkey alias/name
- `this.key` - Hotkey key combination
- `this.condition` - Condition string

**Available `this` Methods:**
- `this.enable()` - Enable this hotkey
- `this.disable()` - Disable this hotkey
- `this.toggle()` - Toggle hotkey enabled state
- `this.remove()` - Remove this hotkey

### Window Groups

Organize windows into named groups for easier management:

```havel
// Config format (havel.cfg)
[window]
window.group.browsers = title Firefox
window.group.browsers = title Chrome
window.group.terminals = title Alacritty
window.group.terminals = class kitty
window.group.music = title Spotify
```

**Window Module Functions:**
```havel
// Get all groups
let groups = window.getGroups()

// Get windows in a group
let wins = window.getGroupWindows("browsers")

// Check if current window is in group
if window.isWindowInGroup(window.title, "browsers") {
    print("Browser is active")
}

// Find window in group
let win = window.findInGroup("terminals")
if win.found {
    print("Found terminal: " + win.id)
}
```

**Conditional Hotkeys with Groups:**
```havel
// Hotkey only active when browser is focused
F1 when group browsers => {
    print("Browser action")
}

// Multiple conditions
F2 when group terminals && mode == "work" => {
    print("Work terminal action")
}
```

### String Repetition with `*`

Python-style string repetition:

```havel
// String * Number
"abc" * 3      // "abcabcabc"
"-" * 5        // "-----"
"hi" * 0       // ""

// Number * String (commutative)
3 * "abc"      // "abcabcabc"
5 * "-"        // "-----"

// With variables
let sep = "-"
print(sep * 40)  // Print separator line

// In expressions
print("=" * 60)
```

### Global Clipboard Variable `$Clipboard`

Read/write clipboard using a global variable:

```havel
// Set clipboard
$Clipboard = "hello"
$Clipboard = 123

// Get clipboard
print($Clipboard)
let text = $Clipboard

// Use in expressions
if $Clipboard.contains("http") {
    run($Clipboard)  // Open URL
}
```

### Clipboard Send Function

Send clipboard content or text as keystrokes:

```havel
// Send current clipboard content
clipboard.send()

// Send specific text
clipboard.send("hello world")

// Send number
clipboard.send(123)
```

### IO Suspend/Resume

Toggle hotkey processing on/off:

```havel
io.suspend()    // Toggle suspend state
```

### Global Convenience Functions

Common mouse operations available as globals:

```havel
click()           // Left click
doubleClick()     // Double click
mousePress()      // Press mouse button
mouseRelease()    // Release mouse button
mouseMove(x, y)   // Move to position
mouseMoveRel(dx, dy)  // Relative movement
scroll(dy, dx)    // Scroll (vertical, horizontal)
```

### Spread Operator (`*`)

The spread operator expands arrays and objects inline, similar to JavaScript.

#### Array Spread
```havel
// Expand array elements
let a = [1, 2, 3]
let b = [0, ...a, 4]  // [0, 1, 2, 3, 4]

// Combine multiple arrays
let x = [1, 2]
let y = [3, 4]
let z = [...x, ...y]  // [1, 2, 3, 4]

// Flatten one level
let nested = [[1, 2], [3, 4]]
let flat = [...nested]  // [1, 2, 3, 4]
```

#### Object Spread
```havel
// Merge objects (later keys override earlier)
let obj1 = {a: 1, b: 2}
let obj2 = {c: 3, ...obj1}  // {a: 1, b: 2, c: 3}

// Override properties
let defaults = {volume: 50, muted: false}
let settings = {...defaults, volume: 80}  // {volume: 80, muted: false}
```

#### Function Call Spread
```havel
fn add3(x, y, z) {
    return x + y + z
}

let args = [1, 2, 3]
print(add3(...args))  // 6

// Mouse movement with spread
let coords = [100, 200]
mouse.moveTo(...coords)  // mouse.moveTo(100, 200)
```

### Enhanced `sleep()` Function

The `sleep()` function now accepts duration strings with units:

```havel
// Original numeric (milliseconds)
sleep(1000)  // 1 second

// Unit-based durations
sleep("30s")      // 30 seconds
sleep("5m")       // 5 minutes
sleep("1h")       // 1 hour
sleep("2d")       // 2 days
sleep("1w")       // 1 week
sleep("500ms")    // 500 milliseconds

// Combined units
sleep("1h30m")    // 1 hour 30 minutes
sleep("1m30s500ms")  // 1 minute 30.5 seconds

// Time format (HH:MM:SS.mmm)
sleep("0:0:30")      // 30 seconds
sleep("1:30:00")     // 1 hour 30 minutes
sleep("0:0:30.500")  // 30.5 seconds
```

### New `sleepUntil()` Function

Sleep until a specific time:

```havel
// Sleep until specific time today/tomorrow
sleepUntil("13:10")      // Sleep until 1:10 PM
sleepUntil("23:59:59")   // Sleep until 11:59:59 PM

// Sleep until specific day and time
sleepUntil("thursday 8:00")   // Sleep until Thursday 8 AM
sleepUntil("monday 14:30")    // Sleep until Monday 2:30 PM
sleepUntil("friday 17:00")    // Sleep until Friday 5 PM

// Day name abbreviations
sleepUntil("mon 9:00")   // Monday
sleepUntil("tue 9:00")   // Tuesday
sleepUntil("wed 9:00")   // Wednesday
sleepUntil("thu 9:00")   // Thursday
sleepUntil("fri 9:00")   // Friday
sleepUntil("sat 9:00")   // Saturday
sleepUntil("sun 9:00")   // Sunday
```

### Input Shortcuts (Implicit Input Statements)

Inside hotkey blocks, you can use implicit input commands without the `>` prefix:

```havel
// Traditional explicit syntax
^!t => {
    > "Hello World"
    > {Enter}
    > lmb
    > m(100, 200)
}

// Also works with explicit > prefix
^!t => {
    > "Hello World" :500 > {Enter}
    > lmb :250 > rmb
    > m(500, 200) lmb :2000 > r(0, 50)
}
```

**Available Commands:**
- `"text"` - Send text string
- `{Key}` - Send key (e.g., `{Enter}`, `{Esc}`)
- `lmb`, `rmb` - Mouse clicks
- `m(x, y)` - Move mouse to absolute position
- `r(x, y)` - Move mouse relative to current position
- `w(x, y)` - Scroll wheel
- `:500` - Sleep for 500ms

**Example: Complex Input Sequence**
```havel
^!t => {
    > "test@example.com" :250 > {Tab} "password123" :500 > {Enter}
    > m(100, 200) lmb :100 > r(50, 0)
}
```

### Sort with Custom Comparator

The `sort()` and `sorted()` functions now accept custom comparators:

```havel
// Default sort (ascending)
let a = [3, 1, 2]
a.sort()  // [1, 2, 3]

// Custom comparator (descending)
let a = [3, 1, 2]
a.sort((x, y) => y - x)  // [3, 2, 1]

// Sort strings by length
let names = ["alice", "bob", "charlie"]
names.sort((a, b) => a.length - b.length)

// Non-mutating version
let a = [3, 1, 2]
let b = sorted(a)  // a unchanged, b = [1, 2, 3]
let c = sorted(a, (x, y) => y - x)  // [3, 2, 1]
```

### Object Sorting with `sortByKey()`

Sort arrays of objects by a specific key:

```havel
let arr = [
    {name: "Bob", age: 30},
    {name: "Alice", age: 25},
    {name: "Charlie", age: 35}
]

// Sort by age (ascending)
arr.sortByKey("age")
// => [{age: 25, name: Alice}, {age: 30, name: Bob}, {age: 35, name: Charlie}]

// Sort by name (descending)
arr.sortByKey("name", (a, b) => b > a)

// With custom comparator
arr.sortByKey("age", (a, b) => b - a)  // Descending by age
```

### Monitor Methods

Get information about connected monitors:

```havel
// Get all monitors
let monitors = window.getMonitors()
for mon in monitors {
    print("Monitor: " + mon.name)
    print("  Position: (" + mon.x + ", " + mon.y + ")")
    print("  Size: " + mon.width + "x" + mon.height)
    print("  Primary: " + mon.isPrimary)
}

// Get combined area of all monitors
let area = window.getMonitorArea()
print("Total desktop area: " + area.width + "x" + area.height)
print("Bounding box: (" + area.x + ", " + area.y + ")")
```

**Monitor Object Properties:**
- `name` - Monitor name (e.g., "HDMI-0", "DVI-D-0")
- `x`, `y` - Position in virtual desktop
- `width`, `height` - Resolution
- `isPrimary` - true if primary monitor

**Example: Multi-Monitor Setup**
```havel
// Two monitors: 1366x768 at (0, 312) + 1920x1080 at (1366, 0)
let monitors = window.getMonitors()
// => [
//   {name: "DVI-D-0", x: 0, y: 312, width: 1366, height: 768, isPrimary: false},
//   {name: "HDMI-0", x: 1366, y: 0, width: 1920, height: 1080, isPrimary: true}
// ]

let area = window.getMonitorArea()
// => {x: 0, y: 0, width: 3286, height: 1080}
```

### Combo Hotkey Improvements

Combo hotkeys now correctly distinguish between left and right modifiers:

```havel
// Right Shift + Wheel Up/Down
@RShift & WheelUp => zoom(1)
@RShift & WheelDown => zoom(0)

// Left Shift + Wheel (different action)
@LShift & WheelUp => scrollUp()
@LShift & WheelDown => scrollDown()

// Mouse button combos
@LButton & RButton => toggleFeature()
@RButton & WheelUp => nextItem()
```

**Key Points:**
- `@RShift` requires Right Shift specifically (not Left Shift)
- `@LShift` requires Left Shift specifically
- Same for `@LCtrl`, `@RCtrl`, `@LAlt`, `@RAlt`
- Wheel events work correctly in combos

### Pixel and Image Automation

Powerful screen automation primitives for game bots, UI automation, and visual conditionals.

#### Pixel Operations

```havel
// Get pixel color at position
let c = pixel.get(500, 300)
print(c.r, c.g, c.b)  // RGB values (0-255)
print(c.hex)          // Hex string: "#FF0000"

// Check if pixel matches color (with tolerance)
if pixel.match(500, 300, "#ff0000", 10) {
    print("Pixel is red (±10 tolerance)")
}

// Wait for pixel color (with timeout)
if pixel.wait(500, 300, "#00ff00", 10, 3000) {
    print("Green pixel appeared within 3 seconds")
}
```

#### Image Search

```havel
// Find image on screen
let btn = image.find("ok.png")
if btn {
    print("Found at: " + btn.x + ", " + btn.y)
    print("Size: " + btn.w + "x" + btn.h)
    print("Confidence: " + btn.confidence)
    
    // Click on the found image
    > m(btn.centerX, btn.centerY) lmb
}

// Find with region (performance optimization)
let region = pixel.region(0, 0, 800, 600)
let btn = image.find("ok.png", region)

// Wait for image to appear
let btn = image.wait("loading.png", pixel.region(0,0,1920,1080), 5000)
if btn {
    print("Loading complete!")
}

// Check if image exists
if image.exists("enemy.png") {
    print("Enemy detected!")
}

// Count occurrences
let count = image.count("coin.png")
print("Found " + count + " coins")

// Find all occurrences
let matches = image.findAll("star.png")
for match in matches {
    print("Star at: " + match.x + ", " + match.y)
}
```

#### OCR (Optical Character Recognition)

```havel
// Read text from screen region
let text = ocr.read(pixel.region(100, 200, 400, 300))
print("Recognized: " + text)

// With language and character whitelist
let text = ocr.read(
    pixel.region(100, 200, 400, 300),
    "eng",      // Language
    "0123456789"  // Only digits
)
```

#### Screenshot Cache (Performance)

For loops that check pixels/images repeatedly, use screenshot caching:

```havel
// Enable caching (100ms expiry)
pixel.setCacheEnabled(true, 100)

// Manual capture
pixel.captureScreen()

// Now multiple operations use the same screenshot
while condition {
    if image.exists("enemy.png") {
        // ...
    }
    if pixel.match(100, 100, "#ff0000") {
        // ...
    }
    sleep(50)
}

// Clear cache when done
pixel.clearCache()
```

**Performance Tips:**
- Always use region-based search when possible
- Enable caching for tight loops
- Use `exists()` instead of `find()` when you only need a boolean
- Adjust image match threshold (0.0-1.0) for speed vs accuracy

---

### Traits (Interface-Based Polymorphism)

Traits define interfaces that types can implement without inheritance:

```havel
// Define a trait
trait Drawable {
  fn draw()
}

trait Area {
  fn area()
}

// Implement trait for a struct
struct Circle {
  radius
  fn init(r) {
    this.radius = r
  }
}

impl Drawable for Circle {
  fn draw() {
    print("Drawing circle with radius " + this.radius)
  }
}

impl Area for Circle {
  fn area() {
    return 3.14 * this.radius * this.radius
  }
}

// Use trait methods
let c = Circle.new(10)
c.draw()   // "Drawing circle with radius 10"
print(c.area())  // 314

// Check trait implementation
implements(c, "Drawable")  // true
implements(c, "Area")      // true
implements(c, "Unknown")   // false
```

**Key Points:**
- Traits define method signatures (no bodies)
- `impl Trait for Type` provides implementations
- Methods are injected into type's method table
- Multiple traits per type supported
- No inheritance hierarchy needed

### const (Immutable Bindings)

`const` creates immutable variable bindings:

```havel
const x = 10
x = 20  // Error: Cannot assign to const variable

// Works with objects and arrays
const obj = {a: 1}
obj.a = 2    // OK - modifying property
obj = {}     // Error - rebinding const

const arr = [1, 2]
arr.push(3)  // OK - modifying array
arr = []     // Error - rebinding const
```

**Use Cases:**
- Configuration values
- Constants that shouldn't change
- Preventing accidental reassignment

### repeat Statement (Enhanced)

`repeat` now accepts variables and expressions:

```havel
// Literal count
repeat 5 {
  print("iteration")
}

// Variable count
let n = 3
repeat n {
  print("repeat " + n)
}

// Expression count
repeat 2 + 3 {
  print("expression count")
}

// Inline form
repeat 3 print("inline")
```

### Shell Commands

Two ways to execute shell commands:

```havel
// $ command - Fire-and-forget execution
$ firefox
$ touch /tmp/file
$ ~/.bin/script.sh arg1 arg2

// `command` - Capture output
let out = `echo "hello"`
print(out.stdout)     // "hello"
print(out.exitCode)   // 0
print(out.success)    // true

// Error handling
let result = `ls /nonexistent`
if !result.success {
  print("Error: " + result.stderr)
}

// Backtick returns object:
// - stdout: Command output
// - stderr: Error output
// - exitCode: Exit code
// - success: Boolean
// - error: Error message
```

### Screenshot Module (Enhanced)

Screenshot functions now return image data:

```havel
// Full screenshot
let result = screenshot.full()
print(result.path)    // File path
print(result.width)   // Image width
print(result.height)  // Image height
print(result.data)    // Base64-encoded PNG

// Region screenshot
let region = screenshot.region(100, 100, 200, 200)

// Monitor screenshot
let monitor = screenshot.monitor()

// All functions return:
// {path, data, width, height}
```

### Struct Methods and Type() Constructor

Structs now support methods and constructor sugar:

```havel
struct MousePos {
  x
  y
  
  fn init(x, y) {
    this.x = x
    this.y = y
  }
  
  fn moveTo(speed) {
    mouse.moveTo(this.x, this.y, speed)
  }
  
  fn distance(other) {
    let dx = this.x - other.x
    let dy = this.y - other.y
    return sqrt(dx*dx + dy*dy)
  }
}

// Constructor sugar
let p1 = MousePos.new(100, 200)  // Traditional
let p2 = MousePos(300, 400)      // Sugar - same result

// Method calls
p1.moveTo(10)
p1.distance(p2)
```

### Type Conversions

New type conversion functions:

```havel
// Numeric conversions
int(3.9)      // 3 (truncates)
num("3.14")   // 3.14

// String conversion
str(123)      // "123"
str(3.14)     // "3.14"

// Container constructors
list(1, 2, 3)      // [1, 2, 3]
list(existingArr)  // Copy array
tuple(1, 2, 3)     // Fixed-size list
set_(1, 2, 2, 3)   // [1, 2, 3] (unique)
```

### approx() - Fuzzy Float Comparison

Relative tolerance for floating point comparison:

```havel
// Default tolerance (1e-9)
approx(0.1 + 0.2, 0.3)  // true

// Custom epsilon
approx(0.1 + 0.2, 0.3, 1e-15)  // false (too strict)
approx(0.1 + 0.2, 0.3, 1e-6)   // true (relaxed)

// Uses relative tolerance:
// |a - b| <= eps * max(1, |a|, |b|)
```

### Sleep Statement (Global)

`:duration` sleep syntax now works anywhere:

```havel
// In any context
:100
print("after 100ms")

// Duration formats
:1s       // 1 second
:1m30s    // 1 minute 30 seconds
:500      // 500 milliseconds
```

---

## Type System (Gradual Typing)

Havel supports optional type annotations for better code organization and IDE support. Types are metadata-only by default - runtime validation is optional based on TypeMode.

### Type Modes
```havel
// Set type checking mode
TypeChecker.setMode("none")   // Ignore types (default)
TypeChecker.setMode("warn")   // Print warnings on mismatch
TypeChecker.setMode("strict") // Runtime error on mismatch
```

### Struct Definitions
```havel
// Typed struct
struct Vec2 {
    x: Num
    y: Num
}

struct Player {
    pos: Vec2
    health: Num
    name: Str
}

// Untyped struct (dynamic fields)
struct Loose {
    x
    y
}
```

### Enum Definitions
```havel
// Simple enum
enum Color {
    Red
    Green
    Blue
}

// Enum with payloads (sum types)
enum Result {
    Ok(value)
    Err(message)
}
```

### Type Annotations
```havel
// Typed variable
let v: Vec2 = { x: 10, y: 20 }

// Untyped variable (dynamic)
let p = { pos: v, health: 100, name: "arc" }

// Function with type annotations
let add: Func(Num, Num) -> Num = fn(a, b) -> a + b
```

### Struct Construction
```havel
// Using struct literal
let v1 = Vec2 { x: 10, y: 20 }

// Using object literal (validated against struct type)
let v2: Vec2 = { x: 30, y: 40 }

// Field access
print(v1.x)  // 10
v1.y++       // v1.y is now 21
```

### Enum Construction
```havel
// Simple enum
let c = Color.Red

// Enum with payload
let r = Result.Ok(42)
let e = Result.Err("Something went wrong")

// Pattern matching (future)
match r {
    Ok(v) => print("Success: " + v)
    Err(e) => print("Error: " + e)
}
```

---

## CLI Usage

### Running Scripts

```bash
# Full mode (with IO, hotkeys, GUI)
havel script.hv

# Pure mode (no IO/hotkeys - for testing)
havel --run script.hv
havel run script.hv

# REPL mode
havel --repl
havel -r

# GUI-only mode (no hotkeys)
havel --gui

# Show help
havel --help
havel -h
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--run`, `-r` | Run script in pure mode (no IO/hotkeys) |
| `--repl` | Start interactive REPL |
| `--gui` | GUI-only mode (no hotkeys) |
| `--error`, `-e` | Stop on first error/warning |
| `--help`, `-h` | Show help |

### Error Reporting

Runtime errors now include line and column information:

```
2026-03-01 05:12:51.032 [ERROR] Runtime Error at line 4: Undefined variable: undefinedVar
```

### Pure Mode (--run)

Pure mode executes scripts without IO, hotkeys, display, or GUI. Useful for:

- Testing scripts that auto-exit
- Running algorithms (fibonacci, sorting, etc.)
- Type system testing
- Benchmarking

```bash
# Run a test script
havel --run scripts/test_types.hv

# Run fibonacci
havel --run scripts/fibonacci.hv

# Stop on first error
havel --run --error script.hv
```

---

## Language Server Protocol (LSP)

Havel includes a full-featured language server for IDE integration.

### Starting the LSP Server

```bash
# Start LSP server
havel-lsp
```

### VSCode Extension

A VSCode extension is available in the `vscode-extension` directory:

```bash
cd vscode-extension
npm install
npm run compile

# In VSCode: Extensions → ⋯ → Install from VSIX
# Select: vscode-extension/havel-language-0.1.0.vsix
```

#### Features

- ✅ **Syntax Highlighting** - Full Havel grammar support
- ✅ **Diagnostics** - Real-time error reporting with line/column
- ✅ **Hover** - Type information on hover
- ✅ **Go to Definition** - F12 navigation
- ✅ **Document Symbols** - Outline view (Ctrl+Shift+O)
- ✅ **Bracket Matching** - Auto-close brackets
- ✅ **Comment Toggling** - Ctrl+/ for line comments

#### Configuration

In VSCode settings (`settings.json`):

```json
{
  "havel.languageServer.path": "/path/to/havel-lsp",
  "havel.languageServer.trace": "off"
}
```

### Grammar Rules

#### Comments

Comments use `//` for line comments and `/* */` for block comments:

```havel
// This is a line comment
print("Hello")  // Inline comment

/* This is a
   block comment */
```

**Important:** `#` is **NOT** a comment character. It is used for:
- Hotkey modifiers: `#f1`, `#!Esc`, `#^c`
- Set literals: `#{1, 2, 3}`

#### Member Expression Assignment

Assign to object properties using dot notation:

```havel
let obj = {}
obj.name = "test"
obj.value = 42

print(obj.name)  // "test"
print(obj.value) // 42
```

#### Debug Module

The `debug` module provides runtime introspection:

```havel
// Show AST after parsing
debug.showAST(true)

// Stop execution on first error
debug.stopOnError(true)

// Get interpreter state
print(debug.interpreterState())
```

### Configuration Options

#### Process Priority

Control process priority and thread count in config:

```ini
[Advanced]
# Process priority: -20 (highest) to 19 (lowest)
# Default: 0 (normal priority)
ProcessPriority=0

# Number of worker threads for hotkey execution
# Range: 1 to 32
# Default: 4
WorkerThreads=4
```

#### Clipboard Manager

Multi-select and animation features:

```havel
// Multi-select support (in GUI)
// Hold Ctrl to select multiple items
// Press Enter to copy all selected items

// Animations
// Window fade-in/fade-out on show/hide
// Item fade animation on delete
```

### Window Manager Detection

The window manager detector now uses EWMH properties for reliable detection:

```havel
// Get active window title
let title = window.getTitle()

// Get active window class
let className = window.getClass()
```

Detection works correctly with:
- ✅ X11 sessions (including `startx`)
- ✅ Wayland compositors
- ✅ Display managers (gdm, sddm, lightdm)

### Exception Containment

All interpreter exceptions are now contained to prevent crashes:

```havel
// Conditional hotkeys with errors won't crash
mode == "gaming" => {
    F1 => click()  // Safe even if condition fails
}
```

---

## Configuration System

### Config Sections (Hyprland-Style)

Declarative configuration blocks with expression support:

```havel
// Basic config section
general {
  log = true
  name = "my-config"
}

// Expression evaluation
let brightnessStep = 2
brightness {
  step = num(brightnessStep) / 10  // 0.2
  current = brightnessManager.getBrightness()
}

// Hyprland-style arguments
monitor "HDMI-0" "primary" {
  brightness = 0.8
  enabled = true
}

// Access via config namespace
print(config.general.log)           // true
print(config.brightness.step)       // 0.2
print(config.monitor.brightness)    // 0.8
```

### Debounced Saves

Config changes are automatically saved with 500ms debounce:

```havel
// Multiple changes trigger single save
config.set("General.Key1", "value1", true)
config.set("General.Key2", "value2", true)
config.set("General.Key3", "value3", true)
// Single save after 500ms, not 3 saves

// Force immediate save
config.forceSave()

// Batch mode
config.beginBatch()
config.set("A", "1", true)
config.set("B", "2", true)
config.endBatch()  // Single save
```

### Hot Reload

Config file changes are detected instantly using inotify (Linux):

```havel
// Start watching (automatic in most cases)
config.startFileWatching()

// Changes to havel.cfg trigger instant reload
// No polling - zero CPU usage when idle
```

---

## Language Reference

For complete grammar specification, see [GRAMMAR.md](GRAMMAR.md).

### Quick Syntax Reference

```havel
// Hotkeys
F1 => { print("Hello") }
^!T => { run("terminal") }

// Variables
let x = 10
const PI = 3.14

// Functions
fn add(a, b) { return a + b }

// Structs
struct Point { x, y }
let p = Point(10, 20)

// Config sections
display {
  brightness = 0.8
}
print(config.display.brightness)

// Control flow
if x > 0 { print("positive") }
for i in 1..10 { print(i) }
repeat 5 { print("loop") }
```

### Standard Library

Use `help()` in REPL or see [GRAMMAR.md#standard-library](GRAMMAR.md#standard-library) for complete list.

---

## Exception Containment

All interpreter exceptions are contained to prevent crashes:

```havel
// Conditional hotkeys with errors won't crash
mode == "gaming" => {
    F1 => undefinedFunction()  // Error logged, continues
}
```

---
