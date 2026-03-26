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
send("^c")
play("music.mp3")
exit()
read("/tmp/file.txt")
write("/tmp/file.txt", "content")
click()
move(100, 200)
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
- `click(button, state)` - Mouse actions
- `move(x, y, speed, acceleration)` - Mouse positioning

### 2. Use Statement Flattening (Structured Path)
Import module functions directly into current scope:

```havel
use io.*, media.*, fs.*

// Direct access without module.method()
move(10, 0)     // instead of mouse.move(10, 0)
play("song.mp3")     // instead of media.play("song.mp3")
read("/tmp/file.txt") // instead of fs.read("/tmp/file.txt")
```

**Available Modules:**
- `io` - Input/output operations
- `mouse` - Mouse operations
- `media` - Media playback control
- `fs` - File system operations
- `clipboard` - Clipboard operations
- `time` - Date/Time operations
- `window` - Window management

### 3. With Blocks (Contextual Scoping)
Create temporary scope with object members available:

```havel
with io, mouse {
    scroll(10, 0)     // Direct access inside with block
    click()               // No io. prefix needed
    send("hello")         // All functions available
    listDevices()
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
    print "Caught: $error"
}
```

#### Try with Finally
```havel
try {
    // Operation that needs cleanup
    file = fs.open("/tmp/data.txt")
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
mouse.scroll(3)         // Scroll up
mouse.scroll(-3)        // Scroll down
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
```

### Configuration Block
```havel
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
```

**Config Keys:**
- `Havel.debug = true`
- `Havel.io.log = true`
- `Havel.io.evdev = true`
- `Havel.mouse.log = true`
- `Havel.window.monitor = true`
- `Havel.window.log = false`
- `Havel.hotkey.conditional = true`

**Accessing Config Values:**
```havel
// Config values are stored with Havel. prefix
use config
debugMode = config.get("Havel.debug")
windowMonitoring = config.get("Havel.window.monitor")
```

## Standard Library Modules

### Built-in Modules
Havel provides comprehensive built-in modules for common automation tasks:

#### IO Module (Input/Output)
```havel
// Direct access
use io.*
send("hello")            // Send keystrokes
keyDown("ctrl")          // Key down
keyUp("ctrl")            // Key up
suspend()                // Toggle hotkey suspend
```

#### Media Module (Media Control)
```havel
// Direct access
use media.*
play()                   // Play/pause
pause()                  // Pause playback
stop()                   // Stop playback
next()                   // Next track
previous()               // Previous track
```

#### Filemanager Module (File Operations)
```havel
// Direct access
use fs.*
read("/path/to/file")           // Read file
write("/path/to/file", "content") // Write file
exists("/path/to/file")         // Check existence
size("/path/to/file")           // Get file size
```

#### Clipboard Module (Clipboard Operations)
```havel
// Direct access
use clipboard.*
get()                    // Get clipboard content
set("text")              // Set clipboard content
clear()                  // Clear clipboard
send()                   // Send clipboard as keystrokes
send("hello")            // Send text as keystrokes

// Global variable
#clip = "text"      // Set clipboard
print(#clip)        // Get clipboard
clipboard.send()    // Send clipboard content
clipboard.clear()   // Clear clipboard
```

#### Strings
```havel
"hello".upper()             // Uppercase
"HELLO".lower()             // Lowercase
"  text  ".trim()            // Remove whitespace
"text".replace("t", "T")  // Replace text
```

#### Math Module (Mathematical Functions)
```havel
// Direct access
use math.*

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

// Random numbers using urandom
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

#### Async Module (Concurrency)
```havel
// Direct access
use async.*

// Timing
sleep(1000)                // Sleep for 1000ms
let ts = time.now()        // Current timestamp (ms)

// Task spawning (requires closure support)
let taskId = async.run(fn() {
    // Background task
    sleep(500)
    print("Done!")
})

