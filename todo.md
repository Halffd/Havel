# todo

Working state and follow-ups. Dated entries; remove when done.

## open

- [ ] 2026-10-15 — fix the tier-1 depth-model refusal in
  `src/havel-lang/compiler/cranelift-backend/src/lib.rs` (~911):
  `chunkAt`/`chunkLeft` in `scripts/smoke/chunk_navigator.hv` refuse
  with "join depth conflict (1 vs 0)" and silently fall back to the
  interpreter. The default-arg prologue
  (`LOAD_VAR; DUP; JUMP_IF_NULL skip; POP; LOAD_GLOBAL; STORE_VAR`)
  rejoins its arms at genuinely different operand depths — benign under
  the interpreter's truncate-at-RETURN semantics. Fix: converge joins
  to the maximum depth, synthesize null pads on shallower predecessor
  edges. Then delete the `KNOWN_FAILURES` entry in
  `scripts/ci/jit-parity-test.sh` — the gate holds the script from
  then on. Full mechanism in `scripts/benchmarks/README.md`
  ("Known open issue caught by the JIT parity gate").

- [ ] deferred by user — Havel++ syntax discussion, until cleanup and
  phases end.

## done (kept for orientation)

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
