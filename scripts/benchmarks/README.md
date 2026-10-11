# Tier-1 (Cranelift) benchmarks

Fixed workloads for measuring the Cranelift tier-1 backend against the
interpreter. Built during the backend-verification phase (build-crane-nollvm-hl,
release, headless, `ENABLE_CRANELIFT=ON`, `ENABLE_LLVM=OFF`).

## How to run

```bash
B=<crane build>/havel
# interpreter baseline (tiering off - backend is never constructed)
env -u DISPLAY -u WAYLAND_DISPLAY HAVEL_HEADLESS=1 $B scripts/benchmarks/<bench>.hv --minimal
# tier-1 enabled
env -u DISPLAY -u WAYLAND_DISPLAY HAVEL_HEADLESS=1 $B --tiering scripts/benchmarks/<bench>.hv --minimal
```

`--tiering` is the opt-in: it constructs the TieredBackend (Cranelift tier-1;
no tier-2 without LLVM) and arms the hot counters. `--target jit` alone does
NOT enable tiering.

## Methodology

- **Use CPU time, not wall clock.** This repository is routinely built on
  shared machines; measured wall-clock spread on identical runs was 5x under
  load spikes (load average 4-7). Compare `user+sys`:
  `TIMEFORMAT='%U %S'; { time $B ... ; }`.
- Each benchmark's printed output must be **identical** in both modes. A
  mismatch is a correctness bug in the lowering, not a performance result.
- The profiler line (`[profiler] profiler: ...`) is the execution-mode
  evidence: `tier1=N>0` means functions actually tiered up; the interpreted
  instruction count collapsing (306M -> 172k on tier_bench_two_fns) means the
  loops really ran native.

## Baselines (release, headless, no LLVM, CPU seconds)

| bench | interpreter | tier-1 | delta |
|---|---|---|---|
| tier_bench_calls (200k tiny calls) | 2.37 wall, par | 2.23 wall | ~par |
| tier_bench_loop (2M inner iters) | 5.72u + 0.52s | 3.99u + 1.16s | ~18% less CPU |
| tier_bench_two_fns (12M inner iters) | 34.16u + 0.87s | 25.21u + 2.99s | ~20% less CPU |

Compile latency: ~61ms first function (includes one-time backend init),
~1ms marginal per small function (tier_bench_two_fns debug timestamps).

## After the iterator-result reuse (2026-10-08, GC change)

Cost 1 above is fixed: the iterator now allocates its {first, second, done}
result object once per ITER_NEW and only overwrites the fields (GC-rooted
from the iterator; the field writes go through set() so the OBJECT_GET
inline cache re-resolves - with operator[] the cached done=false was served
forever and the compiled for-in loop never exited). The dead // DEBUG block
in havel_vm_iter_next (iterator+range pointer fetches, discarded) is also
gone - real per-call waste on the native path.

Interpreter benefits directly (same heap code):

| bench | interpreter (before) | interpreter (after) | allocations |
|---|---|---|---|
| tier_bench_loop | 3.6-4.1u | 2.18u | ~2M -> 2231 |
| tier_bench_two_fns | 18.9-22.9u | 12.16u | ~6M -> 6236 |

Tier-1:

| bench | tier-1 (before) | tier-1 (after) | tier-1/interp |
|---|---|---|---|
| tier_bench_loop | 2.30-2.80u | 1.10u | 0.50 |
| tier_bench_two_fns | 11.6-14.4u | 6.88u | 0.57 |
| tier_while probe | 0.18u | 0.07u | 0.09 |

Verified: iteration semantics identical (interpreter == tiered) for arrays,
objects, strings, sets, nested loops and value capture; range inclusivity
unchanged; full smoke gate 323 passed / 3 failed (the 3 known UI-backend
tests) / 0 skipped | 326 files; cranelift_proto_driver exit 0; 30 Rust
lowering tests.

Commit 705822ac1 also carries a havel_vm_gc_checkpoint safe-point (from
concurrent in-flight work staged with the same files): the tier-1 backedge
hook now offers the GC a collection point with the native frame's live
values as extra roots - without it the GC starves for the whole native
call (343k objects / 446MB peak RSS on tier_bench_loop measured with
per-iteration results, vs 11k / 72MB interpreted).

## After the backedge rework (2026-10-08, hot-path restructure + stride throttle)

Cost 3 below is fixed, and with it the C++ hot path both execution modes
share:

- C++ `recordBackedgePublic` restructure: the per-(function, ip) one-shot
  events (hot-trace hook, tier-up attempt, tier-2 site dedup) moved from
  "every past-threshold iteration" (string hash + mutex + hash-set insert
  + a fresh `std::string` per call) to a per-site cache consulted once.
  This is the shared hot path, so the INTERPRETER benefits directly - it
  never tiered anything and still paid the mutex on every iteration of a
  hot loop.
- Native backedge throttle: the lowering routes only the TAKEN backward
  arm (interpreter parity; the old pre-branch call also counted every
  fall-through, inflating tier-2 enqueues by one site on tier_bench_two_fns)
  through a per-site stride counter that calls `havel_vm_backedge_n(vm,
  ip, 64)` every 64th taken edge with an exact delta - 64x fewer bridge
  calls per native loop. Hotness, tier-up, tier-2 site dedup and yield
  requests all survive (yield latency grows by at most one stride).

Verified: 29 Rust lowering tests (two new: stride flush at edges 64/128/192
with exact deltas; do-while taken-only counting where the fall-through
exit reports nothing), cranelift_proto_driver exit 0, ctest 9/9,
module-tiering smoke exit 0 (tier1=2, tier2_enqueued=1), full smoke gate
322/325 with the 3 failures being UI-backend tests (gui/Qt/REPL) that
cannot pass in this headless config, output parity on all three benches,
ABI drift guard pass.

Measured pairs (same load, back-to-back, loadavg ~13 on the shared
machine - absolute cross-load comparisons on this box are unreliable: the
same binary and mode measured 18.2u and 51.6u at loadavg 13 vs 20, so
only paired same-conditions ratios are quoted):

| bench | interpreter (after) | tier-1 (after) | tier-1/interp |
|---|---|---|---|
| tier_bench_loop | 3.33u + 0.53s | 2.30u + 0.94s | 0.69 |
| tier_bench_two_fns | 18.25u + 0.76s | 11.60u + 2.06s | 0.64 |

Quiet-load confirmation (loadavg 1.4-1.6, two passes each, outputs
identical in both modes, tier1=3 on two_fns):

| bench | interpreter (A / B) | tier-1 (A / B) | tier-1/interp |
|---|---|---|---|
| tier_bench_calls | 0.91 / 0.88 | 0.85 / 0.88 | ~par |
| tier_bench_loop | 3.62 / 3.64 | 2.68 / 2.80 | 0.74-0.77 |
| tier_bench_two_fns | 18.94 / 22.86 | 14.24 / 13.16 | 0.57-0.75 |

Under quiet conditions the tier-1 advantage narrows to 23-25% less CPU on
loop-heavy benches (oversubscription inflates interpreter dispatch
disproportionately, so loaded ratios overstate the win). Every mode is
still faster than the 2026-10-07 post-binop records measured at load
(loop 5.33u -> 3.3u interp, 3.11u -> 2.15u tiered; two_fns 29.8u ->
18.6-22.9u interp, 18.9u -> 11.9-12.7u tiered). The interpreter-side win
from the C++ hot-path restructure is confirmed structural, not
load-flavored. The tiered sys-time rise persists under quiet load
(interp ~0.3s vs tiered 0.3-1.6s) and remains unprofiled (cost 4 below).


## After the binop branch fix (2026-10-07, call-then-select -> branch)

Cost 1 below was fixed: the arithmetic/EQ lowering now branches and only
pays the bridge call on the non-int path (verified: 27 Rust lowering tests,
driver exit 0, output parity on all three benches, tiering smoke exit 0).

| bench | interpreter | tier-1 (after fix) | delta |
|---|---|---|---|
| tier_bench_loop | 5.33u + 0.76s | 3.11u + 1.60s | ~23% less CPU (user -42%) |
| tier_bench_two_fns | 29.82u + 0.80s | 18.91u + 3.74s | ~26% less CPU (user -37%) |

User time in tiered mode dropped 22-25% versus the call-then-select
lowering. The remaining gap to theoretical is cost 2 (below) plus the sys
time increase, which grows with the fix and is not yet profiled.

## Phase C: self-hosted compiler measurement (2026-10-09)

Workload: 1927-line generated script (functions/classes/loops/interp),
run through the pure self-hosted pipeline
(`havel --run modules/lang/launcher.hv -- --run wl.hv`; fresh
`~/.cache/havel`, loadavg noted per run - this machine is shared).

| stage | quiet-ish (loadavg 1.6-2.5) | loaded (5-9) |
|---|---|---|
| parse: tokenize | 42-66 ms | - |
| parse: parseAST | ~30 s | 36-70 s |
| typecheck | 0.7-1.1 s | - |
| emit | 0.9-1.9 s | - |

