# A goroutine's write to an enclosing function-local is not visible to the parent

**Status:** open — needs boxed upvalues (compiler + VM) or per-fiber locals.
**Impact:** narrow. Reads work; writes to objects and module-level globals work.
Only a write to a *function-local* of the enclosing function is lost.

## Symptom

```havel
fn case() {
    w = 0
    go {
        w = 42          // the write is lost
    }
    sleep(300)
    return w            // 0, not 42
}
```

Working today (asserted in `scripts/smoke/test_closure_capture_constructs.hv`):

```havel
shared = {n: 0}
go { shared.n = 42 }     // visible to the parent
globalCounter = 0
go { globalCounter = 7 } // visible to the parent
```

## Mechanism

Fibers do not share the VM's operand stack or locals: `VM::saveFiberState`
copies the VM's `locals` vector into the fiber's own map
(`fiber->locals["_local_N"]`) and `VM::loadFiberState` restores the vector
from that map. A closure's captured local is an *open upvalue cell*
(`GCHeap::UpvalueCell` with `is_open`, `open_index`, `locals_base`) that
points into a **frame slot** of the defining function.

`VM::startGoroutineCall` clears the shared `locals` vector and builds the
goroutine's frame from base 0, so the parent frame's slots no longer exist
at the indices the open cell points at. The goroutine's write therefore
lands in the goroutine's own region, and the parent's restored snapshot
(the copy taken when the parent suspended) never sees it.

Closures that stay in the same fiber are unaffected: the cell and the
frame slot are the same storage, so reads and writes agree - which is why
single-closure probes look correct and hide this.

## Fix options

1. **Boxed upvalues (the standard fix).** A local captured by an inner
   scope lives in a heap cell; the defining function reads and writes
   *through* the cell, and every closure captures the same cell. Then a
   goroutine's write lands in the cell, which the parent reads on resume -
   no fiber-local snapshot is involved. This is a compiler change
   (the resolver must mark captured locals; the compiler must route their
   accesses through the cell) plus a VM change (cells become GC-visible
   heap objects) and touches every closure in the language. It also fixes
   the "parent writes, goroutine reads" direction that no write-back hack
   can cover.

2. **Per-fiber locals.** Stop snapshotting `locals` per fiber and give
   each fiber its own locals region, so a cell can point into a live
   frame across a switch. Larger VM change, and it interacts with the
   fiber-state work other trees are doing.

Option 1 is the smaller, more conventional change.

## Reproducer

`scripts/smoke/test_closure_capture_constructs.hv` covers the supported
paths; the function-local write case is intentionally not asserted (it
would fail). A minimal repro:

```havel
fn case() {
    w = 0
    go { w = 42 }
    sleep(300)
    print(w)   // 0
}
```

## Related

The lambda **frame-size** defect (a lambda's frame sized from the
compiler's counter instead of the resolver's slot count, so a local
declared before a loop was clobbered by the loop body) is fixed in
`88c7ebf5f` and covered by `scripts/smoke/test_closure_frame_locals.hv`.
That one was a compiler bug; this one is a fiber/upvalue-lifetime issue
and is a different fix.
