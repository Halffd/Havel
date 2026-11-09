# `VMApi::setField: expected object` kills hotkeys0.3.hv during early startup

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
