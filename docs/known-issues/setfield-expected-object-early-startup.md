# `VMApi::setField: expected object` kills hotkeys0.3.hv during early startup

**Status:** FIXED (Nov 8 2026) in `src/modules/config/ConfigModule.cpp`.
Root cause confirmed by reproduction; regression test
`scripts/smoke/test_issue_config_set_in_module_scope.hv`.

## Root cause (confirmed by reproduction, Nov 8 2026)

The config plugin registers the live config object into the globals map of
whatever scope was active at registration time, and its call-time lookup
(`configObjAtCall`) read ONLY that ambient map:

    auto configObjAtCall = [api]() -> Value {
      auto &g = api.vm().getGlobals();
      auto it = g.find("config");
      if (it != g.end() && it->second.isObjectId()) return it->second;
      return Value::makeNull();
    };

`config.set` then repopulates that object. When the lookup returned null,
`setNestedField` called `api.setField(null, ...)` and VMApi::setField threw
`VMApi::setField: expected object` (throw site:
`src/havel-lang/compiler/vm/VMApi.hpp:84`).

The null was produced by a SCOPE mismatch, not by a missing registration:

- The config object is registered EAGERLY (the engine registers it before
  the script runs - `config`, `cfg` and `conf` all resolve to an object in
  a script that never imports the module).
- `use "std/config"` in the MAIN script therefore found the module already
  registered and did not re-register anything in the main scope.
- `start.hv` is a separate module. Its own `use "std/config"` hit the
  module CACHE (no re-registration), so start.hv ran with a sandbox globals
  map that never carried the `config` global - the ambient-only lookup
  returned null.

Minimal reproduction (main scope imports config FIRST, then a module that
calls `cfg.set`):

    // main.hv
    use "std/config" as cfg
    use "writer"
    // writer.hv
    use "std/config" as cfg
    cfg.set("Probe.ScopeKey", "from-module")

Before the fix: `Execution error: VMApi::setField: expected object`.
Without the main-scope import the same module script PASSED - the import
ordering is what decides whether the object is in the caller's scope.

## Fix

`configObjAtCall` now uses `VM::findGlobalValueByName`, which walks the
same scopes a global read can reach: ambient, module base layer, then the
pushed scope frames. `repopulate` returns early when there is no live
object instead of throwing, and `config.restore` uses the same live-object
lookup instead of the object captured at registration time.

## Verification

- `scripts/smoke/test_issue_config_set_in_module_scope.hv` (new): the
  failing shape must pass, the no-main-import shape must pass (negative
  control), and the value written from the module scope must read back.
  Runs in a child process with a sandboxed HOME so the developer's real
  config is never touched.

## Original report (Nov 3, user run)

- Failure: `Execution error: VMApi::setField: expected object`.
- Timeline: modules load 23:09:46-52 (script imports), `FFICall libXtst`
  23:09:52.869, profiler dump (`throws=1`) 23:09:53.075, then
  `Emergency released 0 virtual keys` (uinput teardown,
  src/core/io/UinputDevice.cpp:318), then the error is printed by the
  launcher.
- hotkeys0.3.hv line 36 imports `start.hv`, whose top-level runs the
  `config {}` block and `cfg.set` x3 - the calls that hit the null lookup.

The initial reproduction attempts failed because the host was in a
storage-congestion storm (see havel-process-linger-during-box-storms.md);
every havel run stalled before its first output. The reproduction above
was possible only in a calm window.
**Status:** FIXED (Nov 8, 2026). Root cause confirmed with gdb; regression
test `scripts/smoke/test_issue_config_set_in_module_scope.hv` (plus fixture
`scripts/smoke/config_scope_fixture/setter.hv`).

## Symptom

Running `~/scripts/havel/hotkeys0.3.hv` ends with:

    2026-11-08 19:10:10.034 [WARNING] [VM][GC] 1 external roots still pinned at VM shutdown
    2026-11-08 19:10:10.173 [INFO] Emergency released 0 virtual keys
    2026-11-08 19:10:10.221 [ERROR] Execution error: VMApi::setField: expected object

`throws=1` in the profiler dump; the script dies before it reaches its
event loop. Reproduced headless with a sandbox HOME:

    HAVEL_HEADLESS=1 HOME=$SBX ./build-debug/havel ~/scripts/havel/hotkeys0.3.hv
    -> Execution error: VMApi::setField: expected object   (exit 1)

## Root cause (gdb, Nov 8)

Backtrace at the throw (`break VMApi.hpp:84`):

    #0 VMApi::setField (obj=<not an object>, key="Compiler") VMApi.hpp:84
    #1 havel::modules::setNestedField (key="Compiler.JITWarnings") ConfigModule.cpp:373
    #2 registerConfigModule(...)::$_14::operator()(Value configObj) ConfigModule.cpp:253
    #3 registerConfigModule(...)::$_2::operator()(args) ConfigModule.cpp:272   <- config.set
    ...
    #12 VM::loadModule (path="start") VM.cpp:7371

`config.set` -> `repopulate(configObjAtCall())` -> `setNestedField` ->
`setField(null, ...)`.

`configObjAtCall()` resolved the `config` global through
`api.vm().getGlobals()`, which is the AMBIENT scope. The failing call came
from `start.hv`'s TOP LEVEL (`cfg.set("Debug.VerboseKeyLogging", DOLOG)`),
and a module body runs in the VM's module SANDBOX - a fresh globals map that
does not carry the importing script's `config` global. The lookup therefore
returned null and the repopulate path handed that null to `setField`.

The same ambient-only lookup was used by the registration-time reuse branch,
so a plugin registering during a module load could not find the script's
existing `config` object and built a SECOND one - the "two configObj
instances" case the surrounding comment already described.

## Fix

- `src/havel-lang/compiler/vm/VMApi.hpp`: new `findGlobalValue(name, out)` -
  resolves a global across the same scope chain a script-level read uses
  (ambient -> module base layer -> pushed globals_stack_ frames), i.e. the
  existing `VM::findGlobalValueByName`.
- `src/modules/config/ConfigModule.cpp`:
  - registration-time reuse looks the `config` global up across all scopes
    (no second config object);
  - `configObjAtCall()` looks it up across all scopes (was ambient-only);
  - `repopulate()` bails out when the object cannot be resolved instead of
    handing null to `setField`.

## Verification

- Pre-fix binary: `scripts/smoke/test_issue_config_set_in_module_scope.hv`
  -> `Execution error: VMApi::setField: expected object` (exit 1).
- Post-fix binary: same test -> `PASS: config_set_in_module_scope` (exit 0).
- `scripts/tests/test_config_shadow.hv` -> `test config shadow passed` (exit 0).
- `~/scripts/havel/hotkeys0.3.hv` (sandbox HOME) -> no execution error.

## Related

- `docs/known-issues/cfg-section-chain-shell.md` (same module, earlier defect
  chain: the `std/config` binding)
- `docs/known-issues/havel-process-linger-during-box-storms.md` (the storm
  window that blocked the earlier reproduction attempts)
