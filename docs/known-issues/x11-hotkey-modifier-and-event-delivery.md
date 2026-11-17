# X11 hotkeys: the modifier bits disagreed between the grab and the match, and a refused root-window subscription silenced every key event

**Status:** FIXED (this session). Four independent defects on the X11 input path, all confirmed by direct experiment on the box and all now covered by regression tests.

## Impact

On a box where the X11 input path is used (`--io x11`, or a box with no readable evdev devices):

1. **Ctrl and Shift were swapped** in every hotkey match. A hotkey bound to `^A` (Ctrl) was matched as if it required **Shift**, and a hotkey bound to `+A` (Shift) was matched as if it required **Ctrl**. A hotkey with no modifier was unaffected.
2. **No key events at all** when another X client held a pointer grab. The root-window subscription was refused with `BadAccess`, and the refusal took the *key* events down with it - so the process received nothing, not even the keys it had asked for.
3. The standalone X11 hotkey monitor compared an X11 event state against an abstract modifier mask **in two incompatible layouts**, so it could never match a hotkey carrying a modifier.
4. A removed hotkey could leave a **stale grab** on the X server that kept stealing the key from every other application.

## Defect 1: two modifier bit layouts

`ModifierMasks` (the platform-neutral layout, in `src/core/io/IOBackend.hpp`) had:

```cpp
SHIFT   = (1 << 0);
CONTROL = (1 << 1);
```

The hotkey matchers - `EventListener::CheckModifierMatch` and `HotkeyManager::checkModifierMatch` - read the **evdev** layout:

```cpp
ctrlRequired  = (requiredModifiers & (1 << 0)) != 0;
shiftRequired = (requiredModifiers & (1 << 1)) != 0;
```

And `IO::ParseModifiersAndFlags` produced the evdev layout on the evdev path and `ModifierMasks` on every other path:

```cpp
result.modifiers |= isEvdev ? (1 << 0) : ModifierMasks::CONTROL;
```

So on the evdev path both sides used the evdev layout (correct), and on the X11 path the parser produced `CONTROL = bit 1` while the matcher read bit 1 as **Shift**.

**Fix:** one layout for every backend - `ModifierMasks` is aligned with the evdev layout (`CONTROL = bit 0`, `SHIFT = bit 1`, `ALT = bit 2`, `META = bit 3`), `ParseModifiersAndFlags` produces `ModifierMasks` unconditionally, and every producer and consumer now names the constants instead of spelling the bits out inline.

Regression lock: `tests/io_modifier_layout_ut.cpp` (`io_modifier_layout_test`).

## Defect 2: a refused root-window subscription silenced every key event

`X11Adapter::SelectRootInputEvents()` asked for the key **and** the pointer events in a single `XSelectInput` call:

```cpp
XSelectInput(display_, root_,
    KeyPressMask | KeyReleaseMask |
    ButtonPressMask | ButtonReleaseMask |
    PointerMotionMask | ButtonMotionMask);
```

`XSelectInput` **replaces** the event mask of the window, and the X server refuses the whole request with `BadAccess` when it selects `ButtonPress`/`ButtonRelease` on the root window while another client holds an active pointer grab. A refused request leaves the **previous** mask in place; it does not partially apply.

So the `ButtonPress`/`ButtonRelease` bits were refused, and the key events went down with them - even though a key-only subscription would have been accepted.

Confirmed by experiment (`/tmp/opencode/xmaskprobe.c`, `xsplitprobe.c`):

```
KeyPress only                       error=-1
ButtonPress only                    error=10    <- BadAccess
PointerMotion only                  error=-1
ALL (havel's mask)                  error=10

step 1 (key mask)        error=-1  mask now = 0x3   <- keys active
step 2 (button+motion)   error=10  mask now = 0x3   <- refused, keys SURVIVE
```

**Fix:** two calls. The first asks for the key events alone - a refusal there is a real failure, because no hotkey can fire without them, and it is retried. The second asks for the key bits **and** the pointer bits: if it succeeds the mask holds both, and if it is refused the key-only mask from the first call stays in effect. Asking for the pointer bits alone would have *wiped* the key subscription, because `XSelectInput` replaces the mask.

