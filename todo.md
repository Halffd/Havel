# todo

Working state and follow-ups. Dated entries; remove when done.

## open

- [ ] deferred by user — Havel++ syntax discussion, until cleanup and
  phases end.

## done (kept for orientation)

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
