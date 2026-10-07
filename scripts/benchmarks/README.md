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

## Known costs eating the theoretical win (Phase B targets)

The interpreter executes ~1780x more dispatch work on tier_bench_two_fns
(306M vs 172k instructions) yet tier-1 only saves ~20% CPU. Identified in the
lowering (cranelift-backend/src/lib.rs), confirmed by the sys-time increase:

1. **Every ADD/SUB/MUL/EQ emits an unconditional bridge call**
   (`havel_vm_add(vm, l, r)`), then `select`s between the bridge result and
   the inline int-48 result. The call executes even when both operands are
   ints and its result is discarded. Branch around the call instead of
   selecting after it.
2. **Every backward JUMP calls `havel_vm_backedge`** unthrottled; the C++
   side (`recordBackedgePublic`) does an `unordered_map[ip]` increment plus
   two atomic RMWs per iteration. Needs a counter-in-native-code scheme or a
   throttled re-check.
3. sys time rises 0.9s -> 3.0s on the big bench; source not yet profiled
   (suspect JIT page permissions / mmap churn).

None of these were changed during the verification phase; they are recorded
so the next change has a before/after to diff against.

## Fallback behavior (verified, by design)

`can_lower` declines functions with opcodes outside the subset (e.g. MOD, and
`__main__` in these benches because of its print/interp sites). Declined
functions stay interpreted and outputs remain correct - "a backend must never
partially compile a function". In these benchmarks 1 of 2 (calls bench) and
2 of 3 (two_fns bench) tier-up attempts compiled; the rest declined cleanly.
