---
title: "First Script"
description: "Write and run your first Havel script."
---

# First Script

## Hello World

Create `hello.hv`:

```hv
// hello.hv
fn greet(name) {
    "Hello, {name}!"
}

print(greet("world"))
```

Run it:

```bash
./build-debug/havel hello.hv
```

Output:

```
Hello, world!
```

## Script with Hotkeys

Create `hotkey_demo.hv`:

```hv
// hotkey_demo.hv
print("Hotkey demo running. Press Ctrl+Shift+A to trigger.")

// Simple hotkey: F1 prints a message
F1 => { print("F1 pressed") }

// Ctrl+Shift+A sends text
^+A => { send("Hello from Havel!") }

// Alt+C maps CapsLock to Escape (toggle)
!C => { map("capslock", "esc") }

// Conditional: only in Chrome
^!R if window.active.exe == "chrome" => { send("^F5") }

// When block: group hotkeys under a condition
when mode == "gaming" {
    ^!A => click()
    ^!B => click("right")
    F1 if health < 50 => send("e")
}
```

Run it:

```bash
./build-debug/havel hotkey_demo.hv
```

The script registers hotkeys and waits for events. Press `Ctrl+C` to exit.

## REPL

Start the interactive REPL:

```bash
./build-debug/havel --repl
```

```hv
havel> 1 + 2
3
havel> fn double(x) => x * 2
havel> double(21)
42
havel> .help
// Shows help topics
```

### REPL Commands

| Command | Description |
|---------|-------------|
| `.help` | Show help |
| `.exit` / `.quit` | Exit REPL |
| `.clear` | Clear screen |
| `.load file.hv` | Load and execute file |
| `.vars` | List global variables |
| `.fns` | List functions |
| `.hotkeys` | List registered hotkeys |

## Inline Execution

Run code directly without a file:

```bash
# Evaluate expression
./build-debug/havel -E 'print("inline: {1 + 2}")'

# Multiple statements
./build-debug/havel -E '
x = 10
y = 20
print(x + y)
'
```

## Lint a Script

Check syntax and compilation errors without running:

```bash
./build-debug/havel --lint script.hv
```

## Compile to Bytecode

```bash
# Compile to .hvc (bytecode)
./build-debug/havel --build script.hv -o script.hvc

# Run bytecode
./build-debug/havel script.hvc
```

## Run Tests

```bash
# Run all .hv files in a directory as tests
./build-debug/havel --test scripts/tests/
```

---

**Previous:** [Installation](/getting-started/installation)
**Next:** [Basic Syntax →](/getting-started/basic-syntax)