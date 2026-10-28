# A discarded hotkey-binding pin leaked one GC external root per binding for the process lifetime

**Status:** FIXED twice over. (1) Shutdown drain: each hotkey registration
pins its hotkey-context object with `VM::pinExternalRoot`; the pin handle
was discarded, so roots stayed live until process exit. `trackExternalRoot`
hands the handle to the VM shutdown drain
(`src/havel-lang/compiler/vm/VM.cpp`, `tracked_external_roots_`).
(2) Mid-session release: `hotkey.remove()` now releases ALL registration
pins - the action callback, the condition callback, and the context-object
root (HotkeyModule::removeById releases via `vm.releaseCallback` and
`vm.unpinExternalRoot`; InputBridge stores the root handle per binding via
`HotkeyModule::setContextRootId`). Before (2), a register/remove cycle
leaked 3 roots per cycle for the process lifetime - measured 180 roots
after 60 cycles by `scripts/tests/test_hotkey_root_lifetime.hv` (now the
regression lock). `gc_external_root_count()` (new, read-only) exposes the
live pin count for this class of test.

## Symptom

Progressive scheduler slowdown in long-lived hotkey configs: every GC cycle
marks all pinned roots, so cost grows with total bindings ever registered
(318 for a large config), not with live ones. Observable pre-fix only via
the shutdown warning (`[VM][GC] N external roots still pinned at VM
shutdown`) or heap introspection.

## Mechanism

`src/host/module/bridges/InputBridge.cpp` (both `handleHotkeyRegister` and
`handleHotkeyRegisterConditional`): `pinExternalRoot` returns the only
handle that can release the root; the original code dropped it. The
comment at those sites documents this exactly - and that documentation
existed BEFORE the fix, as a post-mortem of the diagnosis that was never
converted into a tracked defect or test. That gap is what
`AGENTS.md` "Defect Discovery and Session Handoffs" now prohibits.

## Reproducer (manual-run; no input injection)

```havel
// scripts/tests/test_hotkey_root_lifetime.hv (committed)
_base = gc_external_root_count()
// register 60 conditional bindings, then:
for _n in _names { hotkey.remove(_n) }
gc_collect_full()
gc_collect()
if gc_external_root_count() > _base { print("FAIL: roots leaked"); exit(1) }
```

## Audit of the same pattern (Oct 26, havel-2 session)

Every `pinExternalRoot` call site was traced to a release path:

| Site | Release |
|---|---|
| InputBridge.cpp hotkey ctx (both register paths) | `trackExternalRoot` → shutdown drain AND `hotkey.remove()` → `setContextRootId`-stored handle released in `removeById` |
| VMCb.cpp:34 (`registerCallback`) | `releaseCallback` → `unpinExternalRoot` |
| VMCollections.cpp:29 | RAII `PinnedRootGuard` |
| VM.cpp:5531 (module closure wrappers) | `host_function_gc_roots_` → shutdown drain |
| VM.cpp:5794/5835/6912/7365 (transient copies/exports) | immediate `unpin` after use |
| JitRuntimeBridges.cpp:2283+ (array HOF bridges) | paired `unpin` |

Timer/interval pins (GC.hpp:498, ConcurrencyBridge.cpp:16, VM.cpp:149/214)
are released via sweep-freed hooks (`addIntervalFreedHook`/
`addTimeoutFreedHook`) when the sweep collects a dropped timer - those
comments describe FIXED machinery, not live hazards. C++-level root
regression: `host-root-lifetime` in `src/hvtest/smoke_runner.cpp` and
`src/havel-lang/tools/BytecodeSmokeMain.cpp`.

Remaining suspicious-comment triage from the codebase grep
(`this leaks|never released|not released|memory leak`): all other hits
are fixed-defect post-mortems ("used to leak", "otherwise leaks") or
leak-detector infrastructure. No additional live leak was found in the
pin/unpin surface this pass.