The refusal is now detected (`ScopedX11ErrorProbe`), reported at the right severity, and retried.

Regression lock: `tests/io_x11_select_input_retry_ut.cpp` (`io_x11_select_input_retry_test`) covers the retry rule.

## Defect 3: the X11 hotkey monitor compared two incompatible layouts

`X11HotkeyMonitor::MonitorLoop` compared the X11 event state directly against the abstract modifier mask:

```cpp
const unsigned int cleanedState = CleanMask(keyEvent->state);   // X11 PLATFORM bits
...
static_cast<int>(cleanedState) == hotkey.modifiers               // ModifierMasks bits
```

The X11 state uses the server's modifier bits (`Shift=0, Lock=1, Control=2, Mod1=3, Mod2=4, Mod4=6`); `ModifierMasks` uses the evdev bits (`Ctrl=0, Shift=1, Alt=2, Meta=3`). The two are not interchangeable, so the monitor could never match a hotkey that carries a modifier.

**Fix:** one shared conversion, `havel::X11StateToAbstractMask(state, numlockMask)`, used by `X11IOBackend::ToAbstractMask` and by every other X11 consumer, so they cannot drift apart again. The NumLock discovery also had to match the backend's rule: the **first** modifier row carrying Num_Lock wins (the previous code took the last match, so the resulting bit depended on the iteration order).

Regression lock: `tests/io_x11_state_mask_ut.cpp` (`io_x11_state_mask_test`).

## Defect 4: a removed hotkey left a stale grab behind

`X11IOBackend::RegisterHotkey` and `UnregisterHotkey` expanded the grab mask into the variants `{0, LockMask, numlock, numlock|Lock}` on top of a modifier value that `ToPlatformMask` had **already** OR-ed `LockMask`/`numlockMask_` into. The "no lock modifier" variant was therefore unreachable: it was never grabbed, never ungrabbed, and the removed hotkey left a live grab on the X server that kept stealing the key from every other application.

**Fix:** one shared free function, `havel::GrabMaskVariants(modifiers, numlockMask)`, used by **both** the register and the ungrab path.

Regression lock: `tests/io_x11_grab_mask_variants_ut.cpp` (`io_x11_grab_mask_variants_test`).

## The dead monitor was removed

`EventListener::StartX11Monitor` had **no callers**: the `X11HotkeyMonitor` was a parallel implementation of the `X11Adapter` path and was never wired into production. The production X11 path is `X11Adapter` -> `EventListener::SetKeyCallback` -> `OnBackendKeyEvent` -> `ProcessKeyboardEvent` -> the hotkey matcher.

Leaving the class in place meant two X11 delivery paths that could drift apart - and this one had already drifted (the incompatible mask comparison above). The class, its header, and the three `EventListener` methods that drove it are removed.

## End-to-end verification

`scripts/tests/test_x11_hotkey_delivery.hv` proves the whole path on a real X server: it registers a Ctrl+F24 binding and a Shift+F24 binding, injects a Ctrl+F24 press with XTest (`scripts/tests/x11inject.c`), and asserts that the Ctrl binding fired exactly once and the Shift binding did not fire.

The test is manual-run, not part of `scripts/smoke`: injecting input must never happen in the automated suite.

A note on the semantics it relies on: a hotkey with **no** modifier matches whatever modifiers happen to be held (the "ignore modifiers unless explicitly set" rule), so the plain binding fires for a Ctrl press too. The discrimination check therefore uses a **different** modifier (Shift), not the absence of one.

## A note on reading the gate output on a loaded box

The gate's per-test timeout is 300 s. On a box whose load average is in the
30s, a script that normally starts in 5 s can take 300 s to start, and the
gate reports it as `[FAIL] <name> (timeout)`. The gate summary makes this
visible: a run with `99634 ms/test avg` is a **load** report, not a
correctness report.

### Measured: the per-test startup cost, and what it is made of

`HAVEL_STARTUP_TIMING=1` breaks the launch down. On this box, a trivial
script (`print("x")`) against the shared `~/.cache/havel`:

