# Runtime Architecture

Overview of the Havel runtime internals: compiler pipeline, virtual machine, scheduler, and how they interact.

---

## Compiler Pipeline

```
Source (.hv)
    |
    v
Lexer (tokenizer) -> Token stream
    |
    v
Parser (precedence climbing) -> AST
    |
    v
Semantic Analyzer (type check, symbol resolution, imports) -> Typed AST
    |
    v
Bytecode Compiler (IR generation, optimization) -> Bytecode
    |
    v
VM / JIT (execution)
```

### Lexer

- Tokenizes source into tokens
- Handles string interpolation (`{var}` inside strings)
- Modifier symbols: `^` (Ctrl), `+` (Shift), `!` (Alt), `#` (Super)

### Parser

- Precedence climbing for expressions
- Produces AST nodes
- Handles hotkey syntax: `F1 => { ... }`, `^+A => send("x")`
- `when` blocks compile to conditional hotkey registrations

### Semantic Analyzer

- Type checking
- Symbol table management
- Module resolution (`use` statements)
- Import validation

### Bytecode Compiler

- Generates bytecode IR from typed AST
- Optimization passes (constant folding, dead code elimination)
- Emits final bytecode for VM execution

---

## Virtual Machine

### Stack-Based VM

- Operand stack for computations
- Call frames for function invocations
- Local variables per frame

### Value Representation

| Type | Tag | Description |
|------|-----|-------------|
| `int` | INT_TAG | 64-bit integer |
| `num` | FLOAT_TAG | 64-bit float |
| `bool` | BOOL_TAG | true/false |
| `nil` | NIL_TAG | null/none |
| `string` | STRING_ID / STRING_VAL_ID | Heap-allocated string |
| `array` | ARRAY_ID | Heap-allocated array |
| `object` | OBJECT_ID | Heap-allocated object |
| `fn` | CLOSURE_ID / FUNCTION_OBJ_ID | Function/closure |
| `thread` | THREAD_ID | Goroutine reference |
| `channel` | CHANNEL_ID | Channel reference |
| `coroutine` | COROUTINE_ID | Coroutine reference |

### String Handling

- Strings are heap-allocated via `VM::getHeap().allocateString(str)`
- Returns `StringRef { id }` used with `Value::makeStringId(id)`
- Reverse lookup: `VM::resolveStringKey(value)` gets string from any string Value
- Two string tags: `STRING_ID` (heap STRING_ID), `STRING_VAL_ID` (value-string)

---

## Scheduler / Goroutines

The scheduler manages concurrent execution using cooperative goroutines (fibers).

### Goroutine Lifecycle

```
Created -> Runnable -> Running -> Suspended -> Runnable -> ...
                              \-> Finished
```

### Suspension Reasons

| Reason | Description |
|--------|-------------|
| `HotkeyWait` | Waiting for hotkey trigger |
| `Sleep` | Sleeping for duration |
| `ThreadJoin` | Waiting for thread to finish |
| `ChannelRecv` | Waiting to receive from channel |
| `ChannelSend` | Waiting to send to channel |
| `TimerWait` | Waiting for timer |
| `Coroutine` | Yielded coroutine |
| `External` | External wait |

### Hotkey Goroutine Flow

1. `hotkey.register(key, action)` spawns a persistent goroutine
2. The goroutine executes the action, then parks as `Suspended(HotkeyWait)`
3. When the OS key event arrives, `wakeHotkey()` wakes the goroutine
4. The goroutine resumes, executes the action again, re-parks

### HotkeyPolicy

When a hotkey fires while its goroutine is already running:

| Policy | Behavior |
|--------|----------|
| `Drop` (0) | Discard the new trigger |
| `Replace` (1) | Kill current goroutine, re-queue it fresh |
| `Queue` (2) | Queue the trigger, run after current finishes |
| `Coalesce` (3) | Merge with current trigger, update args |

### Lock Ordering

The scheduler uses two mutexes that must be acquired in order:

1. `priority_mutex_` first
2. `goroutines_mutex_` second

Or: release `goroutines_mutex_` before calling anything that acquires `priority_mutex_`.

---

## Event System

### EventQueue

Thread-safe queue for cross-thread communication. OS threads (X11, timers, mode changes) push events, the main loop processes them.

### Event Types

| Type | Description |
|------|-------------|
| `VAR_CHANGED` | A watched variable changed |
| `TIMER_FIRED` | A timer expired |
| `HOTKEY_FIRED` | A hotkey was pressed |
| `MODE_CHANGED` | The active mode changed |

### Event-Driven Updates

```
[OS thread] -> EventQueue::push(event)
                   |
                   v
              Main loop processes event
                   |
                   v
              VAR_CHANGED handler:
                   |
                   +-> ModeManager::update()
                   +-> ConditionalHotkeyManager::ScheduleReevaluation()
```

---

## Conditional Hotkey Manager

### Architecture

- **No polling** — event-driven via VAR_CHANGED
- `ScheduleReevaluation()` batches updates to avoid thrashing
- Each `ConditionalHotkey` tracks its own condition, grab state, and dependencies
- When condition changes from false to true: `GrabHotkey()`
- When condition changes from true to false: `UngrabHotkey()`