// Task management
async.await(taskId)        // Wait for task completion
async.cancel(taskId)       // Cancel running task
let running = async.isRunning(taskId)

// Channel communication
async.channel("mychan")    // Create channel
async.send("mychan", "Hello")  // Send value
let msg = async.receive("mychan")  // Blocking receive
let msg2 = async.tryReceive("mychan")  // Non-blocking receive
async.channel.close("mychan")  // Close channel
```

**Async Features:**
- `sleep(ms)` - Delay execution (also available as global)
- `time.now()` - Current timestamp in milliseconds
- `async.run(fn)` - Spawn background task (returns task ID)
- `async.await(taskId)` - Block until task completes
- `async.cancel(taskId)` - Cancel running task
- `async.isRunning(taskId)` - Check if task is still running
- `async.channel(name)` - Create named channel
- `async.send(name, value)` - Send value to channel
- `async.receive(name)` - Blocking receive from channel
- `async.tryReceive(name)` - Non-blocking receive (returns "" if empty)
- `async.channel.close(name)` - Close channel

**Note:** `async.run()` currently requires closure support to execute VM functions. Until then, it can spawn placeholder tasks.

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
fn abs(x) {
    "custom abs: " + x;
}

// Override module function
fn math.abs(x) {
    "custom math.abs: " + x;
}

// Override with different signature
fn math.sin(x, y) {
    "custom sin with 2 args: " + x + ", " + y;
}
```

**Overriding Rules:**

1. **Direct Function Override**: Define a function with the same name as a built-in function
   - `abs(-5)` calls custom `abs()` instead of built-in
   - Takes precedence over built-in functions

2. **Module Function Override**: Define a function with qualified module name
   - `math.abs(-5)` calls custom `math.abs()` instead of built-in
   - `gui.notify("title", "msg")` calls custom `gui.notify()` instead of built-in

3. **Signature Flexibility**: Override with different parameter counts
   - Built-in `math.sin(x)` can be overridden with `math.sin(x, y)`
   - Allows extending functionality while maintaining compatibility

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

**WindowMonitor Integration:**
Conditional hotkeys now use `WindowMonitor` for efficient window information caching. This provides:
- Faster condition evaluation (cached vs. repeated X11 calls)
- Automatic window info updates (100ms poll interval)
- Transparent fallback to IO if WindowMonitor unavailable

```havel
// Conditional hotkeys benefit from WindowMonitor caching
F1 when title == "Firefox" => {
    // Window info retrieved from cache (fast!)
    print("Firefox is active")
}

when mode gaming && exe == "steam" {
    // Both conditions use cached window info
    F2 => print("Gaming on Steam")
}
```

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
F1 when mode gaming && title == "Game" => {
    print("Gaming in game window")
}

F2 if mode == "work" && class == "code" => {
    print("Coding at work")
}
```

### When Blocks (Group Hotkeys by Condition)
```havel
when mode gaming {
    LButton & RButton => {
        print "Attack!"
        automation.autoClick("LButton")
    }
    Enter => {
        print "Defend!"
        mouse.down("right")
    }
    F1 => {
        print "Special!"
        for i in [1, 2, 3, 4] {
            send i
        }
    }
}

