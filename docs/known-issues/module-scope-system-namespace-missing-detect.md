# Module scope resolves a `system` object that has no `detect` field

**Status:** FIXED. The `wm: null` symptom had **two** independent root
causes, both landed. Regression test:
`scripts/tests/test_sys_module_exports.hv`. A/B confirmed the test fails
before the fix (`sys namespace must expose windowManager from
modules/std/sys.hv`) and passes after.

It is **not** in `WindowManagerDetector`: that class always returns a
non-empty name (`"Unknown"` at minimum), the bridge substitutes
`"unknown"` for an empty string, and both `system.detect` implementations
always set the `windowManager` field. See "Ruled out".

## What was actually wrong

Root cause 1 (the `wm: null` the user saw): `SysModule::registerSysModule`
never loaded its Havel-side layer. It registered the 16 native `sys.*`
host functions and published `sysObj`, but never called
`vm.loadModule("sys")` the way every other native module does
(ArrayModule, ShellModule, MathModule, ObjectModule, TypeModule, ...).
`modules/std/sys.hv`'s `windowManager()`, `os()`, `kernel()`,
`desktopEnv()` and `displayProtocol()` therefore never reached the
namespace object, so `sys.windowManager` read back as a missing field and
`start.hv`'s `sys.windowManager.lower()` yielded null.

Root cause 2 (the module-scope null, recorded below when it appeared to
be the whole story): `SysModule` also REPLACED the `system` global with a
fresh object *after* `VM::loadModule` had snapshotted the earlier one into
`baseModuleGlobals_`, so a module context resolved an object carrying only
`gc`/`gcStats` — `system.detect()` called from inside a module function
returned null. That is now fixed by merging into the live object rather
than forking it, and `registerDefaultHostGlobals` resolves the live object
across the whole scope chain instead of the ambient map alone.

Both defects are masked by a third, separate trap: `sys.windowManager` is
a **function** (`fn windowManager() { system.detect().windowManager }`),
never a value — it has always been defined that way since `cc906734`. The
user's `start.hv:67` wrote `sys.windowManager.lower()` without parens,
and `.lower()` on a function silently returns null, which is why the
symptom looked like "the WM detector failed" rather than "the call is
missing a paren". The user's script was corrected to
`sys.windowManager().lower()`. `modules/std/os.hv`-style delegators are why
the merge must skip fields the native layer already registered: letting a
wrapper replace `sys.platform` closed a self-call loop
("module wrapper execution depth exceeded (max 50)").

## Symptom

`~/scripts/havel/hotkeys0.3.hv` imports `wm` from `start.hv`, whose line 67 is

    wm = sys.windowManager.lower()

and `modules/std/sys.hv:51` is

    fn windowManager() { system.detect().windowManager }

The output is `wm: null`. `null.lower()` yields null, so the interpolated
string prints `null` rather than an empty or `"unknown"` name.

The same call at the top level of the same script works: `system.detect()`
returns an object and `.windowManager` is `"Cinnamon"`. The failure is
specific to a **module function** calling `system.detect()`.

## Reproducer

Two files in the same directory (`/tmp/opencode/wm/` during the session):

    // mymod9.hv
    fn rsd() { system.detect }
    fn rgc() { system.gc }

    // wmmod13.hv
    use mymod9
    print("mod gc      = " + str(mymod9.rgc()))
    print("mod gcStats = " + str(mymod9.rgcs()))
    print("mod detect  = " + str(mymod9.rdet()))
    print("mod hardware= " + str(mymod9.rhw()))
    print("top detect  = " + str(system.detect))

Run in the chroot:

    xbps-uchroot -b /home/void/Havel:/src -b /tmp/opencode/wm:/wmt \
      masterdir-x86_64 -- sh -c "cd /wmt && \
      XDG_CURRENT_DESKTOP=X-Cinnamon XDG_SESSION_TYPE=x11 \
      HAVEL_STDLIB=/src/modules/std /builddir/havel-dev/havel wmmod13.hv"

Observed (binary `/builddir/havel-dev/havel`, commit
`9ad6a2fe28088560996b57b076d6c8eb9622a8ec`):

    mod gc      = <fn system.gc>
    mod gcStats = <fn system.gcStats>
    mod detect  = null
    mod hardware= null
    top detect  = <fn system.detect>

GetWMName-related note: with `DISPLAY` set the top-level value is
`"Cinnamon"` (the `SystemBridge` implementation); without it the value is
`"cinnamon"` (the `SysModule` implementation, whose pgrep cascade falls
through to its `XDG_CURRENT_DESKTOP` hint). Both are non-null, so which
implementation wins is a separate question from this defect.

## Evidence

OBSERVED, in order:

1. `type(system)` inside a module function is `object`; `type(system.detect)`
   is `null`. The object resolves; its **field is missing**.
2. `system.gc` and `system.gcStats` are functions in that same object;
   `system.detect` and `system.hardware` are null. So the object a module
   resolves carries exactly the two fields that
   `VM::registerDefaultHostGlobals()` sets on its own fresh `system`
   object.
3. Assigning `system.zprobe = "set-from-top"` at top level, then reading
   `system.zprobe` from a module function, returns **null**. The top-level
   read returns the string. The two scopes therefore hold **two different
   objects**, not one object observed through two paths.
4. `fs`, `window` and `process` all resolve to functions in the same module
   function, so this is not a general failure of the module base layer —
   it is specific to the duplicated `system` object.