```
[startup] vm-create               =  517.56ms
[startup] stdlib-register-pure    = 29577.96ms     <- the bulk
[startup] modules-install         =  910.24ms
[startup] HavelEngine::initializeFull TOTAL = 31465.69ms
```

The same measurement against the **three** trees on this box, same script,
same moment:

| tree | `stdlib-register-pure` | `initializeFull TOTAL` |
|------|------------------------|------------------------|
| `/host/havel` (main) | 55.2 s - 63.2 s | 59.9 s - 65.2 s |
| `/host/havel-2` | 8.9 s - 10.0 s | 10.2 s - 11.5 s |
| `/host/havel-3` (this tree) | 6.6 s - 7.3 s | 7.8 s - 8.5 s |

So the cost is **not** introduced by the changes in this document: the
baseline tree is the slowest of the three.

`strace` of the same launch shows the shape of the work: 35 distinct `.hv`
module files are opened **292** times (each `modules/lang/math/*.hv`
11-21 times), and the `.hvc` bytecode cache is opened 5-6 times per entry.
The compile path also reports:

```
[compile] cache:      miss
[compile] cache.reject: pipeline-fingerprint-drift (3c05b6b9b05f != f818bcbfde69)
```

- i.e. the bytecode cache entry is **rejected on every launch**, so every
module is recompiled from source. The four pipeline-fingerprint inputs
(`lang.emitter/pratt/lexer/scope`) are deliberately not content-keyed, so
they are fought over by all four checkouts on the box - see
`shared-bytecode-cache-cross-worktree-thrash.md` and
`havel-process-linger-during-box-storms.md`.

### The gate workaround that works

Point `XDG_CACHE_HOME` at a **persistent** scratch directory for the tree.
This was previously blocked because a cache outside `$HOME/.cache/havel`
made `test_python_bridge` fail under AddressSanitizer with a 2289-byte
CPython leak; that leak is fixed by the change in
`src/modules/python/PythonModule.cpp` (see
`python-bridge-cpython-module-leak.md`), so the workaround is usable
again.

Measured on this box, same binary, same trivial script:

| cache | wall |
|-------|------|
| shared `~/.cache/havel` | 11.85 s - 27.45 s |
| private, cold (first launch) | 47.72 s |
| private, warm | **1.45 s - 3.16 s** |

The cache must be **persistent**: a `mktemp -d` that is discarded between
runs pays the cold cost (47.72 s) every time.

Effect on the smoke gate, same tree, same binary, same command:

| cache | result | per-test avg |
|-------|--------|--------------|
| shared | 315 passed, 77 failed (all timeouts) | 33103 ms |
| shared | 372 passed, 20 failed (all timeouts) | 24289 ms |
| shared | 387 passed, 5 failed (all timeouts) | 7966 ms |
| private, cold | 360 passed, 32 failed (all timeouts) | 11936 ms |
| private, warm | 383 passed, 9 failed (all timeouts) | 11049 ms |
| private, warm | 366 passed, 26 failed (all timeouts) | 12773 ms |
| private, warm | 336 passed, 56 failed (all timeouts) | 19887 ms |
| private, warm | 309 passed, 83 failed (82 timeouts, 1 exit=1) | 25878 ms |
| private, warm | 366 passed, 26 failed (all timeouts) | 12773 ms |
| private, warm | 337 passed, 55 failed (all timeouts) | 23151 ms |

The failing set is different in every run and the count rises and falls
with the box's load average (measured: 6-10 for the 5-failure run, 11-16
for the 26-failure run, 16-24 for the 56-failure run) - that is
contention, not a code defect. Every failing test was re-run individually
and **passed**:

| run | re-run | result |
|-----|--------|--------|
| 77 failures | 77/77 | RC=0 |
| 20 failures | 20/20 | RC=0 |
| 27 failures | 27/27 | RC=0 |
| 5 failures | 5/5 | RC=0 |
| 9 failures | 9/9 | RC=0 |
| 26 failures | 26/26 | RC=0 |
| 56 failures | 56/56 | RC=0 |
| 83 failures | 83/83 | RC=0 |
| 26 failures (2026-11-16) | 26/26 | RC=0 |
| 55 failures (2026-11-16) | 55/55 | RC=0 |