### ConditionalHotkey Struct

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Unique identifier |
| `key` | string | Key combo |
| `condition` | variant<string, function<bool()>> | Condition expression or callback |
| `trueAction` | fn | Action when condition is true |
| `falseAction` | fn | Optional action when condition becomes false |
| `currentlyGrabbed` | bool | Whether the key is currently grabbed |
| `lastConditionResult` | bool | Last evaluation result |
| `monitoringEnabled` | bool | Whether monitoring is active |
| `async` | bool | Async evaluation |
| `dependencies` | set<string> | Variable names this condition depends on |

### Condition Evaluation

1. `evaluateCondition(id)` — evaluate a single conditional hotkey
2. `ScheduleReevaluation()` — schedule batch update via EventQueue
3. `BatchUpdateConditionalHotkeys()` — re-evaluate all, grab/ungrab as needed
4. `SetMode(mode)` — update current mode and trigger re-evaluation

---

## Mode Manager

### ModeDefinition

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Mode name |
| `priority` | int | Higher = evaluated first |
| `isActive` | bool | Currently active |
| `conditionCallback` | function<bool()> | Condition function |
| `onEnter` | function<void()> | Enter callback |
| `onExit` | function<void()> | Exit callback |
| `onEnterFrom` | function<void(string)> | Enter from specific mode |
| `onExitTo` | function<void(string)> | Exit to specific mode |
| `enterTime` | time_point | When mode was entered |
| `totalTime` | milliseconds | Total time spent in mode |
| `transitionCount` | int | Number of times entered |

### Mode Evaluation

1. All modes sorted by priority (descending)
2. Evaluate each condition in order
3. First true condition activates its mode
4. If no condition is true, fall back to `"default"`
5. Transition callbacks fire on mode change

### VAR_CHANGED Integration

ModeManager registers a handler for VAR_CHANGED events. When a variable changes:

1. All signal conditions are re-evaluated
2. All mode conditions are re-evaluated
3. If the active mode changes, enter/exit callbacks fire
4. `HotkeyManager::setMode()` is called, triggering conditional hotkey re-evaluation

---

## Host Module System

### Bridge Architecture

```
HostContext (holds pointers to core services)
    |
    v
BridgeModule subclasses (InputBridge, ModeBridge, WindowBridge, ...)
    |
    v
install() registers host_functions into PipelineOptions
    |
    v
HostBridge collects all functions, makes them available to VM
```

### HostContext Pointers

| Pointer | Service |
|---------|---------|
| `io` | IO (keyboard/mouse) |
| `windowManager` | Window manipulation |
| `hotkeyManager` | Hotkey registration/grab |
| `modeManager` | Mode management |
| `brightnessManager` | Brightness control |
| `audioManager` | Audio control |
| `guiManager` | GUI operations |
| `screenshotManager` | Screenshots |
| `clipboardManager` | Clipboard |
| `pixelAutomation` | Pixel-based automation |
| `automationManager` | Automation tasks |
| `fileManager` | File operations |
| `processManager` | Process management |
| `networkManager` | Network |
| `windowMonitor` | Window monitoring for dynamic vars |
| `mpvController` | MPV media player |
| `eventQueue` | Thread-safe event dispatch |

### Function Registration

Host functions are `BytecodeHostFunction`: `std::function<Value(const std::vector<Value>&)>`.

Registered in `install()`:

```cpp
options.host_functions["hotkey.register"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRegister(args, ctx);
};
```

### Prototype Methods

Registered via `api.registerPrototypeMethod()`:

```cpp
api.registerPrototypeMethod("Hotkey", "enable", 1, [&vm](const auto &args) -> Value {
    // ...
});
```

This template:
1. Calls `vm.registerHostFunction(fullName, arity, func)` to register the function
2. Calls `vm.registerPrototypeMethodByName(typeName, methodName, fullName)` to map the method

---

## Build System

### Build Modes

| Mode | Type | Tests | Havel Lang | LLVM | Build Dir |
|------|------|-------|------------|------|-----------|
| 0 | Debug | yes | yes | yes | build-debug |
| 5 | Release | yes | yes | yes | build-release |
| 6 | Debug | yes | yes | no | build-debug |
| 9 | Release | yes | yes | no | build-release |

### Commands

```bash
./build.sh 5 build    # Full release with LLVM
./build.sh 6 build    # Fast debug (no LLVM)
./build.sh test       # Run all tests
./build.sh detect     # Show system dependencies
```

### Executables

| Binary | Purpose |
|--------|---------|
| `build-debug/havel` | Main application |
| `build-debug/havel-lsp` | Language Server Protocol |
| `build-debug/havel-bytecode-smoke` | Bytecode smoke test |

### Testing

```bash
./build-debug/hvtest --smoke       # Bytecode smoke tests
./build-debug/hvtest --scripts     # Script tests
./build-debug/hvtest --scheduler   # Scheduler rig tests (124 tests)
```

---

## Key Constraints

- **No `Scheduler.hpp` in `VM.hpp`** — X11 `#define None 0L` conflicts with `SuspensionReason::None`; use forward declarations
- **Lock ordering** — acquire `priority_mutex_` before `goroutines_mutex_`
- **GC roots** — pin hotkey callback closures in `handleHotkeyRegister` (has VM access), not in Scheduler
- **`hotkey_global_names`** — must never be reintroduced; caused desync bugs; host functions use runtime dispatch via `CALL` + `HostFunctionRef`
- **`HOTKEY_MAX_INSTRUCTIONS`** — raised to 100,000 (1,000 was too low)
- **ffi.call** — known SEGV bug in libffi (`cif->flags` corruption); all OpenCV FFI functions removed until fixed
