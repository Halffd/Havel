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

## After the branch fix (2026-10-07, binop call-then-select -> branch)

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
3. **The per-iteration backedge hook** (`havel_vm_backedge` per taken
   backward edge: map increment + two atomics + C call) is ~85ns/call in
   the tiered while-counter probe (4M calls in 0.34s user) - a minor cost
   for range-loop code (ITER dominates) but a real one for while-style
   loops. A batched form (`havel_vm_backedge_n(vm, ip, stride)`) exists on
   the C++ side but is not yet wired into the lowering.
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
