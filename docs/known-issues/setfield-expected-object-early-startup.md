# `VMApi::setField: expected object` kills hotkeys0.3.hv during early startup

**Status:** FIXED. Two independent fixes were landed for this defect and
merged: `507954af` (havel-2) and `313dec21e` (main), plus `9020effb9`
which fixed a dangling-capture SIGSEGV in the first fix. Regression test:
`scripts/smoke/test_issue_config_set_in_module_scope.hv`.

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

A MODULE body runs in that sandbox, whose globals map holds only the
module's own names. The config globals (`config`, `cfg`, `conf`) are
registered into the scope that was active at plugin-registration time, so
the ambient-only lookup missed them in every module scope.

This is not a missing registration:

- The config object is registered EAGERLY (the engine registers it before
  the script runs - `config`, `cfg` and `conf` all resolve to an object in
  a script that never imports the module).
- `use "std/config"` in the MAIN script therefore found the module already
  registered and did not re-register anything in the main scope.
- `start.hv` is a separate module. Its own `use "std/config"` hit the
  module CACHE (no re-registration), so start.hv ran with a sandbox globals
  map that never carried the config globals.

Two failures follow from this one root cause:

1. `config.set` repopulates the object. With a null lookup, `setNestedField`
   called `api.setField(null, ...)` and VMApi::setField threw
   `VMApi::setField: expected object` (throw site:
   `src/havel-lang/compiler/vm/VMApi.hpp:84`).
2. A `config { ... }` block compiles to `conf.key = value; config.set(...)`
   (ByteCompiler.cpp, ConfigBlock case). With no `conf` global in the module
   scope, the block failed earlier with `Undefined variable: 'conf'`.

The same ambient-only lookup was used by the registration-time reuse branch,
so a plugin registering during a module load could not find the script's
existing `config` object and built a SECOND one - the "two configObj
instances" case the surrounding comment already described.

Minimal reproduction (a module that uses a `config {}` block and cfg.set):

    // main.hv
    use "std/config" as cfg
    use "writer"
    // writer.hv
    use "std/config" as cfg
    config { Probe.BlockKey = "from-block" }
    cfg.set("Probe.ScopeKey", "from-module")

Before the fix: `Undefined variable: 'conf'` (and, without the block,
`VMApi::setField: expected object`).

## Fix

- `src/havel-lang/compiler/vm/VMApi.hpp`: new `findGlobalValue(name, out)` -
  resolves a global across the same scope chain a script-level read uses
  (ambient -> module base layer -> pushed globals_stack_ frames), i.e. the
  existing `VM::findGlobalValueByName`.
- `src/modules/config/ConfigModule.cpp`:
  - registration-time reuse looks the `config` global up across all scopes
    (no second config object);
  - `configObjAtCall()` resolves the LIVE config object - the one carrying
    the `__module_object` marker - across all scopes, with the
    registration-time object as the primary source;
  - `repopulate()` bails out when the object cannot be resolved instead of
    handing null to `setField`;
  - `config.restore` uses the same live-object lookup instead of the object
    captured at registration time.

Two details that the first attempt got wrong, both caught by the A/B:

- The lookup must REJECT the .hv wrapper: in a module scope `use "std/config"`
  binds the wrapper namespace under the name "config", and that wrapper is
  an object too. Refreshing the wrapper leaves the live object stale - the
  write reaches the config store but the script's `cfg.<Section>.<Key>` read
  still returns null.
- The lambdas must capture `VMApi` BY VALUE. Capturing by reference (or
  capturing a lambda that itself captured by reference) dangles once
  registerConfigModule returns: the later `config.set` ran against a
  dead frame and SIGSEGV'd inside GCHeap::ownerThread (gdb, Nov 9).

## Verification

A/B against the pre-fix plugin (same tree, only the lookup hunk reverted):

- fixed plugin: `PASS: cfg.set() from a module scope updates the live config
  object and does not crash` (exit 0)
- pre-fix plugin: exit 1, `Undefined variable: 'conf'`

The regression test also covers: the no-main-import shape must pass
(negative control), the value written from the module scope must read back
on the same object, and a saved value must be readable from the file by a
separate process. Child processes run with a sandboxed HOME, so the
developer's real config is never touched.

Earlier verification on the main-line fix (`313dec21e`):

- Pre-fix binary: `scripts/smoke/test_issue_config_set_in_module_scope.hv`
  -> `Execution error: VMApi::setField: expected object` (exit 1).
- Post-fix binary: same test -> `PASS: config_set_in_module_scope` (exit 0).
- `scripts/tests/test_config_shadow.hv` -> `test config shadow passed` (exit 0).
- `~/scripts/havel/hotkeys0.3.hv` (sandbox HOME) -> no execution error.

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

## Related

- `docs/known-issues/cfg-section-chain-shell.md` (same module, earlier defect
  chain: the `std/config` binding)
- `docs/known-issues/havel-process-linger-during-box-storms.md` (the storm
  window that blocked the earlier reproduction attempts)
