# Havel Language - Import System & Mode Enhancements

## Summary

Implemented comprehensive script import system and enhanced mode system with signals, priority, and window queries.

## Features Implemented

### 1. Script Imports

**Syntax:**
```havel
use "gaming.hv" as game
use "work.hv" as work
```

**Usage:**
```havel
game.start()
work.active
```

**Files:**
- `ImportManager.hpp/.cpp` - Manages script imports
- `Parser.cpp` - Parses `use "file.hv" as alias` syntax
- `AST.h` - `UseStatement` with file import support
- `StatementEvaluator.cpp` - Executes import statements

### 2. Enhanced Mode System

**Features:**
- **Signals** - Facts about system state
- **Priority** - Higher priority modes override lower
- **Transition hooks** - `on enter from`, `on exit to`
- **Statistics** - Time spent, transition count

**Syntax:**
```havel
// Signals
signal steam_running = window.any(exe == "steam.exe")
signal gaming_focus = active.exe == "steam.exe"

// Modes with priority
mode gaming priority 10 {
    condition = gaming_focus
    enter { brightness(50); volume(80) }
    exit { brightness(100); volume(50) }
    
    // Transition hooks
    on enter from "coding" { 
        notify("leaving code for games")
    }
    on exit to "default" {
        run("killall steam")
    }
}
```

**API:**
```havel
mode.current          // Current mode name
mode.previous         // Previous mode name
mode.time("gaming")   // Time spent in mode (seconds)
mode.transitions()    // Number of transitions
mode.set("gaming")    // Set mode explicitly
mode.list()           // List all modes
mode.signals()        // List all signals
mode.isSignal(name)   // Check if signal active
```

### 3. Window Query API

**Syntax:**
```havel
window.active              // Active window info (object)
window.any(exe == "steam") // Boolean
window.count(class == "discord") // Integer
window.filter(title ~ ".*YouTube.*") // Array
```

**Window Info Object:**
```havel
let win = window.active
win.title  // Window title
win.class  // Window class
win.exe    // Executable name
win.pid    // Process ID
```

**Files:**
- `WindowQuery.hpp/.cpp` - Window query implementation
- `WindowModule.hpp/.cpp` - Exposes `window.*` API

### 4. Concurrency Primitives

**Thread (Actor-based):**
```havel
let worker = thread {
  on message(msg) {
    print(msg)
  }
}
worker.send("hello")
worker.pause()
worker.resume()
worker.stop()
```

**Interval:**
```havel
let timer = interval 1000 {
  print("tick")
}
timer.pause()
timer.resume()
timer.stop()
```

**Timeout:**
```havel
timeout 5000 {
  print("5 seconds later")
}
```

**Range:**
```havel
if time.hour in (8..18) {
  print("Working hours")
}
```

### 5. MPV Controller Functions

**All implemented functions:**
```havel
mpv.volumeUp()
mpv.volumeDown()
mpv.toggleMute()
mpv.stop()
mpv.next()
mpv.previous()
mpv.addSpeed(delta)
mpv.addSubScale(delta)
mpv.addSubDelay(delta)
mpv.subSeek(index)
mpv.cycle(property)
mpv.copyCurrentSubtitle()
mpv.ipcSet(path)
mpv.ipcRestart()
mpv.pic  // Picture-in-picture status
```

## Test Scripts

### test_gaming.hv
Gaming mode module with exportable functions.

### test_import.hv
Tests import system and new APIs:
```havel
use "test_gaming.hv" as game

F1 => game.start()
F2 => game.stop()
F3 => game.toggle()
F4 => window queries
F5 => mode API
```

## Architecture

### Separation of Concerns

1. **Signals** - Facts about system (`window.any(...)`)
2. **Modes** - High-level state with priority
3. **Reactions** - `when` blocks that respond

### Import System Flow

```
Parser → UseStatement (filePath, alias)
   ↓
StatementEvaluator
   ↓
ImportManager.importScript(filePath, alias)
   ↓
Execute script in isolated environment
   ↓
Create module object with exports
   ↓
Define in current environment as alias
```

## Commits

```
f53253f test: add import system test scripts
87793f6 feat: implement script import system
42974b3 feat: implement comprehensive mode system
fc0812d feat: add Thread, Interval, Timeout, Range
69f84f5 feat: implement all MPV controller functions
... and 45 more
```

## TODO (Parser Integration)

The runtime supports these features, but parser integration is needed for:

1. ✅ `use "file.hv" as alias` - DONE
2. ⏳ `signal name = expression` syntax
3. ⏳ `mode name priority N { ... }` syntax  
4. ⏳ `on enter from`, `on exit to` hooks
5. ⏳ `window.any()`, `window.count()`, `window.filter()` expressions
6. ⏳ `export` keyword for module exports
7. ⏳ `config.gaming.classes` nested config access
8. ⏳ `group "name" { modes: [...] }` syntax

## Testing

Run test script:
```bash
./build-debug/havel test_import.hv
```

Expected behavior:
1. Import gaming module successfully
2. F1-F5 hotkeys work with imported functions
3. Window queries return active window info
4. Mode API returns current mode stats
