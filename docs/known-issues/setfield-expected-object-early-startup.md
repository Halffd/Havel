# `VMApi::setField: expected object` kills hotkeys0.3.hv during early startup

**Status:** OPEN, user-reported (Nov 3 23:09, real desktop run of
`~/scripts/havel/hotkeys0.3.hv` on havel-2 build-debug at tip 9fb44bdee).
Reproduction attempts could not complete: every run during that window
produced zero output and stalled (the host-storage congestion documented in
havel-process-linger-during-box-storms.md); one gdb attempt ran 150 s
without hitting a breakpoint (symbol loading itself stalls on the wedged
mount - inconclusive, the breakpoint may also never have bound).

## What is known (from the user's run)

- Failure: `Execution error: VMApi::setField: expected object`
  (throw site: `src/havel-lang/compiler/vm/VMApi.hpp`, `setField` when
  `!obj.isObjectId()` - a host module passed a non-object).
- Timeline: modules load 23:09:46-52 (script imports), `FFICall libXtst`
  23:09:52.869, profiler dump (`throws=1`) 23:09:53.075, then
  `Emergency released 0 virtual keys` (uinput teardown,
  src/core/io/UinputDevice.cpp:318), then the error is printed by the
  launcher.
- No script-side `print` output appears before the failure, so the throw is
  in the early execution path: the import chain or the script's first
  statements. `throws=1` means the throw happened during execution and the
  teardown logs ran while unwinding.

## Suspect path

hotkeys0.3.hv line 36 imports `start.hv`, whose top-level (118 lines) runs:

    config { IO.Executor = Scheduler ... }     // config.set via block
    cfg.get / cfg.set x3 / cfg.save()          // config module
    debug.errorCount()
    sys.* (shell/user/home/displayProtocol/windowManager)
    system.hardware()                          // {cpu,gpu,ram,motherboard,storage}

`system.hardware()` builds objects field-by-field (setField caller list
includes src/modules/system_monitor) and runs at the end of start.hv; the
config paths were recently reworked on this branch (649a3ee7f). Either can
pass a null into setField; nothing has been ruled out.

## Next step (prepared, not yet run)

1. Bisect statically first: `grep -n setField` in
   `src/modules/system_monitor/`, `src/modules/display/`,
   `src/modules/audio/`, `src/modules/clipboard/`, `src/modules/mouse/`,
   and read the call sites for null-able arguments.
2. Bisect dynamically with a SANDBOX HOME (the script calls
   `cfg.save()` - never point it at the real config):
   ```
   SBX=/tmp/opencode/cfgsandbox; ./reset_fixture.sh
   HAVEL_HEADLESS=1 HOME=$SBX timeout 30 ./build-debug/havel probe.hv
   ```
   with probe.hv = `use { DOLOG } from "start"` only (exercises start.hv's
   whole top-level without the hotkey registrations), then narrow within
   start.hv by commenting halves.
3. Only if (2) is clean, bisect hotkeys0.3.hv lines 39-51.
4. Get the precise call site with a working debugger in a storm-free
   window: `gdb -batch -ex 'break VMApi.hpp:84' -ex run -ex 'bt 20'` -
   prefer building a symbol-light copy of the binary on a local disk if
   gdb still stalls on /host.