5. With `HAVEL_DEBUG_IO=1`, the `[SystemDetect]` trace in
   `SystemBridge::handleSystemDetect` never prints for a module-scope call.
   The null is produced at **name resolution**, before the handler runs, so
   the handler's `if (!ctx || !ctx->vm) return Value::makeNull();` early-out
   is not the source.
6. A clean `HOME=/tmp/cleanhome HAVEL_CACHE_DIR=/tmp/cleanhome/cache`
   reproduces identically. The stdlib `.hvc` cache is not involved.
7. A temporary trace in `VM::registerDefaultHostGlobals()` printed:

       [TRACE] rdg: ambient has system=0 id=0 fresh_obj=8

   meaning: in the one call to that function during the process, the ambient
   `globals` map had **no** `system` entry, so the `else` branch ran and a
   fresh object (id 8) carrying only `gc` and `gcStats` became
   `globals["system"]`. Its field set matches observation 2 exactly.

HYPOTHESIS (supported by 2, 3 and 7; NOT confirmed): the half-populated
object installed by `registerDefaultHostGlobals` is the one a module context
resolves (captured into `baseModuleGlobals_` at first module load), while
the top level later resolves a second, complete object created by
`SystemBridge`'s `system_object_initializer` else-branch. The exact
interleaving that leaves two objects in place is not yet pinned.

## Code map

- `src/havel-lang/compiler/vm/VMHostFunctions.cpp:4349-4371`
  (`VM::registerDefaultHostGlobals`) — allocates a fresh `system` object
  with `gc`/`gcStats` and installs it when the ambient map has no live
  `system` object. This is the only place that builds a `system` object
  without `detect`/`hardware`.
- `src/host/module/bridges/SystemBridge.cpp:19-43`
  (`system_object_initializer`) — merges `detect`/`hardware` into
  `globals["system"]` when it exists, otherwise allocates a new object with
  those fields and **replaces** the global. Same replace-vs-merge shape.
- `src/havel-lang/stdlib/SysModule.cpp:1236` — third creator of the
  `system` global (`api.setGlobal("system", systemObj)`).
- `src/havel-lang/compiler/vm/VMHostFunctions.cpp:4667-4733`
  (`VM::buildNamespaceGlobals`) — fourth path; merges namespace fields into
  an existing object.
- `src/havel-lang/compiler/vm/VM.cpp:7248-7259` (`VM::loadModule`) —
  `baseModuleGlobals_` captures a **snapshot** of object-typed globals at the
  first module load and is never refreshed (`if (!baseModuleGlobals_ && ...)`).
- `src/havel-lang/compiler/vm/VMDispatch.cpp:44-174` (`LOAD_GLOBAL`) —
  resolution order: ambient -> `host_function_globals_` -> closure sidecar ->
  `baseModuleGlobals_` -> pushed `globals_stack_` frames.
- `src/havel-lang/compiler/vm/VM.hpp:2256` (`findGlobalValueByName`) —
  resolves a global across the **full** chain. Already exists and is the
  pattern the earlier `config` fix used; it is the obvious tool for this
  one.

## Ruled out

- `WindowManagerDetector::GetWMName()` returning an empty string: the
  switch has a `default: return "Unknown"`, and `detectSystem()` assigns it
  unconditionally on Linux.
- `SystemBridge::handleSystemDetect`'s `!ctx || !ctx->vm` null return: the
  `HAVEL_DEBUG_IO` trace shows the handler never runs for the failing call.
- `SysModule`'s `system.detect` object lacking `windowManager`: it calls
  `api.setField(obj, "windowManager", api.makeString(windowManager))`
  for every path, and `runCmd` returns a non-empty string or `"unknown"`.
- The stdlib `.hvc` bytecode cache.
- The `CheckEnvironmentVar` colon-list change (`83bf00f4`, `c4b58589`): it
  can only make environment matching more permissive, never less, so it
  cannot turn a WM name into null. It can mis-resolve
  `X-Cinnamon:GNOME` to GNOME, but that yields `"gnome"`, not `null`.

## Failed approach

Instrumenting the three `system`-object creation sites with
`if (getenv("HAVEL_TRACE_SYSTEM")) fprintf(stderr, ...)` produced a hard
SIGSEGV (exit 139) with no ASAN report, from a script with no module
imports, while the same binary runs clean with the variable unset. The
instrumented binary cannot be trusted; do not re-instrument these sites
that way. Use the debugger, or a read-only probe that derives the answer
from script-visible state (the `zprobe` marker trick in observation 3 is
the shape that works).

## Next step

Restore the invariant that there is exactly one `system` object. The
smallest change that addresses the traced cause is in
`VMHostFunctions.cpp:4349-4371`: resolve the live object through
`findGlobalValueByName("system", ...)` (full scope chain, as above) instead
of the ambient `globals` map alone, merge `gc`/`gcStats` into it, and
re-point the ambient at it so the module base layer and the top level agree
on one object. Verify against the reproducer above (`mod detect` must become
`<fn system.detect>`), plus a regression test that fails before the change,
plus the smoke gate and `scripts/ci/jit-parity-test.sh`.

Note the same class of defect was already fixed for `config`
(`507954af`, `313dec21e`, `9020effb9`, regression test
`scripts/smoke/test_issue_config_set_in_module_scope.hv`): an ambient-only
global lookup returning null inside a module sandbox. `system` is the second
instance. Any fix here should follow that precedent — resolve across the
whole chain, never only the ambient map.