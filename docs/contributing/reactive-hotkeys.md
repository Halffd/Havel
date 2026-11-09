---
title: "Reactive hotkeys and `when`: the operation order is the contract"
description: "Why the var-change handler, conditional-hotkey getters and goroutine-error boundary must keep their order - and which tests lock it."
---

# Reactive hotkeys and `when`: the operation order is the contract

The reactive hotkey system - `when` blocks, conditional hotkeys, and the
scheduler that re-evaluates them - looks like ordinary event plumbing and is
not. Three real defects came out of reordering or bypassing it in one week
(2026-10-30 .. 11-01), each one invisible to `hotkey.list()` snapshots until
a script read the state mid-transition. This page is the contract.

Read `AGENTS.md` ("Reactive hotkeys and `when`: the operation order is the
contract") first; this page has the detail behind those rules.

## Before you change anything here

This is a protected zone, held to the same bar as the GC and JIT/tiering
changes (which gate on `scripts/ci/jit-parity-test.sh` with pasted output).
The record justifies it: the pin leak, the when-teardown bug, the
suspended-fiber visibility questions and the goroutine-error abort all live
here, none were caught by the existing suite, and all surfaced only from
production behaviour. A change that compiles and passes the suite proves
almost nothing in this subsystem.

Every change here carries three things in its commit message:

1. **A named defect or gap, reproducible.** Not "cleanup", not
   "consistency", not "seemed off". If it closes a `docs/known-issues/`
   entry, link it.
2. **A before/after test.** The regression test must be shown failing on
   the pre-change binary and passing after, targeting the exact failure
   mode. "The existing suite still passes" is not a safety signal here.
3. **A blast-radius note.** State what the change does not touch. The
   failure mode in this subsystem is rarely "the change is wrong" - it is
   "locally correct, unstated side effect three layers away", because mode
   tracking, re-registration, pin lifetimes, interval lifecycles and fiber
   scheduling are chained.

Template for the commit message:

    Defect: <repro or docs/known-issues link>
    Before: <command; observed output on pre-change binary>
    After:  <command; observed output; test name that locks it>
    Blast radius: does not change <which of: pin/unpin lifetimes,
      interval lifecycles, fiber scheduling/GC visibility, mode
      transitions, hotkey re-registration, which transitions occur>
    Gates: <manual-run tests + smoke gate results>

### Deliberate non-effects of the 2026-10-30..11-01 fixes

For the record, so follow-up changes do not widen them:

* `332a8165e` (exit branches observe the ungrab): changed WHEN grab/enabled
  state is applied relative to when-branch bodies - the same transitions
  occur, no registration/pin/interval behaviour changes.
* `b72c5b7ba` (nil property reads throw catchably): affects reads only;
  `?.` unchanged; non-object scalar receivers keep returning nil.
* `410682e8d` (goroutine-error boundary): added the engine-boundary
  conversion only; error propagation elsewhere and scheduler behaviour are
  untouched.

## The order, and why

On every variable change, the engine handler
(`VM::setOnVarChangedSync` - `src/core/init/Havel.cpp` for the app,
`src/havel-lang/runtime/HavelEngine.hpp` for the launcher engine) runs, in
this order:

1. watcher condition evaluation (which `when` bodies fired);
2. **conditional-hotkey grab/enabled re-evaluation and application**;
3. the fired when-branch bodies;
4. `processSignalBindings`.

Steps 2 and 3 used to be swapped. A `when` exit branch then observed the
pre-transition state of a binding created in the sibling `when` body:
`enabled=true grab=true` on a hotkey that had just deactivated, with the
flip landing one tick later (reproduced: exit-branch read
`enabled=true grab=true`, +100ms read `enabled=false grab=false`). Fixed in
`332a8165e` by staging the fired fibers (`firedBodies`) and running them
after the re-eval - in both handlers.

The `grab` half of that state was already live despite the stale object
field, because reads go through the prototype getter `__get_grab`
(drain + live `ctx->grab` + field sync). `enabled` had no getter, so it
read the raw object field, which is only synced when the context has a host
object - every `saved = key => ...` binding inside a `when` body kept its
registration-time value. Fixed in the same commit by adding `__get_enabled`
as an exact mirror. Do not remove either getter, and do not "optimise" a
field read around them.

## The deferral rule (HavelEngine only)

`HavelEngine` runs scripts on the launcher path; its handler defers the
conditional re-eval to the scheduler tick
(`pending_var_changes_` / `drainPendingVarChanges`) while a goroutine
dispatch frame is active or `deepWrapModuleFunctions` is mid-frame.
Synchronous re-eval from inside those frames nests a recursive VM dispatch
(`callFunctionSync`) inside the active frame and wedges the host FFI - e.g.
`window.active()` never returns. The deferral means bodies in that path can
still see pre-drain state; that is known, documented at the site, and the
reason the field-read drain (`drainConditionalHotkeyPending`) exists.

## The goroutine-error boundary

An uncaught script error inside a `go` fiber is converted by the dispatch
into `ScriptError` - a plain struct, **not** derived from `std::exception`.
Without explicit handling it sails past `ScriptRunner`'s
`catch (const std::exception&)` and `HavelEngine::execute` (which had no
catch at all, unlike `Pipeline::execute`) into `std::terminate`: the
process died with SIGABRT (`rc=134`,
`go fn() { x = nil; y = x.foo }`). Fixed in `410682e8d`:
`HavelEngine::execute` wraps `processGoroutines()` and `tickGoroutines`
wraps `vm_->tickScheduler()` through `convertUncaughtScriptError`, which
reports via `ErrorReporter` + `notifyRuntimeError` and throws
`std::runtime_error` for the launcher's existing boundary. String values
resolve through the VM, so the message is the text, not `<string:629>`.

These catches are load-bearing. If you add a new engine entry point the
launcher calls, it needs the same conversion.

## The locking tests

| Test | Locks |
| --- | --- |
| `scripts/tests/test_when_exit_observes_ungrab.hv` | grab re-eval before bodies; `__get_enabled` liveness |
| `scripts/tests/test_when_block_hotkey_lifecycle.hv` | nil probe via try/catch, conditional registration, exit observation, re-arm on re-entry |
| `scripts/tests/test_when_assigned_hotkey_conditional.hv` | `saved = F10 => ...` inside a `when` compiles to a gated conditional (`11ab5dafc`) |
| `scripts/ci/goroutine-error-exit-test.sh` | uncaught goroutine error -> reported + exit 1, never SIGABRT |

They are manual-run by design (`hotkey.trigger` and hotkey registration are
on the smoke-forbidden list); run them plus the smoke gate before landing
anything in this area. The two smoke tests
`scripts/smoke/for_loop_destructure_*.hv` cover the unrelated
multi-iterator binding used by these tests.

## Related history

* `11ab5dafc` - when-body hotkey bindings are gated by the when condition
  (assigned bindings used to compile to plain `hotkey.register` and kept
  firing after the block deactivated).
* `332a8165e` - exit branches observe the ungrab; `enabled` reads are live.
* `b72c5b7ba` - nil property reads throw catchably (a `catch` guard on an
  unassigned binding previously never fired because `nil.field` quietly
  returned `nil`).
* `410682e8d` - uncaught goroutine errors are reported instead of aborting.
