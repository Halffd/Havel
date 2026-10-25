# Conditional hotkeys: window conditions never re-evaluate; grab state frozen

Status: FIXED (this change). Documented because the failure mode is subtle
and the machinery spans five files.

## Symptom

`when window.title contains "genshin" { Enter => {...} }` fires (or eats the
key) in the wrong window after an alt-tab, even though the dispatch-time
condition gate exists and re-checks the condition on every press.

## Root causes (three, stacked)

1. **Dispatch gate is fine.** `VM::hotkeyConditionAllows` (VM.cpp) re-invokes
   the stored condition closure synchronously at every trigger, on all four
   dispatch entry points, and re-parks the goroutine when false. A press on
   a condition-gated hotkey never bypasses it (the direct-thunk fast path
   runs *after* the gate).

2. **Window conditions have no reactive deps.** `when window.title contains
   "x"` compiles to `LOAD_GLOBAL window` (a static namespace object) plus a
   host call. The DependencyTracker therefore records dep `"window"` — a
   global nothing ever stores to. No `STORE_GLOBAL` -> no
   `emitVariableChanged` -> the dep-driven re-eval (`reevalConditionalHotkeys`
   / `setOnVarChangedSync` / `ExecutionEngine::onVariableChanged`) never
   fires for focus changes. Grab state stayed frozen at the registration-time
   condition result:
   - registered-while-true -> OS grab armed forever; every keypress eaten by
     the handler even though the gate blocked the body;
   - registered-while-false -> OS grab never armed; key never routed to us
     even after the condition turned true (X11-grabbed keys).

3. **Stale cache window at the gate.** The gate's live `window.title()` read
   flows through `X11Backend::getActiveWindowTitle`, which serves the cached
   active-window snapshot for up to `CACHE_TTL` (50ms) after the actual focus
   change. A press landing inside that window passed the gate on the previous
   window's title and ran the body in the wrong window.

## Fixes

- `WindowBackend::invalidateActiveWindowCache()` (virtual, inert default;
  X11Backend resets its snapshot). The gate calls it before every condition
  eval, and `Havel::startPeriodicTimer`'s window poll calls it on every
  active-window transition.
- Gate self-heal (`hotkeyConditionAllows`): when the live eval contradicts
  `hotkey_condition_last_result`, sync OS grab and HotkeyModule context grab
  by id — closing the frozen-grab desync for any condition whose deps never
  change, not just window ones.
- `Havel::startPeriodicTimer`: the window-check block (previously an empty
  `if`) now polls the active window every 100ms; on transition it
  invalidates the cache and emits the `__active_window__` sentinel through
  `emitVariableChanged` (deferred to the VM thread). All three re-eval paths
  treat the sentinel as match-all so window-reading conditions re-evaluate.
- Grab routing by id everywhere: `core/init/Havel.cpp`'s re-eval path was
  still toggling by alias (first-match wins — the same-key defect class
  fixed elsewhere); now routes by `hotkey_os_id` / `hotkey_id`.
- Removed the synthetic trigger: `ExecutionEngine::onVariableChanged` called
  `wakeHotkey(g, "cond-dep-change")` on a false->true transition, which
  re-queued the goroutine and ran the body with no keypress at all. A
  condition turning true arms the grab; only a real press runs the body.

## Verification

All commands run on `./build.sh 6 build` (mode 6, no JIT backend):

- Fail-first A/B (`scripts/smoke/test_conditional_hotkey_gate_self_heal.hv`,
  run as `./build-debug/havel --run <script> -dhk --log-level debug`):
  - with the self-heal block neutralized: 1 check FAILED
    ("grab healed to false at the gate") — gate logged
    `allow=false (prev=true)`, body blocked, grab stayed stale.
  - with the fix: all 6 checks PASS.
- `./build-debug/hvtest --smoke test_conditional_hotkey`:
  2 passed, 0 failed (gate_self_heal + same_key).
- `./build-debug/hvtest --smoke` (fast tier): 336 passed, 12 failed.
- `./build-debug/hvtest --smoke --slow-too`: 358 passed, 12 failed
  (0 skipped; no test timed out or was manually terminated).
- The 12 failures reproduce IDENTICALLY on a HEAD baseline with all of
  this change's edits reverted (A/B via `git checkout HEAD -- <files>` +
  rebuild): root cause is `modules/core/process.hv` (committed in
  741e40b83 without parsing — `from:` object key and `fn catch`/`fn try`
  keyword names) and display-dependent window tests. Not caused by this
  change; zero regression delta.
- `scripts/ci/jit-parity-test.sh`: NOT RUN — no JIT-capable build exists
  in this worktree (ENABLE_CRANELIFT=OFF, no build-release). The gate
  changes are interpreter-side C++ (`VM::hotkeyConditionAllows` runs
  before any tiered code in the dispatch loop); no JIT lowering or
  tiering path was modified.

## Separate pre-existing defect (partially fixed here)

`modules/core/process.hv:150` uses `from` as an object-literal key; the
C++ parser's `isKeyToken` allowlist rejected it ("Expected variable name
after 'from'"), breaking every smoke test that transitively `use`s the
process module. `From` was added to `isKeyToken` here. The module still
does not parse: it also declares `fn catch(...)` and `fn try(...)`
— reserved-keyword function names — which is a broader grammar decision
(whether keywords should be legal function names) left for its own
change. Until then, the 12 pre-existing failures above remain.
