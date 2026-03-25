# Havel Implementation Status

Tracking implementation progress against docs/Havel.md specification.

## Legend
- ✅ Implemented
- ⚠️ Partially implemented
- ❌ Not implemented
- 📝 Documentation only

---

## Core Language Features

### Syntax & Grammar
| Feature | Status | Notes |
|---------|--------|-------|
| Hotkey mapping (`=>`) | ✅ | Basic hotkey syntax |
| Pipeline operator (`|`) | ⚠️ | Limited support |
| Block structure (`{}`) | ✅ | |
| Variables (`let`) | ✅ | |
| Constants (`const`) | ❌ | Defined in docs |
| Comments (`//`, `/* */`) | ✅ | |

### Control Flow
| Feature | Status | Notes |
|---------|--------|-------|
| `if/else` | ✅ | |
| `do/while` | ❌ | Defined in docs |
| `switch` | ❌ | Pattern matching |
| `when` blocks | ❌ | Conditional blocks |
| `repeat` | ✅ | Basic implementation |
| `for...in` | ⚠️ | Limited support |
| `try/catch/finally` | ❌ | Exception handling |

### Functions
| Feature | Status | Notes |
|---------|--------|-------|
| Function definition (`fn`) | ✅ | |
| Arrow functions | ✅ | |
| Default parameters | ❌ | |
| Variadic functions | ❌ | |
| Closures | ✅ | |
| Function overriding | ✅ | Can override built-ins |

---

## Type System (Gradual Typing)

| Feature | Status | Notes |
|---------|--------|-------|
| Type annotations | ❌ | |
| Struct definitions | ⚠️ | Syntax parsed, VM opcodes added |
| Enum definitions | ⚠️ | Syntax parsed, VM opcodes added |
| Sum types | ⚠️ | Via enum with payload |
| Type checking modes | ❌ | none/warn/strict |
| Trait system | ❌ | Interface-based polymorphism |
| `implements()` | ⚠️ | Placeholder (returns false) |
| `type.of()` | ✅ | Runtime type inspection |
| `type.is()` | ✅ | Type checking |
| Pattern matching (`match`) | ⚠️ | Basic equality matching |

---

## Standard Library Modules

### Core Modules
| Module | Status | Notes |
|--------|--------|-------|
| `io` | ✅ | send, keyDown, keyUp, suspend |
| `media` | ⚠️ | Basic play/pause |
| `fs` | ✅ | read, write, exists, size, delete |
| `clipboard` | ✅ | get, set, clear, send |
| `time` | ⚠️ | Basic time functions |
| `window` | ✅ | active, close, resize, move, focus, min, max |
| `mouse` | ✅ | click, move, scroll |
| `math` | ✅ | Full math library |
| `strings` | ✅ | String methods |
| `array` | ✅ | Array methods |
| `display` | ❌ | Monitor information |
| `process` | ✅ | find, exists, kill, nice, getpid, getppid, execute |
| `http` | ❌ | HTTP client |
| `browser` | ❌ | Browser automation (CDP) |
| `audio` | ❌ | Audio/volume control |
| `system` | ❌ | System integration |
| `pixel` | ❌ | Pixel operations |
| `image` | ❌ | Image search |
| `ocr` | ❌ | Optical character recognition |
| `screenshot` | ⚠️ | Basic screenshot |
| `debug` | ❌ | Debugging tools |
| `config` | ⚠️ | Configuration access |
| `mpv` | ❌ | MPV media controller |

### Extension System
| Feature | Status | Notes |
|---------|--------|-------|
| C ABI (HavelCAPI.h) | ✅ | Stable C interface |
| HavelValue type | ✅ | Opaque value handles |
| ExtensionLoader | ✅ | dlopen-based loading |
| `extension.load()` | ✅ | Load extensions |
| `extension.isLoaded()` | ✅ | Check loaded status |
| `extension.list()` | ✅ | List extensions |
| `extension.unload()` | ❌ | Intentionally removed |
| Host service API | ❌ | get_host_service() |

---

## Hotkey System

### Basic Hotkeys
| Feature | Status | Notes |
|---------|--------|-------|
| Simple mapping | ✅ | `F1 => { ... }` |
| Modifier keys | ✅ | `^!T`, `#f1` |
| Mouse buttons | ✅ | `LButton`, `RButton` |
| Combo hotkeys | ⚠️ | Left/right modifier distinction |
| `when` conditions | ❌ | Grouped conditions |
| Nested `when` | ❌ | |

