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
- [Examples](#examples)

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
- `sleep(ms)` - Delay execution
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
| `config` | Configuration block | `config { ... }` |
| `devices` | Device configuration | `devices { ... }` |
| `modes` | Mode configuration | `modes { ... }` |
| `try` | Exception handling | `try { ... } catch { ... } finally { ... }` |
| `catch` | Exception handler | `catch error { ... }` |
| `finally` | Cleanup block | `finally { ... }` |
| `throw` | Raise exception | `throw value` |

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
keyDown("ctrl")           // Key down
keyUp("ctrl")             // Key up
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
set("text")               // Set clipboard content
clear()                   // Clear clipboard
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

### Postfix Conditional Syntax
```
hotkey => action if condition
```

### Prefix Conditional Syntax
```
hotkey if condition => action
```

### When Blocks
```
when condition {
    hotkey1 => action1
    hotkey2 => action2
}
```

### Nested Conditions
```
when outer_condition {
    hotkey if inner_condition => action
}
```

All conditions are evaluated dynamically at runtime, allowing variables to change and trigger hotkeys accordingly.

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
clipboard.set(text)   // Set clipboard content  
clipboard.clear()    // Clear clipboard
clipboard.history    // Access clipboard history

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
window.min()    // Minimize window
window.max()    // Maximize window

System Integration Module
system.run(cmd)      // Execute system command
system.notify(msg)   // Show notification
system.beep()        // System beep
system.sleep(ms)     // Delay execution

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