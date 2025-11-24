
**Havel** is a powerful utility for managing windows and hotkeys across multiple platforms, with a focus on Linux X11 environments. It allows for complex window management, hotkey configurations, and automated tasks via its own Havel scripting language.

## 📋 Table of Contents
- [Features](#features)
- [Installation](#installation)
- [Syntax Reference](#syntax-reference)
- [Keywords](#keywords)
- [Conditional Hotkeys](#conditional-hotkeys)
- [Examples](#examples)
- [Building](#building)
- [Troubleshooting](#troubleshooting)

## Features

- Global hotkey registration and management
- Dynamic conditional hotkeys with runtime evaluation
- Window tracking and manipulation
- Havel scripting language for complex automation (replacing Lua)
- Configurable via text files
- Cross-platform support

## Installation

### Prerequisites
- C++17 compatible compiler (GCC 9+ or Clang 10+)
- CMake 3.10+
- X11 development libraries (on Linux)
- Qt6 development libraries

## Syntax Reference

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
Example:
```hv
F1 when mode gaming => { print("Gaming mode active") }
!C if remapCapslock == false => map("capslock", "esc")
```

### When Blocks
Group multiple hotkeys under a shared condition:

```hv
when mode gaming {
    ^!A => click()
    ^!B => click("right")
    F1 if health < 50 => send("e")
}
```

All conditions are evaluated dynamically at runtime, allowing variables to change and trigger hotkeys accordingly.

## Examples

### Basic Hotkey
```
F1 => send("Hello World!")
```

### With Conditional Logic
```
^V when title "Discord" => {
    let clip = clipboard.get
    if clip | matches("^\d+") || has("secret") {
        let text = ""
        let length = len(clip)
        for i in 1..length {
            text += "*"
        }
        send("$clip")
    } else {
        send(clip)
    }
}
```

### Conditional Hotkey
```
^!A if window.active.title.contains("Chrome") => send("^F5")
```

### When Block
```
when mode coding {
    ^!S => send("^s")
    ^!F => send("^f")
    ^!G => send("^g")
}
```

### With Built-in Help
The language includes a built-in `help()` function for interactive learning:
- `help()` - Show main help page
- `help("syntax")` - Show syntax reference
- `help("keywords")` - Show all keywords
- `help("hotkeys")` - Show hotkey features
- `help("modules")` - Show available modules

## Building the Project
```bash
# Build release
./build.sh 1 build

# Run havel script file
./build-release/havel script.hv

# Run REPL
./build-release/havwl
```

Alternatively, you can use the Makefile:
```bash
make
```

## Troubleshooting

If you encounter build issues, please refer to the [BUILD_ISSUES.md](BUILD_ISSUES.md) file for common problems and solutions.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Directory Structure

- `/src` - Source code
- `/include` - Header files
- `/config` - Configuration files
- `/log` - Log files
- `/build` - Build directory
- `/scripts` - Scripts examples
