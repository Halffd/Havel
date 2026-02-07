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