parseAST is ~93% of the pipeline. Where the time is NOT:

- not the module-call wrappers' globals install: the whole run makes
  only ~8-12k wrapped calls; the goroutine pump is not involved (an ops
  probe executed through the launcher runs at the same ~0.3us/op as the
  plain fast loop); host calls total ~2 s (type() 34k calls, scope
  helpers); slow-path dispatch < 1M iterations.
- pratt.hv's own entry/exit timestamps confirm the ~39 s elapses inside
  parseTokens itself: ordinary interpreted bytecode, roughly 100M+ ops
  at fast-loop cost (~0.3us/op).

Diagnosis: the parse is ordinary Havel code paying ordinary dispatch -
and tier-1 today only removes ~9% of it because can_lower declines the
parser's hot functions (object/closure-heavy shapes). The Phase C work
is exactly what the architecture plan predicted: extend the lowering
subset to cover the parser's hot op shapes so tier-1 can compile them,
not rewrite the parser.

## Phase C: lowering the parser's blocker opcodes (2026-10-11)

Extended the tier-1 (Cranelift) lowering subset with the four opcodes
the refusal histogram showed blocking the parser's hot functions
(10 refusals -> 0; every refusal name - parseStatement, parseExpr,
parseBlock, nud, led, inferStmt, inferExpr, emitExpr, ... - now tiers):

- **DIV/MOD** route through the generic `havel_vm_binop(vm, op, l, r)`
  bridge; the adapter carries the C++ OpCode in the instruction operand
  (16/20), so the Rust lowering embeds no enum values (hclb ops 61/62).
- **NEGATE** gets a unary `havel_vm_negate(vm, v)` bridge that stages the
  operand and runs the interpreter's own `execNegate` (single-sourced
  semantics: int/double/op_negate).
- **TAIL_CALL** lowers as call-then-return: stage [callee, args...] like
  CALL, return the bridge's result. No native frame reuse - interpreter
  TCO semantics stay with the interpreter's own TAIL_CALL arm.

Parity verified end to end (interp vs --tiering, identical results):
int/float division (`/` is float division - 7/2 == 3.5), mod sign
semantics, double negation, tail-call recursion depth 20k, mixed
arith loops. Rust side: 34 cargo tests (4 new, one per opcode shape,
operand-forwarding disambiguated - MOD uses 40/6 so a mis-carried DIV
opcode cannot pass). C++ side: proto driver now asserts SET-lowering
refusal on STORE_IMMUT_VAR (MOD was the old refusal case) plus MOD
can_lower/compile acceptance.

### The write-barrier root leak (found via this work)

Tiering the parser made the self-hosted parse *slower* in CPU time
(pre-fix: tiered {147, 130, 148, 168}s vs interp {136, 129, 133}s) and
left `149019 external roots still pinned at VM shutdown` in every
tiered run (interp: none). Root cause: `havel_gc_write_barrier` -
emitted at every OBJECT_SET/ARRAY_SET/global store in tier-1 code -
pinned the stored value via `pinExternalRoot` and nothing ever
unpinned it. The parser builds its AST through OBJECT_SET, so tiering
it converted every AST field store into a permanent root: 149k registry
entries, all scanned by every subsequent GC collection, cost growing
with GC timing - which is why tiered CPU time was both higher AND
wildly variable.

Fix: the bridge now mirrors the interpreter's store-site barrier
exactly (`heap_.writeBarrierPublic(makeNull(), value)` - a marking
no-op while incremental GC is idle, via a new public GCHeap forwarder).
Post-fix tiered runs pin zero external roots. Regression smoke:
`scripts/smoke/test_issue_cranelift_write_barrier_store.hv` (30k
objects stored from tiered code under allocation pressure, all
values read back and asserted).

### Post-fix measurement state: within machine noise

Post-fix wl200 CPU-time pairs: interp {141, 157} vs tiered {131, 176};
wl40 medians interp 45.9 vs tiered 49.0. Every cluster overlaps in both
directions - on this shared 4-core box (loadavg 7-35 across the runs,
sibling full builds running), run-to-run CPU-time spread is +-8-17%,
larger than any tier1-vs-interp delta measurable right now. What IS
deterministic: the leak is gone (0 external roots vs 149k), parity
holds, and the pre-fix tiered-overhead component it explains is
eliminated. A clean quiet-machine pass over the paired rusage harness
(`scripts/benchmarks/rusage_run.py`, workload regenerable via
`scripts/benchmarks/gen_parse_workload.py`) is the way to get the final
Phase C number.