when mode work {
    ^b => {
        print("Build project")
        let path = window.active().programPath
        run "cd $path && npm run build"
    }
}
```

### Nested When Blocks
```havel
when mode gaming {
    when title Steam {
        F1 => { print("Steam overlay"); send("+{Tab}") }
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
    | upper
    | replace " " "_"
    | send

// Complex workflows
^V => {
    clipbooard.get
        | trim
        | json.stringify
        | send
}

// Conditional logic
F2 => {
    if clipbooard.get | has "error" {
        send "DEBUG: "
    }
    clipbooard.get | send
}

// Dynamic conditional hotkeys
F1 if mode == "gaming" => send "{Enter}"
^!A if title.includes("Chrome") => send "^F12"

// When blocks for grouped conditions
when mode == "gaming" {
    ^!A => audio.unmute
    ^!B => play
    ^!C => media.next
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
clipboard.send(text) // Send clipboard/text as keystrokes
clipboard.history    // Access clipboard history
#clip           // Global clipboard variable (get/set)

Window Management Module
window.active        // Get active window
window.list          // List all windows
window.focus(name)   // Focus specific window
window.min()         // Minimize window
window.max()         // Maximize window
window.groups()      // Get all window group names
window.groupGet(group) // Get windows in group
window.inGroup(title, group) // Check if window in group
window.findInGroup(group) // Find first window in group

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
mouse.click(button, state)        // Click mouse button (left, right, middle, side1, side2 or 1,2,3,4,5) (click, press, release or 0,1,2)
mouse.down(button)         // Press and hold button
mouse.up(button)           // Release button
mouse.move(x, y, speed, acceleration)           // Move to absolute position
mouse.moveRel(dx, dy, speed, acceleration)      // Move relative to current position
mouse.scroll(dy, dx)        // Scroll wheel (positive=up, negative=down)
mouse.pos()        // Get current position {x, y}
mouse.setSpeed(speed)      // Set mouse speed (default: 5)
mouse.setAccel(accel)      // Set acceleration (default: 1.0)
mouse.setDPI(dpi)

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
mouse.scroll(3)    // Scroll up 3 notches
mouse.scroll(-5)   // Scroll down 5 notches

// Precise positioning
mouse.move(1500, 500, 5, 1.2)
pos = mouse.pos()
print("Mouse at: " + pos.x + ", " + pos.y)

// Speed and acceleration
mouse.setSpeed(10)     // Faster movement
mouse.setAccel(2.0)    // More acceleration

Debugging & Testing
	* Interactive REPL - Test code snippets
	* Unit Testing Framework - Built-in testing capabilities
	* Performance Profiler - Optimize automation scripts

Getting Started
# Quick start
./build.sh 1 build install
havel --version
echo 'F1 => send "Hello, Havel!"' > hello.hv
havel hello.hv

----

🏰 Built with the strength of Havel, the reliability of steel, and the precision of gears.

"In automation, as in battle, preparation and reliability triumph over complexity."

---
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
let groups = window.groups()

// Get windows in a group
let wins = window.groupGet("browsers")

// Check if current window is in group
if window.inGroup(window.title, "browsers") {
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

### IO Suspend/Resume

Toggle hotkey processing on/off:

```havel
io.suspend()    // Toggle suspend state
io.suspend(false) // Unsuspend (enable hotkeys)
io.suspend(true) // Suspended (disable hotkeys)
```

### Global Convenience Functions

Common mouse operations available as globals:

```havel
click()           // Left click
dblclick()        // Double click
move(x, y)        // Move to position
moveRel(dx, dy)   // Relative movement
scroll(dy, dx)    // Scroll (vertical, horizontal)
```

### Spread Operator

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
    "Hello World"
    {Enter}
    lmb
    m(100, 200)
}

// Also works with explicit > prefix
> {Esc} :500
^!t => {
    > "Hello World" :500 > {Enter}
    :100
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
- `w(y, x)` - Scroll wheel
- `:500` - Sleep for 500ms

**Example: Complex Input Sequence**
```havel
^!t => {         
    > "test@example.com" :250 {Tab} "password123" :500 {Enter}
    > m(100, 200) lmb :100 r(50, 0)
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

### Display module

#### Monitor Methods

Get information about connected monitors:

```havel
// Get all monitors
let monitors = display.monitors()
for mon in monitors {
    print("Monitor: " + mon.name)
    print("  Position: (" + mon.x + ", " + mon.y + ")")
    print("  Size: " + mon.width + "x" + mon.height)
    print("  Primary: " + mon.isPrimary)
}

// Get combined area of all monitors
let area = display.monitorArea()
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
let monitors = display.monitors()
// => [
//   {name: "DVI-D-0", x: 0, y: 312, width: 1366, height: 768, isPrimary: false},
//   {name: "HDMI-0", x: 1366, y: 0, width: 1920, height: 1080, isPrimary: true}
// ]

let area = display.monitorArea()
// => {x: 0, y: 0, width: 3286, height: 1080}
```

### Combo Hotkey Improvements

Combo hotkeys now correctly distinguish between left and right modifiers:

```havel
// Right Shift + Wheel Up/Down
RShift & WheelUp => zoom(1)
RShift & WheelDown => zoom(0)

// Left Shift + Wheel (different action)
LShift & WheelUp => scrollUp()
LShift & WheelDown => scrollDown()

// Mouse button combos
LButton & RButton => toggleFeature()
RButton & WheelUp => nextItem()
```

**Key Points:**
- `RShift` requires Right Shift specifically (not Left Shift)
- `LShift` requires Left Shift specifically
- Same for `LCtrl`, `RCtrl`, `LAlt`, `RAlt`
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

Type conversion functions:

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
set(1, 2, 2, 3)   // [1, 2, 3] (unique)
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
typeChecker.setMode("none")   // Ignore types (default)
typeChecker.setMode("warn")   // Print warnings on mismatch
typeypeChecker.setMode("strict") // Runtime error on mismatch
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

# Show help
havel --help
havel -h
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--run`, `-r` | Run script in pure mode (no IO/hotkeys) |
| `--repl` | Start interactive REPL |
| `--error`, `-e` | Stop on first error/warning |
| `--help`, `-h` | Show help |

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

## Configuration System

### Config Sections

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

// Access via config
print(config.general.log)           // true
print(config.brightness.step)       // 0.2
print(config.monitor.brightness)    // 0.8
// config gets automaticly saved from config blocks
```
Config changes that get automatically saved with debounce:

```havel
config.debounce(800)
// Multiple config changes trigger single save
config.set("General.Key1", "value1")
config.set("General.Key2", "value2")
config.set("General.Key3", "value3")

// Force immediate save
config.save()

// Batch mode
config.begin()
config.set("A", "1", true)
config.set("B", "2", true)
config.end()  // Single save
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

### Script Imports

Import and reuse Havel scripts as modules:

```havel
// Import script file with alias
use "gaming.hv" as game
use "work.hv" as work

// Use exported functions
game.start()
work.active

// Access exported variables
if game.active {
    print("Gaming mode is active")
}
```

**Exporting from modules:**
```havel
// gaming.hv
export fn start() {
    brightness(50)
    volume(80)
}

export fn stop() {
    brightness(100)
    volume(50)
}

export let active = false

export fn toggle() {
    active = !active
    if active { start() } else { stop() }
}
```

**Features:**
- Scripts run in isolated environments
- Exports create module object properties
- Multiple imports supported
- Circular import detection

---

### Enhanced Mode System

#### Signals

Define system facts that can be monitored:

```havel
// Signals are boolean facts about system state
signal steam_running = window.any(exe == "steam.exe")
signal gaming_focus = active.exe == "steam.exe"
signal many_windows = window.count() > 6
signal youtube_open = window.any(title ~ ".*YouTube.*")
```

**Check signals:**
```havel
when steam_running {
    notify("Steam launched")
}

when mode == "gaming" && !steam_running {
    notify("Steam not running in gaming mode")
}
```

#### Mode Priority

Higher priority modes override lower priority:

```havel
mode gaming priority 10 {
    condition = gaming_focus
    enter { brightness(50); volume(80) }
    exit { brightness(100); volume(50) }
}

mode multitask priority 5 {
    condition = many_windows
    enter { workspace.set(2) }
}

// gaming mode will override multitask when both conditions are true
```

#### Mode Transition Hooks

Execute code on specific mode transitions:

```havel
mode gaming {
    condition = isGame()
    enter {
        brightness(50)
        volume(80)
    }
    exit {
        brightness(100)
        volume(50)
    }

    // Called when entering from specific mode
    on enter from "coding" {
        notify("leaving code for games, respect")
    }

    // Called when exiting to specific mode
    on exit to "default" {
        run("killall -9 steam") // rage quit
    }
}
```

#### Mode Statistics

Track time spent and transitions:

```havel
stats {
    // Get time spent in mode (seconds)
    let gamingTime = mode.time("gaming")
    print("Gaming time: " + gamingTime + " seconds")

    // Get transition count
    let switches = mode.transitions()
    print("Mode switches: " + switches)

    // Monitor mode duration
    on mode.change {
        if mode.duration > 3600 {  // 1 hour
            notify("been in " + mode.current + " for over an hour")
        }
    }
}
```

#### Mode API Reference

```havel
mode.current              // Current mode name (string)
mode.previous             // Previous mode name (string)
mode.time("gaming")       // Time in mode (seconds, number)
mode.transitions("gaming") // Transition count (number)
mode.set("gaming")        // Set mode explicitly
mode.list()               // List all modes (array)
mode.signals()            // List all signals (array)
mode.isSignal("steam")    // Check if signal active (boolean)
```

---

### Window Query API
Query windows with powerful filters:

```havel
// Get active window info
let win = window.active
print("Title: " + win.title)
print("Class: " + win.class)
print("EXE: " + win.exe)
print("PID: " + win.pid)

// Check if any window matches condition
if window.any(exe == "steam.exe") {
    print("Steam is running")
}

// Count matching windows
let chromeCount = window.count(class == "chrome")
print("Chrome windows: " + chromeCount)

// Filter windows
let youtubeWindows = window.filter(title ~ ".*YouTube.*")
for win in youtubeWindows {
    print("YouTube: " + win.title)
}

// Find first matching window
let steamWin = window.find(exe == "steam.exe")
if steamWin {
    print("Steam window: " + steamWin.title)
}
```

**Window Info Object:**
```havel
{
    id: 12345,        // Window ID
    title: "Steam",   // Window title
    class: "steam",   // Window class
    exe: "steam.exe", // Executable name
    pid: 1234         // Process ID
}
```

---

### Concurrency Primitives

#### Thread (Actor-Based)

Lightweight concurrent execution with message passing:

```havel
// Create thread with message handler
let worker = thread {
    on message(msg) {
        print("Received: " + msg)
    }
}

// Send messages
worker.send("hello")
worker.send("world")

// Control execution
worker.pause()   // Pause processing
worker.resume()  // Resume processing
worker.stop()    // Stop permanently

// Check status
if worker.running {
    print("Worker is running")
}
```

#### Interval

Repeating timer with full control:

```havel
// Create repeating timer
let timer = interval 1000 {
    print("Tick: " + time.now())

    if title ~ "YouTube" {
        audio.unmute()
    }
}

// Control timer
timer.pause()   // Pause ticking
timer.resume()  // Resume ticking
timer.stop()    // Stop permanently
```

#### Timeout

One-shot delayed execution:

```havel
// Execute after delay
timeout 5000 {
    print("5 seconds later")
    notify("Reminder: Take a break!")
}

// Can be cancelled
let t = timeout 10000 {
    print("This might not print")
}
t.cancel()  // Cancel before execution
```

#### Range

First-class range type:

```havel
// Create range
let workHours = 8..18  // 8 to 18

// Check membership
if time.hour in workHours {
    print("Working hours")
}

// Range methods
if workHours.contains(time.hour) {
    print("Still working")
}

// Inline range check
if time.hour in (21..24) {
    print("Night hours")
    brightness(0.2)
}
```

---

### MPV Controller Functions

Complete media player control:

```havel
// Volume control
mpv.volumeUp()           // Increase volume +5
mpv.volumeDown()         // Decrease volume -5
mpv.toggleMute()         // Toggle mute

// Playback control
mpv.stop()               // Stop playback
mpv.next()               // Next track
mpv.previous()           // Previous track

// Speed control
mpv.addSpeed(0.1)        // Increase speed by 0.1
mpv.addSpeed(-0.1)       // Decrease speed by 0.1

// Subtitle control
mpv.addSubScale(0.1)     // Increase subtitle scale
mpv.addSubScale(-0.1)    // Decrease subtitle scale
mpv.addSubDelay(0.1)     // Delay subtitles +0.1s
mpv.addSubDelay(-0.1)    // Advance subtitles -0.1s
mpv.subSeek(1)           // Seek to next subtitle
mpv.subSeek(-1)          // Seek to previous subtitle

// Property cycling
mpv.cycle("sub-visibility")           // Toggle subtitle visibility
mpv.cycle("secondary-sub-visibility") // Toggle secondary subtitles

// Subtitle text
let subtitle = mpv.copyCurrentSubtitle()
print("Current: " + subtitle)

// IPC control
mpv.ipcSet("/tmp/mpvsocket2")  // Set IPC socket path
mpv.ipcRestart()                // Restart IPC connection

// Screenshot
let pic = mpv.screenshot()
```

---

### KeyTap and KeyCombo

Tap and combo key detection:

```havel
// Tap detection (quick press/release)
on tap(lwin) => {
    if xfce { run("xfce4-popup-whiskermenu") }
    else { $ ["rofi", "-show", "drun"] }
}

// Combo detection (hold + other keys)
on combo(lwin) => {
    print("LWin held with another key")
}

// Specific key tap
on tap(escape) => {
    print("Escape tapped")
}
```

**Features:**
- Tap: Quick press and release
- Combo: Hold key + press other keys
- Mode-aware (works inside mode blocks)
- Automatic key filtering

---

### KeyDown and KeyUp

Raw key event handling:

```havel
// Catch ALL key down events
on keyDown {
    print("Key pressed: " + this.key)
}

// Catch ALL key up events
on keyUp {
    print("Key released: " + this.key)
}

// Catch specific keys only
on keyDown(lctrl, rctrl, lshift, rshift, esc) {
    mod = true
    print("Modifier pressed")
}

on keyUp(lctrl, rctrl, lshift, rshift, esc) {
    mod = false
    print("Modifier released")
}
```

**Features:**
- Fires on every key event
- Optional key filtering
- `this.key` contains key code
- Works with mode system

---

### File Fallback Helper

Clean file path fallback:

```havel
audio.play(firstExisting(
    "~/Music/night-theme.mp3",
    "~/Music/default-night.mp3",
    "~/Music/fallback-night.mp3"
))

```

---

### Configuration Enhancements

#### Nested Config Blocks

```havel
config {
    debug = true
    logKeys = true

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

    gaming {
        classes = ["steam", "lutris", "heroic"]
        exclude = ["browser", "discord"]
    }
}

// Access nested config
let debug = config.get("Havel.debug")
let monitoring = config.get("Havel.window.monitoring")
let gamingClasses = config.list("Havel.gaming.classes")
```

#### Config List Helper

```havel
// Better than split()
condition = class in config.list("Gaming.Classes")

// Supports comma-separated values
config {
    gaming {
        classes = "steam,lutris,heroic"
    }
}
```

---

### Modes Condition Functions

Define conditions once, reuse everywhere:

```havel
// Define reusable conditions
let isGame = (class in config.gaming.classes || title in config.gaming.titles)
          && !(class in config.gaming.exclude)

let isTerminal = exe in ["kitty", "alacritty", "wezterm",
                         "gnome-terminal", "tilix", "xterm"]

// Use in modes
mode gaming {
    condition = isGame()
    enter { ... }
}

mode terminal {
    condition = isTerminal()
    enter { ... }
}
```

---

### Mode Groups

Batch operations on multiple modes:

```havel
group "productivity" {
    modes: ["coding", "terminal", "typing"]

    on enter {
        brightness(80)
        workspace.set(2)
    }
}

// Apply to all modes in group
for mode in group.productivity {
    mode.enter {
        do_not_disturb(true)
    }
}
```

---

### Temporary Mode Overrides

High-priority temporary modes:

```havel
// Higher priority overrides other modes
mode meeting {
    condition = exe == "zoom" || exe == "teams"
    priority = 10  // Higher than normal modes

    enter {
        notify("Meeting mode: muted notifications")
        do_not_disturb(true)
    }
}
```

---

### Mode as Functions

Programmatic mode definition:

```havel
let GamingMode = Mode({
    condition = isGame()
    enter = fn() {
        brightness(50)
        volume(80)
    }
    exit = fn() {
        brightness(100)
        volume(50)
    }
})

register(GamingMode)

// Or inline
register(Mode({
    name = "coding"
    condition = active.class in ["code", "vim", "nvim"]
    enter = fn() { workspace.set(1) }
}))
```

---

### Complete Example

```havel
// Import modules
use "gaming.hv" as game
use "work.hv" as work

// Define signals
signal steam_running = window.any(exe == "steam.exe")
signal coding_focus = active.class in ["code", "vim", "nvim"]

// Define modes with priority
mode gaming priority 10 {
    condition = active.exe == "steam.exe"
    enter {
        brightness(50)
        volume(80)
        game.start()
    }
    exit {
        brightness(100)
        volume(50)
        game.stop()
    }
    on enter from "coding" {
        notify("Leaving code for games!")
    }
}

mode coding priority 5 {
    condition = coding_focus
    enter {
        brightness(80)
        workspace.set(1)
    }
}

// Reactions
when steam_running && mode != "gaming" {
    notify("Steam running in background")
}

// Hotkeys
F1 => game.toggle()
F2 => work.toggle()
F3 => {
    print("Current mode: " + mode.current)
    print("Time in mode: " + mode.time() + "s")
}

// Key events
on keyDown(esc) {
    if mode.current == "gaming" {
        notify("Quitting game?")
    }
}

// Interval timer
interval 60000 {
    if mode.duration > 3600 {
        notify("Take a break!")
    }
}
```

---

## Quick Reference Card

```havel
// Imports
use "file.hv" as alias    // Import script
alias.function()           // Use imported function

// Modes
mode name priority N { }   // Define mode with priority
signal name = expr         // Define signal
mode.current               // Get current mode
mode.time()                // Get time in mode
mode.transitions()         // Get transition count

// Windows
window.active              // Active window info
window.any(condition)      // Boolean: any match?
window.count(condition)    // Integer: count matches
window.filter(condition)   // Array: filter matches

// Concurrency
thread { on message(m) {} } // Actor thread
interval ms { }            // Repeating timer
timeout ms { }             // One-shot delay
start..end                 // Range literal

// Key Events
on tap(key) { }            // Tap detection
on combo(key) { }          // Combo detection
on keyDown { }             // Raw key down
on keyUp { }               // Raw key up

// MPV
mpv.addSpeed(delta)        // Change playback speed
mpv.addSubScale(delta)     // Change subtitle scale
mpv.subSeek(index)         // Seek subtitle
mpv.cycle(property)        // Cycle property
mpv.ipcSet(path)           // Set IPC socket

// Regex Matching
title ~ /YouTube/          // Regex match with /pattern/
title ~ "YouTube"          // Also works with strings
title matches /pattern/    // Explicit matches operator
class in ["steam", "lutris"]  // Membership test
class not in ["browser"]   // Negative membership

// Boolean Operators
mode == "gaming" and title ~ /Game/
mode == "work" or class == "code"
not (mode == "gaming")

// Shell Commands
$ "echo hello"             // Simple command
$ ["ls", "-la"]            // Array syntax (no shell)
$ "cmd1 | cmd2"            // Piped commands (Unix pipes)
```

---

## Advanced Expression Operators

### Regex Matching

Multiple syntaxes for regex matching:

```havel
// Tilde operator (shorthand)
if title ~ /YouTube/ {
    print("YouTube detected")
}

// Matches keyword
if title matches /.*Game.*/ {
    print("Game window")
}

// String patterns (escaped)
if title ~ ".*Steam.*" {
    print("Steam detected")
}
```

**Regex Features:**
- `/pattern/` - Literal regex syntax
- `~` - Shorthand match operator
- `matches` - Explicit keyword
- Full PCRE regex support

### Membership Operators

Check if value is in array:

```havel
// Positive membership
if class in ["steam", "lutris", "heroic"] {
    print("Gaming platform detected")
}

// Negative membership
if class not in ["browser", "discord"] {
    print("Not a distraction")
}

// With variables
let gamingApps = ["steam", "lutris"]
if exe in gamingApps {
    print("Gaming")
}
```

### Boolean Operators

Full boolean logic support:

```havel
// AND operator
if mode == "gaming" and title ~ /Game/ {
    print("Gaming in game window")
}

// OR operator
if mode == "work" or class == "code" {
    print("Productive mode")
}

// NOT operator
if not (mode == "gaming") {
    print("Not gaming")
}

// Complex expressions
if (mode == "gaming" and title ~ /Steam/) or
   (mode == "coding" and class == "code") {
    print("Active session")
}
```

**Operator Precedence:**
1. `not` (highest)
2. `and`
3. `or` (lowest)

Use parentheses for explicit grouping.

---

## Unix Pipes Implementation

Shell commands use efficient Unix pipes instead of temp files:

```havel
// Piped commands - 10-100x faster than temp files
$ "ps aux | grep chrome | wc -l"

// Complex pipelines
$ "cat /var/log/syslog | grep error | tail -10"

// Mixed with Havel
let count = $ "ps aux | grep havel | wc -l"
if count > 1 {
    print("Multiple Havel instances running")
}
```

**Implementation:**
- Uses `pipe()`/`fork()`/`dup2()`/`exec()`
- No temp file I/O
- Memory-based pipes
- Proper error handling
---

## Special Window Identifiers

Built-in variables for active window info:

```havel
// In conditions
mode gaming {
    condition = exe == "steam.exe"
}

// Direct access
print("Title: " + title)
print("Class: " + class)
print("EXE: " + exe)
print("PID: " + pid)

// In expressions
if class == "firefox" and title ~ "YouTube" {
    print("Watching YouTube on Firefox")
}
```

**Available Identifiers:**
- `title` - Window title (string)
- `class` - Window class (string)
- `exe` - Executable name (string)
- `pid` - Process ID (number)

**Use Cases:**
- Mode conditions
- Conditional hotkeys
- Window detection
- Process monitoring

## Memory Management

### Destructor Cleanup

Proper resource cleanup prevents leaks:

```havel
// Automatic cleanup on script exit/reload
thread { ... }      // Thread stopped
interval { ... }    // Timer stopped
timeout { ... }     // Timeout cancelled

// Environment cleared
let x = 5           // Variables freed
let obj = {}        // Objects freed

// HostContext cleared
mode gaming { }     // Mode data freed
signal test = ...   // Signal data freed
```

**What's Cleaned:**
- Thread pools
- Timer threads
- Environment variables
- Mode definitions
- Signal definitions
- Import manager

**Leak Prevention:**
- `Environment::clear()` - Clears all variables
- `HostContext::clear()` - Stops all managers
- `ImportManager::clear()` - Clears imports
- Proper shared_ptr usage

---

## ShellExecutor Architecture

Structured shell execution with result objects:

```havel
// Internal - ShellExecutor returns structured results
ShellResult {
    stdout: string
    stderr: string
    exitCode: int
    success: bool
    error: string
}

// Usage - automatic conversion
let result = $ "echo hello"
print(result)  // Prints stdout

// Error handling
$ "nonexistent" || {
    print("Command failed")
}
```

**Features:**
- Unified execution API
- Structured result handling
- Proper error propagation
- Pipe chain support

---