The 2026-11-16 runs were the pre-merge gate for the change set in this
document. The 55-failure run was measured at a load average of 16.8-21.2
mid-run; every one of its 55 failures was a **timeout**, and all 55
passed when re-run in a single quiet invocation (`PASS: 55  FAIL: 0`,
RC=0). The per-test startup cost in that run reached 180142 ms
(`test_hpkg_toolchain_store`), which is inside the 300 s budget for one
test but not for four in a row.

### The X11 delivery test passes on a box where the pointer subscription is refused

The end-to-end test
(`scripts/tests/test_x11_hotkey_delivery.hv`, manual-run) was executed on
this box with the binary under test
(`/host/havel-3/build-debug/havel`, commit `f38c0359ae`). The box has an
active pointer grab, so the X server refused the pointer half of the root
subscription - the exact condition that used to take the key events down
with it:

```
[WARNING] X11Adapter: XSelectInput for pointer events on the root window was
refused (error code 10); pointer events are not delivered, key events are
[INFO] EventListener: Grabbed 2/2 input devices
CTRL_FIRED
PASS: X11 hotkey delivery works and Ctrl is not matched as Shift
```

The key events were delivered and the Ctrl binding fired **even though the
pointer subscription was refused** - which is the defect that was fixed.
The same run also proves the modifier layout fix: the Shift binding did
not fire for a Ctrl press.

The per-test timeout is 300 s. A test that needs 160 s of wall time on a
loaded box (measured: `bit_test` and `bit_toggle` at 160210 ms and
160463 ms, load average 24) is inside the budget, but four such tests in
a row are not. The private cache removes the startup cost, but it cannot
remove the CPU contention.



Two smoke scripts are additionally **time-of-check sensitive** and can fail
on assertion (not just timeout) under load:

- `test_conditional_hotkey_gate_self_heal` registers a condition with a
  400 ms window (`flip_after = time.millis() + 400`). If module loading and
  registration take longer than that window, the condition is already false
  when it is first evaluated, the grab never arms, and the script reports
  `FAIL grab armed while condition true` and three more.
- `test_conditional_hotkey_same_key` flips its branch conditions and waits
  200 ms before re-triggering; under load the flip has not propagated yet.

Both were checked against the unmodified baseline binary
(`/host/havel/build-debug/havel`, commit `422fc5570a1f`) under the same
load, and both behave the same there - so these are **not** regressions from
the changes in this document. On a quiet box (load < 7) they pass.

**How to tell a load failure from a regression:** run the failing script
against the baseline binary under the *same* load and compare the output.
Identical output means the failure is not caused by the change under test.

## A smoke script that spawned a stale sibling build

`test_emit_selfhost_parse.hv` needed a *second* havel binary (it runs the
self-hosted launcher as a child process). It picked one by probing the
working directory:

```
candidates = ["${cwd}/build-release/havel", "${cwd}/build-nollvm/havel",
              "${cwd}/build-debug/havel", "${cwd}/havel"]
```

The first existing candidate wins - and in a checkout with several build
trees, `build-release/havel` is routinely from a **different commit** than
the binary under test. The child then loaded the *current* tree's module
plugins, and the loader rejected every one of them:

…
module 'help': build id mismatch (plugin 0x83eb83ce, host 0x48c734b7) -
stale plugin compiled against different VM headers; rebuild the module
```

The message points at the plugins, but the plugins were fine: the child
binary carried the old build id. That made a source-level defect
(`cfg_pipeline_test` also failed, on the same error) look like a broken
build.

**Fix:** the script now resolves the running binary through
`fs.readlink("/proc/self/exe")`, so the child always matches the parent -
same commit, same build id, same plugins. `sys.argv()[0]` would not do:
it is whatever path was typed on the command line (`./havel`), and a
relative path cannot be used to derive the repo root.

The same script had a second cwd dependency: it spawned the child with the
**relative** path `modules/lang/launcher.hv`, which only resolves when the
cwd is the repo root. ctest runs the smoke suite from `build-debug/`, so
the child died with `Failed to read script file: modules/lang/launcher.hv`
and the assertion that reported it pointed at the emit parsing rather than
at the missing file.

### The child processes made the test slower than its own budget

Fixing the stale-binary and cwd problems made the test *fail* differently:
with the child commands now working, the test spawned three children, and
each child pays the **full engine startup**. Measured on this box
(`HAVEL_STARTUP_TIMING=1`, load average 15-20):

```
[startup] vm-create               =  4931.85ms
[startup] stdlib-register-pure    = 24236.73ms   <- the bulk
[startup] modules-install         =  3967.81ms
[startup] HavelEngine::initializeFull TOTAL = 36632.36ms
```

The script's own budget (`// smoke: timeout = 180`) does not cover three
child starts. The test timed out in the gate while the original version
passed - because the original's children **failed fast**: their relative
paths did not resolve, so they exited non-zero in seconds instead of
starting an engine.