### Conditional Hotkeys
| Feature | Status | Notes |
|---------|--------|-------|
| Mode conditions | ⚠️ | Basic mode support |
| Window title | ⚠️ | Via window.active |
| Window class | ⚠️ | Via window.active |
| Process conditions | ❌ | |
| Window groups | ❌ | |
| Signal conditions | ❌ | |
| Combined conditions | ❌ | AND/OR logic |

### Advanced Hotkeys
| Feature | Status | Notes |
|---------|--------|-------|
| `this` context | ❌ | Self-management |
| `on tap()` | ❌ | Tap detection |
| `on combo()` | ❌ | Combo detection |
| `on keyDown` | ❌ | Raw key events |
| `on keyUp` | ❌ | Raw key events |
| Self-disable | ❌ | this.disable() |
| Self-remove | ❌ | this.remove() |

---

## Mode System

| Feature | Status | Notes |
|---------|--------|-------|
| Mode definitions | ⚠️ | Basic mode support |
| Mode priority | ❌ | Override system |
| `enter` hooks | ❌ | |
| `exit` hooks | ❌ | |
| `on enter from` | ❌ | Transition hooks |
| `on exit to` | ❌ | Transition hooks |
| Mode statistics | ❌ | time(), transitions() |
| Mode groups | ❌ | Batch operations |
| Temporary overrides | ❌ | High-priority modes |
| `mode.set()` | ❌ | Programmatic control |
| `mode.list()` | ❌ | List all modes |
| `mode.signals()` | ❌ | List signals |

---

## Window System

| Feature | Status | Notes |
|---------|--------|-------|
| `window.active` | ✅ | Get active window |
| `window.list()` | ❌ | List all windows |
| `window.focus()` | ❌ | Focus window |
| `window.min()` | ❌ | Minimize |
| `window.max()` | ❌ | Maximize |
| `window.any()` | ❌ | Conditional check |
| `window.count()` | ❌ | Count matches |
| `window.filter()` | ❌ | Filter windows |
| `window.find()` | ❌ | Find first match |
| Window groups | ❌ | Group management |
| `window.groupGet()` | ❌ | Get windows in group |
| `window.inGroup()` | ❌ | Check membership |
| `window.findInGroup()` | ❌ | Find in group |

### Special Identifiers
| Identifier | Status | Notes |
|------------|--------|-------|
| `title` | ✅ | Window title |
| `class` | ✅ | Window class |
| `exe` | ✅ | Executable name |
| `pid` | ❌ | Process ID |

---

## Concurrency

| Feature | Status | Notes |
|---------|--------|-------|
| `thread` | ❌ | Actor-based concurrency |
| `interval` | ❌ | Repeating timer |
| `timeout` | ❌ | One-shot timer |
| Message passing | ❌ | thread.send() |
| `pause/resume/stop` | ❌ | Thread control |

---

## Script Lifecycle

| Feature | Status | Notes |
|---------|--------|-------|
| `on start` | ❌ | Initialize once |
| `on reload` | ❌ | Reload hook |
| `runOnce()` | ❌ | Run command once |
| Auto-reload | ❌ | File watching |
| `app.enableReload()` | ❌ | Control reload |

---

## Input System

### Global Functions
| Function | Status | Notes |
|----------|--------|-------|
| `print()` | ✅ | |
| `sleep()` | ✅ | Duration strings |
| `sleepUntil()` | ❌ | Sleep until time |
| `send()` | ✅ | Send keystrokes |
| `click()` | ✅ | Mouse click |
| `dblclick()` | ❌ | Double click |
| `move()` | ✅ | Mouse move |
| `moveRel()` | ❌ | Relative move |
| `scroll()` | ✅ | Mouse scroll |
| `play()` | ❌ | Media control |
| `exit()` | ❌ | Exit application |
| `read()` | ❌ | File read |
| `write()` | ❌ | File write |

### Input Shortcuts
| Feature | Status | Notes |
|---------|--------|-------|
| Implicit input | ❌ | `> "text"` syntax |
| Key symbols | ❌ | `{Enter}`, `{Esc}` |
| Mouse symbols | ❌ | `lmb`, `rmb` |
| Timing (`:500`) | ❌ | Inline sleep |

