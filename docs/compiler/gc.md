---
title: "Garbage Collection"
description: "Generational GC: heap allocation, roots, finalizers, and thread safety."
---

# Garbage Collection

## Architecture

- **Generational GC**: Young and old generations
- **Object allocation**: Via `ObjectEntry` in hash map indexed by `ObjectId`
- **Thread-safe**: All heap accessors use mutex locking

**Source**: `src/havel-lang/compiler/gc/GC.hpp`, `GC.cpp`

---

## Heap Allocation

| Method | Returns | Description |
|--------|---------|-------------|
| `allocateObject()` | `ObjectRef` | Empty object |
| `allocateArray()` | `ArrayRef` | Dynamic array |
| `allocateString(str)` | `StringRef` | Allocate/intern string |
| `allocateSet()` | `SetRef` | Unique-element set |
| `allocateTuple()` | `TupleRef` | Fixed-size heterogeneous |

```cpp
auto obj = vm.getHeap().allocateObject();
auto arr = vm.getHeap().allocateArray();
auto str = vm.getHeap().allocateString("hello");
```

---

## ObjectEntry

```cpp
struct ObjectEntry {
    ObjectId id;
    std::atomic<uint64_t> shape_version;  // for inline caches
    ObjectType type;
    // ... type-specific data
};
```

- Move semantics supported
- Copy constructor/assignment deleted (atomic is non-copyable)
- `shape_version` enables fast prototype invalidation

---

## Thread Safety

| Resource | Mechanism |
|----------|-----------|
| Heap accessors | `GC::mutex_` (mutable, allows const methods to lock) |
| `subHeapBytes` | CAS (compare-and-swap) |
| `next_enum_id_` | CAS |
| String interning | Mutex-protected |

```cpp
// All heap accessors lock mutex
Value GC::getObjectField(ObjectId id, const std::string& key) {
    std::lock_guard lock(mutex_);
    // ...
}
```

---

## GC Roots

The VM registers roots before collection:

1. **Operand stack** values
2. **Local variables** in all active call frames
3. **Global variables**
4. **Upvalues** in all closures
5. **Pinned hotkey callback closures** (pinned in `handleHotkeyRegister`)

```cpp
// In VM::collectGarbage()
void VM::collectGarbage() {
    std::vector<Value> roots;
    // 1. Stack
    for (auto& val : stack_) roots.push_back(val);
    // 2. Locals (all frames)
    for (auto& frame : call_frames_) {
        for (auto& local : frame.locals) roots.push_back(local);
    }
    // 3. Globals
    for (auto& [_, val] : globals_) roots.push_back(val);
    // 4. Upvalues
    for (auto& closure : closures_) {
        for (auto& up : closure.upvalues) roots.push_back(up);
    }
    // 5. Pinned hotkey closures
    for (auto& hk : pinned_hotkey_closures_) roots.push_back(hk);
    
    gc_.collect(roots);
}
```

---

## Collection Phases

```
Mark Phase:
  1. Start from roots
  2. Traverse object graph
  3. Mark reachable objects

Sweep Phase:
  1. Iterate all objects
  2. Free unmarked
  3. Update free lists
```

- No stop-the-world (mark-and-sweep runs single-threaded with VM)
- Incremental GC available via `--gc-incremental`

---

## Finalizers

Objects can have finalizers registered:

```cpp
// Register finalizer for object
vm.getHeap().setFinalizer(obj_id, [](ObjectId id) {
    // Cleanup: close file handles, free native resources
});
```

Finalizers run during sweep phase for collected objects.

---

## Configuration

```bash
--heap-max <bytes>           # Max heap size
--gc-budget <n>              # GC work per tick
--gc-incremental             # Enable incremental GC
--gc-stop-the-world          # Force stop-the-world
--gc-full-interval <n>       # Full GC interval
--gc-promotion-age <n>       # Generational promotion age
```

---

## GC Debugging

```bash
havel -dgc script.hv
```

Output:
```
[GC] Allocated 1024 bytes, total 50KB
[GC] Mark phase: 150 objects, 0.5ms
[GC] Sweep phase: 12 freed, 0.2ms
[GC] Heap: 48KB used, 12KB free
```

---

## Memory Safety

| Issue | Prevention |
|-------|------------|
| Use-after-free | GC roots keep objects alive |
| Double-free | Single ownership via ObjectId |
| Memory leaks | Generational collection + finalizers |
| Data races | Mutex-protected heap access |
| Finalizer ordering | Topological sort by dependencies |

---

**Previous:** [JIT/AOT Compilation](/compiler/jit-aot)
**Next:** [Self-Hosted vs Bootstrap →](/compiler/self-hosted)