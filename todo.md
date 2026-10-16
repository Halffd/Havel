# todo

Working state and follow-ups. Dated entries; remove when done.

## open

- [ ] 2026-10-16 — JIT/AOT sweep (user ask, via havel-2-efa39c handoff):
  done so far on the merged tree - jit parity gate 7/7 exit 0 (twice),
  orc quartet 8/8 rc 0, user config chunk helpers tier-compile clean
  (tier1=7, all assertions; the flagged tier-1-refusal risk retired),
  and a NEW fix landed from the sweep itself: 224e0f81 (tier-2
  fast-tier fallback was a guaranteed DuplicateDefinition in no-LLVM
  builds - noisy [hclb] failure lines on every tier-2 promotion).
  NOT COMPLETED, machine-blocked: the full smoke gates (interpreter
  AND fully-tiered HAVEL_TIERING=1 variants) plus hvtest --scripts
  and --compare (AOT) - the interpreter gate attempt died at its own
  60-min timeout with loadavg climbing 5.4 -> 17.5 (trivial scripts
  timing out; invalid run). 10 users + 13 compiler processes share
  these 4 cores; heavy gates cannot produce signal until the box
  quiets. Honest limitation: the crane build has no LLVM, so the
  sweep covers cranelift tier-1 only - ORC tier-2 needs an LLVM build
  (mode 5) to be swept at all.

- [ ] deferred by user — Havel++ syntax discussion, until cleanup and
  phases end.

## done (kept for orientation)

- [x] 2026-10-16 — hk_starvation_repeated: REAL bug found and fixed
  by havel-3 (4888032e7). processGoroutinesInline dropped held
  goroutines still in Created: a persistent hotkey sits in Created
  until its body runs, a second pick in the same pass moved it to
  held, and the pass-end requeue only handled Runnable/Running - the
  goroutine left every queue while Created and pickNext never
  offered it again (dead hotkey for the rest of the process).
  Scheduler::requeuePreservingState fixes it; havel-3 reports
  1-of-10 -> 10-of-10 four consecutive runs. NOTE my earlier
  load-artifact conclusion (below, kept for the record) was wrong in
  mechanism: load raises the double-pick race's probability rather
  than merely starving wall-clock deadlines - the every-failure-under-
  load pattern fit both explanations until the trace
  (wakeHotkey state=3 -> pickNext x2 -> wakeHotkey state=0 -> never
  picked) discriminated. 4888032e7 is NOT yet in main or havel-2;
  taking it into havel-2 after the in-flight gate documents the
  pre-fix baseline.
- [x] 2026-10-16 — SUPERSEDED load-artifact analysis (wrong mechanism,
  kept per evidence discipline): the test's 500ms wall-clock
  deadlines were believed to fail purely under CPU oversubscription;
  every pass was quiet and every failure at loadavg 6+, and
  3af1054d0's A/B showed byte-identical failure on a pre-change
  baseline. The scheduler trace in 4888032e7 proved a real Created-
  state drop race instead.

- [x] 2026-10-15 — chunk_navigator tier-1 refusal fixed, and a second
  latent JIT bug it was masking fixed with it: (1) the cranelift
  depth model now converges join depths to the maximum and pads nulls
  under shallower arms (lib.rs) instead of refusing; (2) the JIT call
  path now pads omitted default args to param_count exactly like the
  interpreter's local init (VM.cpp jitCallArgs) — previously the
  compiled code read past the caller's args array and computed on
  garbage for any tiered function called with a default omitted.
  Gate: jit-parity 7/7 twice (chunk_navigator tier1=5); autocall
  smoke still PASS on build-debug.
- [x] 2026-10-15 — clean full smoke gate after the orc quartet fix:
  `./build-debug/hvtest --smoke --slow-too` =
  **349 passed, 2 failed, 0 skipped | 351 files**; the only failures
  are the two known pre-existing baselines, both exit=1 in their real
  failure mode (no contention timeouts):
  test_issue_repl_script_mode, test_issue_qt_namespace_restored
  (UI-backend-absent). All four orc quartet tests pass in-gate.
- [x] 2026-10-15 — orc quartet root-caused and fixed (0a1e15f4e):
  goroutine tick-budget expiry inside host-fn re-entries
  (VM::call / callFunctionSync) left callee frames live; now disarmed
  around nested loops. README records it under "Resolved: the orc
  quartet".
- [x] 2026-10-15 — JIT parity gate added
  (`scripts/ci/jit-parity-test.sh`): interpreted + tiered runs, tier1>0
  assertion, compile-refusal check, output parity, per-run timeouts.
  AGENTS.md carries the verification protocol; tiered runs print
  `[tiering] WARN` when the JIT was never exercised. First catch: the
  chunk_navigator depth-model refusal above.
