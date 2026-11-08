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
