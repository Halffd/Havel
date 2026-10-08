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

## Known costs eating the theoretical win (Phase B targets)

The interpreter executes ~1780x more dispatch work on tier_bench_two_fns
(306M vs 172k instructions) yet tier-1 only saves ~20% CPU. Measured
isolation (tier-while probe: same loop as tier_bench_loop but a manual
while-counter instead of `for j in 0 .. 1000`):

| loop shape | interpreter | tier-1 |
|---|---|---|
| while-counter (no ITER opcodes) | 1.14u | **0.34u** |
| range for-in (ITER_NEW/ITER_NEXT) | 5.67u | 3.45u |

1. **The range iterator dominates tiered loop time**: identical work,
   tiered while-counter 0.34s user vs tiered for-in 3.45s - 10x. The
   iterator allocates per iteration (tiered for-in run: ~2M heap
   allocations; tiered while-counter: ~3k) and crosses a bridge call per
   ITER_NEXT. Fixing ITER_NEXT's per-iteration allocation/bridge is the
   biggest remaining win for range-loop code.
2. **Every ADD/SUB/MUL/EQ emitted an unconditional bridge call** - FIXED
   (branch-based lowering, see above).
3. **The per-iteration backedge hook** - FIXED (2026-10-08): the lowering
   emits `havel_vm_backedge_n(vm, ip, 64)` every 64th TAKEN backward edge
   per site with an exact delta, and the C++ past-threshold path no longer
   takes the mutex/string-hash/set-insert on every iteration (per-site
   one-shot event cache). See the 2026-10-08 section above.
4. sys time rises under tiering (0.76s -> 1.6s); source not yet profiled.

Language-semantics note discovered while isolating: `for j in 0 .. 1000`
is INCLUSIVE of the end (verified: `for j in 0 .. 5` counts 6). The
while-counter probe therefore does 1000 iterations per call vs the for-in
bench's 1001 - a 0.1% work difference, irrelevant to the comparison, but
relevant to anyone writing benchmarks against these ranges.

None of these were changed during the verification phase; they are recorded
so the next change has a before/after to diff against.

## Fallback behavior (verified, by design)

`can_lower` declines functions with opcodes outside the subset (e.g. MOD, and
`__main__` in these benches because of its print/interp sites). Declined
functions stay interpreted and outputs remain correct - "a backend must never
partially compile a function". In these benchmarks 1 of 2 (calls bench) and
2 of 3 (two_fns bench) tier-up attempts compiled; the rest declined cleanly.