---

## String Operations

| Feature | Status | Notes |
|---------|--------|-------|
| `.upper()` | ✅ | |
| `.lower()` | ✅ | |
| `.trim()` | ✅ | |
| `.replace()` | ✅ | |
| String repetition (`*`) | ❌ | `"abc" * 3` |
| Interpolation | ❌ | `"Hello ${name}"` |

---

## Array Operations

| Feature | Status | Notes |
|---------|--------|-------|
| `.sort()` | ✅ | With comparator |
| `.sorted()` | ✅ | Non-mutating |
| `.sortByKey()` | ✅ | Object sorting |
| `.map()` | ❌ | |
| `.filter()` | ❌ | |
| `.reduce()` | ❌ | |
| `.find()` | ❌ | |
| `.some()` | ❌ | |
| `.every()` | ❌ | |
| `.includes()` | ❌ | |
| `.indexOf()` | ❌ | |
| `.push/pop()` | ❌ | |
| `.insert()` | ❌ | |
| `.removeAt()` | ❌ | |
| `.slice()` | ❌ | |
| `.concat()` | ❌ | |
| `.swap()` | ❌ | |

---

## Object/Struct Operations

| Feature | Status | Notes |
|---------|--------|-------|
| Object literals | ✅ | `{a: 1, b: 2}` |
| Dot notation | ✅ | `obj.prop` |
| Spread operator | ❌ | `...obj`, `...arr` |
| Struct methods | ❌ | |
| `Type()` constructor | ❌ | `MousePos(10, 20)` |
| `implements()` | ⚠️ | Placeholder |
| `newStruct()` | ❌ | Removed - use VM opcodes |
| `getField()` | ❌ | Removed - use VM opcodes |
| `newEnum()` | ❌ | Removed - use VM opcodes |
| `getVariant()` | ❌ | Removed - use VM opcodes |
| `getPayload()` | ❌ | Removed - use VM opcodes |

---

## Type Conversions

| Function | Status | Notes |
|----------|--------|-------|
| `int()` | ✅ | |
| `num()` | ✅ | |
| `str()` | ✅ | |
| `list()` | ❌ | Array constructor |
| `tuple()` | ❌ | Fixed-size list |
| `set()` | ❌ | Unique elements |

---

## Math Functions

| Function | Status | Notes |
|----------|--------|-------|
| `abs/ceil/floor/round` | ✅ | |
| `sin/cos/tan` | ✅ | |
| `asin/acos/atan` | ✅ | |
| `exp/log/log10/log2` | ✅ | |
| `sqrt/cbrt/pow` | ✅ | |
| `min/max/clamp` | ✅ | |
| `lerp()` | ❌ | Linear interpolation |
| `random()/randint()` | ❌ | Random numbers |
| `deg2rad/rad2deg` | ❌ | Angle conversion |
| `sign/fract/mod` | ❌ | Utility functions |
| `distance/hypot` | ❌ | Geometry |
| Constants (PI, E, TAU) | ✅ | |

---

## Shell Commands

| Feature | Status | Notes |
|---------|--------|-------|
| `$ command` | ✅ | Fire-and-forget |
| `` `command` `` | ❌ | Capture output |
| Pipe chains | ❌ | `cmd1 | cmd2` |
| ShellResult object | ❌ | stdout, stderr, exitCode |

---

## Regex Operations

| Feature | Status | Notes |
|---------|--------|-------|
| `/pattern/` syntax | ❌ | Literal regex |
| `~` operator | ❌ | Match operator |
| `matches` keyword | ❌ | Explicit match |
| Regex in conditions | ❌ | `title ~ /YouTube/` |

---

## Boolean Operators

| Operator | Status | Notes |
|----------|--------|-------|
| `and` | ❌ | |
| `or` | ❌ | |
| `not` | ❌ | |
| `in` | ❌ | Membership |
| `not in` | ❌ | Negative membership |

---

## Configuration System