Measurement note: CPU time (rusage), not wall time, is the honest
metric here - wall time is scheduler noise at these loads. But CPU
time also inflates ~1.5-2x when the box is cache/SMT-thrashing (one
interp run measured 288s CPU in a thrash window vs its own 131-157s
cluster), so pairs must come from the same load window.


Bug found and fixed on the way (see the next commit): the interpreter's
instruction count was a large undercount for parse-shaped code. TWO
mechanisms stacked:

1. `recordInstructions(8192)` existed in only 19 of the 48 computed-goto
   re-dispatch tails in VMDispatch.cpp, so ops dispatched from the other
   29 tails (jumps, NOT, DUP/SWAP, string-cursor ops, ...) never advanced
   the profiler's instructions= figure. Fixed: every tail now runs the
   same periodic checkpoint (counter++; batch record; tick/exit/GC/yield/
   suspension checks), and runDispatchLoop's slow_path (where
   slow_dispatch_fallback redirects land) plus the non-computed-goto
   fallback loop record their own batches.
2. The dispatch counter was a per-entry local. A module-heavy run
   re-enters the dispatch loop per wrapped/closure call (thousands of
   entries per parse), and each entry's ops below one 8192 batch were
   lost on the next entry. Fixed: the counter is now a VM member
   (dispatch_instr_counter_) that accumulates across entries;
   beginFastTick() resets it where a tick budget demands a fresh count.

With honest counting (verified against a hand-computed calibration loop:
2.88M expected, 2,883,584 reported), the self-hosted parse of the 567-line
wl40 workload executes ~4.8M ops - not the ~100M+ once estimated from the
undercount. The "time looked impossible to reconcile" mystery inverted:
ops are NOT where the parse spends its time.

## Where the self-hosted parse actually spends its CPU (2026-10-11)

Phase-C diagnosis, wl40 (567 lines) through the self-hosted pipeline,
~53 s of user CPU in the parse phase (verified by /proc jiffie sampling:
utime 52.8 s, stime 2.9 s - rusage agrees; the in-container
CLOCK_THREAD/PROCESS_CPUTIME_ID reads ~2-4x inflated and must not be
used for attribution on this box):

| consumer | evidence (probe, count-based where possible) |
|---|---|
| **cross-module closure calls: ~41.5 s (~80%)** | 94,794 closure calls at 437.4 us/call average (steady-clock, max 508 ms): each identity-mismatched call copies the callee's module-globals map into the ambient (`globals = *closure_globals`, ~1600-2000 merged entries) inside doCall, and the module-wrapper epilogue move-assigns the whole ambient back. pratt<->ast<->lexer alternation defeats the identity gate on every AST-constructor/helper call. |
| module-wrapper machinery (host-fn wrappers): ~4.8 s | 6,598 wrapped calls: prologue 2.5 s (globals install + arg bind) + epilogue 1.5 s (result materialize + sidecar write-back) + dispatch 1.0 s |
| emit: containsYield O(n^2) walk | 65% of the emit window's periodic samples (110/169); `containsYield(fnDecl.body)` deep-walks the whole body AST per compiled function |
| everything else | dispatch of ~4.8M ops: ~1 s (200 ns/op, matching the calibration loops); GC: 1 collection; pump yields: 217 x ~0.5 us; fiber saves: 71; host-fn direct funnel: 0 calls |

Implications for the tiering work (the original Phase C premise):

- tier-1 compiling the parser's hot functions cannot fix the parse,
  because most of the parse's CPU is C++ map copying in the call path,
  not bytecode dispatch. That is why the lowering extensions moved the
  refusal count 10 -> 0 yet the parse stayed flat (see the 2026-10-11
  tiering section above: paired CPU runs overlap within noise).
- The two real targets, in order: (1) make the ambient-globals install
  O(1) for cross-module calls (pointer/shared-ambient or a swap-cache -
  touches GC-visible dual-write semantics, so it is its own task), and
  (2) cache or hoist containsYield (mark generator/coroutine bodies at
  parse time instead of re-walking per compiled function).
- The 9%-win tier-1 measurements on the small benches stand; they bench
  dispatch-bound loops, which is what tier-1 actually optimizes.

Also fixed on the way: intra-module wrapper calls re-copied the module
globals map (~1600-1968 merged entries, ~94us) on every call.
Identity-gated now (see commit); a module fn calling a wrapped sibling
200k times went 18.8 s -> 144 ms.

## Current state (2026-10-08 evening, after GC safe points)

