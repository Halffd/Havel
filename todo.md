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

- [ ] 2026-10-15 — re-run the full smoke gate
  (`./build-debug/hvtest --smoke --slow-too`) on a quiet machine for a
  clean suite number. The post-orc-quartet-fix run was polluted by
  sibling-build contention: 317 passed, 34 failed, where all 34 were
  33 timeouts of unrelated trivial scripts (time_*, bitwise_xor, ...)
  at loadavg 12-14 plus the known qt baseline. The four orc quartet
  tests passed in-gate and 2/2 standalone; expected clean result is
  349/351 (repl + qt UI-backend baselines only).

- [ ] deferred by user — Havel++ syntax discussion, until cleanup and
  phases end.

## done (kept for orientation)

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