| Feature | Status | Notes |
|---------|--------|-------|
| Config blocks | ⚠️ | Basic support |
| Nested blocks | ❌ | Hierarchical keys |
| `config.get()` | ⚠️ | Via C++ ConfigManager |
| `config.set()` | ❌ | |
| `config.save()` | ❌ | |
| Debounce | ❌ | Batch saves |
| `config.debounce()` | ❌ | |
| `config.begin()/end()` | ❌ | Batch mode |
| Process priority | ❌ | Config option |
| Worker threads | ❌ | Config option |

---

## MPV Controller

| Function | Status | Notes |
|----------|--------|-------|
| `volumeUp/Down()` | ❌ | |
| `toggleMute()` | ❌ | |
| `stop/next/previous()` | ❌ | |
| `addSpeed()` | ❌ | |
| `addSubScale()` | ❌ | |
| `addSubDelay()` | ❌ | |
| `subSeek()` | ❌ | |
| `cycle()` | ❌ | |
| `copyCurrentSubtitle()` | ❌ | |
| `ipcSet()` | ❌ | |
| `ipcRestart()` | ❌ | |
| `screenshot()` | ❌ | |

---

## Pixel & Image Automation

| Feature | Status | Notes |
|---------|--------|-------|
| `pixel.get()` | ❌ | Get pixel color |
| `pixel.match()` | ❌ | Color matching |
| `pixel.wait()` | ❌ | Wait for color |
| `pixel.region()` | ❌ | Create region |
| `image.find()` | ❌ | Image search |
| `image.wait()` | ❌ | Wait for image |
| `image.exists()` | ❌ | Check existence |
| `image.count()` | ❌ | Count occurrences |
| `image.findAll()` | ❌ | Find all matches |
| Screenshot cache | ❌ | Performance optimization |

---

## Browser Automation (CDP/Marionette)

| Feature | Status | Notes |
|---------|--------|-------|
| `browser.connect()` | ❌ | Chrome CDP |
| `browser.connectFirefox()` | ❌ | Firefox Marionette |
| `browser.open/goto()` | ❌ | Navigation |
| `browser.click/type()` | ❌ | Element interaction |
| `browser.setZoom()` | ❌ | Zoom control |
| `browser.eval()` | ❌ | JavaScript execution |
| `browser.screenshot()` | ❌ | Page screenshot |
| Tab management | ❌ | listTabs, activate, close |
| Window management | ❌ | setSize, maximize |
| Extension management | ❌ | Chrome only |
| Browser discovery | ❌ | getOpenBrowsers |

---

## Language Server (LSP)

| Feature | Status | Notes |
|---------|--------|-------|
| LSP server | ❌ | havel-lsp binary |
| VSCode extension | ❌ | |
| Syntax highlighting | ❌ | |
| Diagnostics | ❌ | |
| Hover info | ❌ | |
| Go to definition | ❌ | |
| Document symbols | ❌ | |

---

## CLI

| Command | Status | Notes |
|---------|--------|-------|
| `havel script.hv` | ✅ | Full mode |
| `havel --run` | ✅ | Pure mode |
| `havel --repl` | ✅ | REPL mode |
| `havel --full-repl` | ✅ | Full-featured REPL |
| `havel --help` | ✅ | |
| `havel-lsp` | ❌ | Language server |

---

## Helper Functions

| Function | Status | Notes |
|----------|--------|-------|
| `firstExisting()` | ❌ | File fallback |
| `approx()` | ❌ | Float comparison |
| `config.list()` | ❌ | Parse comma-separated |
| `implements()` | ❌ | Trait check |
| `debug.showAST()` | ❌ | Debug tools |

---

## Summary

### Implementation Progress

| Category | Implemented | Partial | Missing | Total | % Complete |
|----------|-------------|---------|---------|-------|------------|
| Core Language | 8 | 2 | 6 | 16 | 50% |
| Type System | 2 | 4 | 4 | 10 | 20% |
| Standard Library | 8 | 3 | 10 | 21 | 38% |
| Hotkey System | 4 | 1 | 9 | 14 | 29% |
| Mode System | 1 | 1 | 10 | 12 | 8% |
| Window System | 7 | 2 | 4 | 13 | 54% |
| Concurrency | 0 | 0 | 5 | 5 | 0% |
| Script Lifecycle | 0 | 0 | 5 | 5 | 0% |
| Input System | 5 | 0 | 8 | 13 | 38% |
| String/Array/Object | 8 | 1 | 14 | 23 | 35% |
| Math | 13 | 0 | 7 | 20 | 65% |
| Configuration | 1 | 1 | 6 | 8 | 13% |
| Advanced Features | 0 | 0 | 35+ | 35+ | 0% |

