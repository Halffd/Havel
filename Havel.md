HAVEL PROGRAMMING LANGUAGE
Design Document v1.0
----

📋 Summary
Havel is a declarative automation scripting language designed for hotkey management and workflow automation. Named after the resilient knight from Dark Souls, Havel embodies strength, reliability, and unwavering functionality in automation tasks.

## Navigation
- [Quick Syntax Reference](#quick-syntax-reference)
- [Keywords](#keywords)
- [Conditional Hotkeys](#conditional-hotkeys)
- [Examples](#examples)

----

🎯 Language Overview
Core Philosophy
	* Declarative Simplicity - Define what you want, not how to achieve it
	* Functional Flow - Data flows through transformation pipelines
	* Automation First - Built specifically for hotkeys and workflow automation
	* Platform Agnostic - Write once, run everywhere

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

## Keywords

| Keyword | Purpose | Example |
|---------|---------|---------|
| `let` | Variable declaration | `let x = 5` |
| `if/else` | Conditional execution | `if x > 0 { ... } else { ... }` |
| `when` | Conditional block | `when condition { ... }` |
| `fn` | Function definition | `fn name(args) => ...` |
| `return` | Function return | `return value` |
| `import` | Module import | `import module from "path"` |
| `config` | Configuration block | `config { ... }` |
| `devices` | Device configuration | `devices { ... }` |
| `modes` | Mode configuration | `modes { ... }` |

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