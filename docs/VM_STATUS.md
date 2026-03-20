Havel Bytecode VM — Current State
Status: Phase 2/3 complete — fully functional stack VM with closures, GC, and host bridge

Last major commit: ff1d60b — object-model host bridge with callback rooting

Architecture
text
Havel Source
     ↓
LexicalResolver (scopes, upvalues)
     ↓
ByteCompiler
     ↓
BytecodeIR
     ↓
StackVM
     ↓
GC Heap (mark-sweep)
     ↓
HostBridge (object model)
     ↓
C++ Desktop APIs
Bytecode Instruction Set
Category	Opcodes
Stack	PUSH_CONST, POP, DUP, SWAP
Variables	LOAD_VAR, STORE_VAR, LOAD_GLOBAL, STORE_GLOBAL
Arithmetic	ADD, SUB, MUL, DIV, NEG
Comparison	LT, GT, EQ, NE
Control Flow	JUMP, JUMP_IF_FALSE
Functions	CALL, RETURN, CLOSURE
Upvalues	LOAD_UPVALUE, STORE_UPVALUE
Collections	ARRAY_NEW, ARRAY_GET, ARRAY_SET, ARRAY_PUSH
OBJECT_NEW, OBJECT_GET, OBJECT_SET
SET_NEW
Host	CALL_HOST
Value Types
cpp
using BytecodeValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string,
    ArrayRef,      // heap-allocated
    ObjectRef,     // heap-allocated
    SetRef,        // heap-allocated
    ClosureRef,    // heap-allocated
    HostObjectRef, // host object with methods
    HostFunctionRef // callable host method
>;
VM Components
Component	File	Lines	Status
VM	VM.cpp	~1500	✅ complete
GC Heap	GC.cpp	~400	✅ mark-sweep
ByteCompiler	ByteCompiler.cpp	~800	✅ complete
LexicalResolver	LexicalResolver.cpp	~600	✅ upvalues
HostBridge	HostBridge.cpp	~500	✅ object model
Pipeline	Pipeline.cpp	~300	✅ smoke tests
Host Bridge API
Window
havel
window.getActive()           → window object
window.moveToNextMonitor()   → void
window.moveToMonitor(id, idx)→ void
window.close(id)             → void
window.resize(id, w, h)      → void
window.on(event, callback)   → root_id
IO
havel
io.Send(keys)                → void
io.sendKey(key, press)       → void
io.mouseMove(x, y)           → void
io.mouseClick(button)        → void
io.getMousePosition()        → {x, y}
Hotkey
havel
hotkey.register(key, fn)     → root_id
hotkey.trigger(key)          → void (for testing)
Mode
havel
mode.define(name, condition, enter, exit) → void
mode.set(name)               → void
mode.tick()                  → void
System
havel
system.gc()                  → void (force collection)
system.gcStats()             → {heapSize, objectCount, collections, lastPauseNs}
Process
havel
process.find(name)           → array of PIDs
GC Implementation
Type: Mark-sweep

Root set:

Stack

Locals (current frame)

Globals

Active closures (in call frames)

External roots (pinned callbacks)

Collection trigger: After GC_ALLOCATION_BUDGET allocations (default 1024)

Stats:

cpp
struct GCStats {
    size_t heap_size;        // bytes allocated
    size_t object_count;     // live objects
    size_t collections;      // total collections
    uint64_t last_pause_ns;  // last GC pause time
};
Memory Leak Fix
Before: 164,685 bytes leaked in 1,457 allocations

After: All memory tracked by GC heap; external roots cleared on VM shutdown

Testing
Smoke tests: havel-bytecode-smoke

Passing cases:

function-call

first-class-call

nested-local-function

closure-return

closure-transitive

assignment-local

assignment-upvalue

while-loop

shadowing

object-member-assignment

index-assignment-array

set-create-and-assign

system-gc-manual

system-gc-stats

Next Steps (6 days)
Connect old modules — wrap IOModule.cpp, WindowModule.cpp, etc. in HostBridge

Callback execution — connect hotkey triggers to input system

REPL migration — switch from old interpreter to bytecode VM

Delete old interpreter — or keep for backward compatibility

Known Issues
Hotkey callbacks rooted but not yet invoked from actual input events

Mode callbacks same — stored but not triggered

REPL still uses old interpreter

Old modules (src/modules/) still use HavelValue not BytecodeValue

Files
Directory	Purpose
src/havel-lang/compiler/bytecode/	VM, compiler, GC, host bridge
src/havel-lang/tools/BytecodeSmokeMain.cpp	Smoke tests
src/modules/	Old modules (to be wrapped)
src/havel-lang/stdlib/	Pure language stdlib