### Priority TODO

1. **High Priority** (Core functionality)
   - [x] Window management module (`window.*`)
   - [x] Process module (`process.*`)
   - [x] File system module (`fs.*`)
   - [x] HTTP client module (`http.*`)
   - [ ] Browser automation (`browser.*`)
   - [ ] Struct field syntax (`obj.field` → index access)
   - [ ] Enum pattern matching (`match e { Ok(x) => ... }`)

2. **Medium Priority** (Quality of life)
   - [x] Iteration protocol (for-in loops)
   - [x] Match expression
   - [ ] Type system implementation (compile-time tracking)
   - [ ] Concurrency primitives (`spawn {}`)
   - [ ] Script lifecycle hooks
   - [ ] Configuration system improvements

3. **Low Priority** (Advanced features)
   - [ ] Pixel/image automation
   - [ ] MPV controller
   - [ ] LSP server
   - [ ] Advanced regex features

---

Last updated: 2026-03-25

## Latest Changes

### Struct/Enum VM Opcodes (2026-03-25) - ARCHITECTURAL FIX
**Moved struct/enum from HostBridge hacks to proper VM opcodes:**

**BEFORE** (wrong layer - runtime hacks):
- `newStruct()`, `getField()` - string-based field access
- `newEnum()`, `getVariant()`, `getPayload()` - `__variant` string tags
- Used `unordered_map<string, Value>` for storage

**AFTER** (correct layer - VM + Compiler):
- `STRUCT_NEW`, `STRUCT_GET`, `STRUCT_SET` opcodes
- `ENUM_NEW`, `ENUM_TAG`, `ENUM_PAYLOAD`, `ENUM_MATCH` opcodes
- Compact storage: `vector<Value>` with field indices
- Type registry in GCHeap for struct/enum definitions

**Benefits:**
- Fast field access by index (not string hash lookup)
- Compiler can validate field access
- Proper type safety foundation
- Pattern matching ready (`ENUM_MATCH` opcode)

### Match Expression (2026-03-25)
- `match value { pattern => expr, _ => default }` syntax
- Compiles to equality checks with jumps
- Supports default case (`_ =>`)
- Ready for enum pattern matching (via `ENUM_MATCH`)

### Iteration Protocol (2026-03-25) - ARCHITECTURAL FIX
**Proper iterator protocol instead of special-casing:**
- `ITER_NEW` opcode: creates iterator from any iterable
- `ITER_NEXT` opcode: returns `{value, done}` object
- Unified `GCHeap::Iterator` struct
- Works with: **Array**, **String**, **Object** (extensible to File, Generator, etc.)

```havel
for x in [1, 2, 3] { ... }      // Array iteration
for c in "hello" { ... }         // String (char by char)
for key in {a: 1, b: 2} { ... }  // Object (key by key)
```

**Desugaring:**
```
for x in obj { body }

Becomes:
  let __iter = iter(obj)
  while (true) {
    let __result = __iter.next()
    if (__result.done) break
    let x = __result.value
    body
  }
```

### For-In Loops & Dot Notation (2026-03-25)
- **For-in loops**: `for element in array { ... }`
  - LexicalResolver support for iterator scope
  - Bytecode compiler with proper slot allocation
  - ARRAY_LEN opcode for iteration
- **Module names lowercase**: `use io`, `use string` (not IO, String)
- **Dot notation**: `str.trim()` calls `string.trim` via `any.*` dispatch
- **String interpolation**: `$var` syntax (lexer converts to `${var}`)

### String Interpolation & Dot Notation (2026-03-24)
- Added `${expr}` interpolation syntax
- Added dot notation: `str.trim()`, `arr.len()`
- Added `any.*` runtime dispatch for type-based method calls
- Fixed E constant (now Euler's number 2.718...)
- Added E_CHARGE for elementary charge

**Known Issues:**
- Interpolation variable scope resolution may have issues (mini-parser scope)
- `any.*` dispatch needs string/array functions in HostBridge options_
- Arrays print as `array[N]` not values (need toString/__repr__)
- Object iteration order is undefined (hash map iteration)