**Fix:** the parser is now called **in-process**. `lexer` and `pratt` are
importable by the running script itself, so the test tokenizes and parses
the fixtures directly:

```
use { parse } from "pratt"
use { tokenize } from "lexer"
```

No child process, no engine startup. Measured: 30.4 s (original, three
failing children) and 21.5 s / 8.5 s (in-process) against the script's
180 s budget.

The in-process form is also the **stricter** check. The original asserted
on the child's exit code and on the absence of "Unknown" in the child's
stdout - so a child that never parsed anything at all could satisfy both.
In-process, the assertions are about the AST itself: the parse must
produce no errors, the AST must contain `EmitStatement` nodes and no
`ErrorNode`, and every token must have a non-null `tid`.

**The rewritten test catches its own regression.** With the `Emit` row
removed from the TID map in `modules/lang/lexer.hv`:

```
[FAIL] test_emit_selfhost_parse.hv (exit=1)
  | Bytecode error: a token has a null tid - the pratt binding-power
  | lookup would crash: {col: 4, tid: null, line: 1, value: emit, type: Emit}
```

With the row restored: PASS.

The `ShBlock` row that this change adds to the same TID map is covered by
the drift guard `tests/token_type_names_drift_ut.cpp`, which asserts that
every `TOKEN_TYPE_NAMES` name has a TID entry. With the row removed it
fails:

```
TID map in modules/lang/lexer.hv has no entry for token name "ShBlock"
[  FAILED  ] TokenTypeNamesDrift.TableNamesCoverHvTidMap
```

## A stale test stub after a deliberate production change

`cfg_pipeline_test.JITCompilerBackendForwardsLegacyInterface` failed with
`backend.compile(f) == false`. The production code was right and the test
was stale.

Commit `298c22db1` ("jit: gate tiered execution behind vetted lowerings")
changed `JITCompilerBackend::compile()` from "the call returned without
throwing" to "the JIT says the function is compiled":

```cpp
jit_->compileFunction(func);
return jit_->isCompiled(func.name);   // was: return true;
```

That is deliberate: a compile that did not actually produce a callable
function must surface as a refusal, because the caller marks
`jit_compiled` on success and a marked-but-uncompiled function returns
null from every call instead of falling back to the interpreter.

The test's `StubJIT` still answered `isCompiled()` for `"hot_one"` only,
so `compile("forwarded")` correctly reported a refusal - the stub never
modelled a successful compile. The stub now registers the function it
compiled, so it models the contract the adapter actually checks.

## The plugin build id

The plugin build id itself is `HAVEL_MODULE_BUILD_ID`, computed by CMake
from the contents of the ABI headers
(`src/havel-lang/compiler/vm/VM.hpp`, `VMApi.hpp`, `VMInternals.hpp`,
`CallFrame.hpp`, `src/havel-lang/core/Value.hpp`,
`src/havel-lang/compiler/runtime/RuntimeSupport.hpp`,
`src/c/ModulePlugin.h`) at **configure** time. A change to one of those
headers therefore needs a **CMake reconfigure** before the plugins are
rebuilt; otherwise the plugin keeps the previous id and the host rejects
it. `cmake --build` alone does not re-run the configure step, and Make
reports "Built target" for the plugin because the `.so` is newer than its
own object files.

