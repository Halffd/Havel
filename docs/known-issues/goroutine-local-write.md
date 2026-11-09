# A goroutine's write to an enclosing function-local is not visible to the parent

**Status:** FIXED (close-at-capture boxing, this session). A local captured by an inner closure lives in a born-closed heap cell that owns the value; the defining function's local accesses route through the same cell (loadLocalRouted/storeLocalRouted), and CLOSURE creates the cell via VM::boxLocal. A goroutine's write lands in the cell, which the parent reads on resume - no fiber snapshot involved. Regression lock: scripts/smoke/test_goroutine_local_write.hv (5 checks, both write directions + the object/global paths). Tiering note: functions whose locals were captured are refused tier-1 at runtime (functions_with_captured_locals_, stamped by boxLocal) because compiled code writes flat slots and would diverge from the boxes. See the Ownership lock section below for what landed.
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


## Converging defects in the same opcode (2026-10-22)

Three independent defects have been traced to the `CLOSURE` opcode and its
fallback in `VMControlFlow.cpp` (~1420), from three trees:

1. **Lambda frame size** (fixed, `88c7ebf5f`): a lambda's frame was sized
   from the compiler's local counter instead of the resolver's slot count.
2. **Boxed-upvalues gap** (this document, open): captured locals alias frame
   slots; fibers snapshot their locals, so a goroutine's write to an
   enclosing function-local is lost.
3. **CLOSURE fallback copies the ambient** (havel-2, 2634b6ec2): the
   fallback for closures whose parent chain lacks `module_globals` copies
   the ambient into a private snapshot. For Parser-factory closures that
   snapshot is taken while lazy module state is unbuilt, so compiled global
   reads/writes fork the lazy module build (multiple parallel BP tables; a
   consult racing a rebuild reads an empty table and the Pratt loop ends an
   expression one operator early - silent wrong ASTs). Main carries the
   same fallback (`VM.cpp` module-load sites create top-level functions
   with `module_globals = nullptr`, and `VMControlFlow.cpp`'s fallback
   copies the ambient), so this affects main's tiered self-hosted parsing
   too. havel-2's fix direction: the fallback must share the module's
   authoritative sidecar map instead of copying the ambient.

## Ownership lock

Per the "Shared hot paths" rule in AGENTS.md: changes to `doCall`,
`callFunction`, the `CLOSURE` opcode, or the fiber locals/upvalue
machinery must be claimed here before landing. Current claims:

- **goroutine-local-write-fix** (this session): implementing fix option 1
  as *close-at-capture boxing*. `CLOSURE`'s first capture of an absolute
  slot moves the slot's value into the cell and marks it closed (the cell
  becomes the authoritative storage); the defining scope's local accesses
  route through the existing `open_upvalues` map; `closeFrameUpvalues`'s
  idempotent close makes frame-exit a no-op for boxed cells;
  `closeOpenUpvaluesForSpawn` no longer snapshots (the cell is shared,
  not copied); the box map's closed values are GC roots; functions whose
  locals are captured are refused tier-1 (the JIT writes flat slots and
  would diverge from the cell). Files: `VMControlFlow.cpp`, `VMDispatch.cpp`,
  `VM.cpp`, `GC.cpp`, compiler flag stamping. Gates: smoke + jit-parity.

- havel-2-efa39c: the tiering fast-tick budget work
  (`FastTickDisarmGuard` in callFunction's host branch + the JIT bridges),
  landed on havel-2, pending merge to main.
- The boxed-upvalues fix (this document's open item): unstarted; owner
  will claim here before landing. It must NOT land while the tiering
  budget changes are unmerged - both touch the same call paths, and the
  when-block regression found while prototyping boxing (a failed condition
  callback wedged the VM) shows the interaction is real.

## Retrospective: an untested assertion that misdirected this work

During the investigation, this session asserted in a committed test file
that "a closure captures locals by value" - an untested assumption stated
as fact, which then framed the goroutine-write defect as a capture bug and
led to a proposed ticket for a closure-capture language bug. Measured
later: capture semantics are by-reference and correct (a closure reads the
current value of an enclosing local, writes are visible outside, two
closures share the variable). The actual defect was the lambda frame size
- unrelated to capture semantics. Lesson recorded: an untested mechanism
guess must be labelled as a guess at the time it is made, not quietly
reused; the probe that discredited the earlier "hotkey actions cannot
capture block-level locals" claim had the same shape (an invalid script
whose parse failure was misread as a capture failure). Both wrong leads
came from probing with invalid Havel and reading the resulting error as
the finding.