Measured at loadavg ~6, fresh `~/.cache/havel` (see the measurement-
validity note below), with the iterator result-reuse change (705822ac1,
landed by a concurrent session while this work was in flight) plus the
GC work:

| bench | interpreter | tier-1 | tier-1/interp |
|---|---|---|---|
| tier_bench_calls | 0.43u + 0.43s | 0.40u + 0.35s | 0.88 |
| tier_bench_loop | 2.02u + 0.36s | 1.13u + 0.34s | 0.62 |
| tier_bench_two_fns | 10.99u + 0.41s | 5.87u + 0.43s | 0.55 |

tier-1 now costs 55-88% of interpreter CPU (was 73-87% pre-rework), the
tiered sys-time rise is GONE (two_fns: 0.43s tiered vs 0.41s interpreted
- it was the page-fault storm of the starved-GC heap balloon, see cost 1
and the 2026-10-08 sections), and a native allocating loop now runs GC
checkpoints mid-loop via the backedge safe point
(test_issue_cranelift_gc_checkpoint_loop.hv: 300k allocations collected
in-flight, RSS flat, results intact).

### Measurement-validity note (the shared-cache incident)

Every number above this section was measured with whatever bytecode
happened to be in `~/.cache/havel` - which is shared per-user across
checkouts and did not, until the pipeline fingerprint gained the binary
ELF build-id, distinguish binaries. Two concurrent sessions on this
machine (two checkouts, one $HOME) poisoned each other's entries: a
sibling binary re-stamped `tier_bench_loop`'s .hvc with bytecode its
emitter produced, and this checkout's tier-1 wedged executing it while
the interpreter ran it fine - costing hours of bisecting against source
that was not the source actually running. Consequences for the tables
above: cross-day absolute comparisons are unreliable not only because of
machine load but because the executed bytecode differed. The committed
fingerprint fix (build-id in computePipelineFingerprint) makes entries
from any other binary reject as pipeline-fingerprint-drift and recompile;
`HAVEL_STARTUP_TIMING=1` shows the rejects. Clear the cache when in doubt.

## Known costs eating the theoretical win (Phase B targets)

1. **The range iterator dominated tiered loop time** - RESOLVED
   (705822ac1, concurrent session): ITER_NEXT reuses the
   iterator's result object instead of allocating {first, second, done}
   per iteration (~2M -> ~3k allocations per tiered loop bench run;
   bench allocations now 3k, was 2M). The remaining ITER_NEXT bridge call
   per iteration is minor by comparison.
2. **Every ADD/SUB/MUL/EQ emitted an unconditional bridge call** - FIXED
   (branch-based lowering, see above).
3. **The per-iteration backedge hook** - FIXED (2026-10-08): the lowering
   emits `havel_vm_backedge_n(vm, ip, 64)` every 64th TAKEN backward edge
   per site with an exact delta, and the C++ past-threshold path no longer
   takes the mutex/string-hash/set-insert on every iteration (per-site
   one-shot event cache). See the 2026-10-08 section above.
4. **sys time rises under tiering** - RESOLVED: it was the page-fault
   storm of the starved-GC heap balloon (minor faults 13k -> 109k, peak
   RSS 72MB -> 446MB on tier_bench_loop, GC balloons of 343k objects
   between collections vs 11k interpreted - no GC checkpoint exists in
   native code). The backedge GC checkpoint + iterator result reuse close
   it: tiered sys now matches interpreted (0.43s vs 0.41s on two_fns).

Historical isolation snapshot (2026-10-07, executed against cached
bytecode - see the measurement-validity note; the 306M vs 172k
instruction ratio held, absolute times below predate the rework):

| loop shape | interpreter | tier-1 |
|---|---|---|
| while-counter (no ITER opcodes) | 1.14u | **0.34u** |
| range for-in (ITER_NEW/ITER_NEXT) | 5.67u | 3.45u |

Language-semantics note discovered while isolating: `for j in 0 .. 1000`
is INCLUSIVE of the end (verified: `for j in 0 .. 5` counts 6). The
while-counter probe therefore does 1000 iterations per call vs the for-in
bench's 1001 - a 0.1% work difference, irrelevant to the comparison, but
relevant to anyone writing benchmarks against these ranges.

## Fallback behavior (verified, by design)

`can_lower` declines functions with opcodes outside the subset (e.g. MOD, and
`__main__` in these benches because of its print/interp sites). Declined
functions stay interpreted and outputs remain correct - "a backend must never
partially compile a function". In these benchmarks 1 of 2 (calls bench) and
2 of 3 (two_fns bench) tier-up attempts compiled; the rest declined cleanly.
