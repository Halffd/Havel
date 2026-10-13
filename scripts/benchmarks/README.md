# Tier-1 (Cranelift) benchmarks

Fixed workloads for measuring the Cranelift tier-1 backend against the
interpreter. All numbers are from `build-crane-nollvm-hl` — the CMake
config with `ENABLE_CRANELIFT=ON`, `ENABLE_LLVM=OFF` ("crane" = Cranelift,
"nollvm" = no LLVM tier-2, "hl" = headless UI backend), release, headless.

## Current best (2026-10-08 evening, loadavg ~6)

| bench | interpreter | tier-1 | tier-1/interp | loadavg |
|---|---|---|---|---|
| tier_bench_calls (200k tiny calls) | 0.43u + 0.43s | 0.40u + 0.35s | 0.88 | ~6 |
| tier_bench_loop (2M inner iters) | 2.02u + 0.36s | 1.13u + 0.34s | 0.62 | ~6 |
| tier_bench_two_fns (12M inner iters) | 10.99u + 0.41s | 5.87u + 0.43s | 0.55 | ~6 |

Read: tier-1 costs 45-88% of interpreter CPU. Loop-heavy code wins
45%; call-heavy code is near parity. The 0.88 on `tier_bench_calls` is
expected, not a regression: the bodies are trivial, so per-call time is
dominated by the call machinery both modes share — the `havel_vm_call`
bridge plus `doCall` frame setup — which tier-1 does not remove. (The
Phase C findings below identify that same C++ call path, amplified by
module-globals map copying, as the self-hosted parser's dominant cost.
Any call-path optimization lands in this row first.)

These predate the 2026-10-11 dispatch-accounting fix (`a09c032f4`),
whose per-op cost is one counter bump and a masked test — not expected
to move these numbers, but they have not been re-measured since.

Compile latency: ~61ms first function (includes one-time backend init),
~1ms marginal per small function (tier_bench_two_fns debug timestamps).

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

On a loaded machine, run benches through the rusage harness instead of
`time` — it reports the child's CPU seconds directly and is the metric
all paired comparisons here are made with:

```bash
python3 scripts/benchmarks/rusage_run.py $B [--tiering] <bench>.hv --minimal
# RUSAGE: wall=<s> cpu_user=<s> cpu_sys=<s> cpu_total=<s> rc=0
```

For the self-hosted-parse workload (Phase C), the generator is
`scripts/benchmarks/gen_parse_workload.py [funcs] > wl.hv`.

## Methodology

- **Use CPU time, not wall clock.** This box is routinely oversubscribed
  (loadavg 1.4-47 observed across these sessions); wall-clock spread on
  identical runs was 5x under load. `rusage_run.py` (above) gives the
  honest `user+sys`.
- **CPU time is load-dependent too.** Under cache/SMT thrash it inflates
  ~1.5-2x (one interp run measured 288s CPU in a thrash window vs its
  own 131-157s cluster). Paired runs must come from the same load
  window; medians of 3+ pairs for any tier1-vs-interp claim.
- **The in-container `CLOCK_THREAD/PROCESS_CPUTIME_ID` reads are
  unreliable on this box (~2-4x inflated)** — do not use them for
  attribution. `/proc/<pid>/task/*/stat` jiffies and rusage agree with
  each other; use those.
- Each benchmark's printed output must be **identical** in both modes. A
  mismatch is a correctness bug in the lowering, not a performance result.
- The profiler line (`[profiler] profiler: ...`) is the execution-mode
  evidence: `tier1=N>0` means functions actually tiered up; the
  interpreted instruction count collapsing (306M -> 172k on
  tier_bench_two_fns) means the loops really ran native. Since
  `a09c032f4` the instructions= figure is verified honest (calibration
  loop: 2.88M expected, 2,883,584 reported).

## Resolved costs (all four Phase B targets)

1. **Range-iterator allocation dominated tiered loop time — RESOLVED**
   (705822ac1, concurrent session). ITER_NEXT reuses the iterator's
   result object instead of allocating {first, second, done} per
   iteration (~2M -> ~3k allocations per tiered loop bench run). The
   remaining ITER_NEXT bridge call per iteration is minor by comparison.

2. **Every ADD/SUB/MUL/EQ emitted an unconditional bridge call — FIXED**
   (2026-10-07). The arithmetic/EQ lowering now branches and only pays
   the bridge call on the non-int path. User time in tiered mode dropped
   22-25% (see the 2026-10-07 appendix table).

3. **The per-iteration backedge hook — FIXED** (2026-10-08). The lowering
   routes only the TAKEN backward arm (interpreter parity) through a
   per-site stride counter: `havel_vm_backedge_n(vm, ip, 64)` every 64th
   taken edge with an exact delta — 64x fewer bridge calls per native
   loop. The C++ past-threshold path moved from "every past-threshold
   iteration takes a mutex + string hash + hash-set insert" to a
   per-site one-shot cache — and since the interpreter shares that hot
   path, it benefits without ever tiering anything.

4. **sys time rises under tiering — RESOLVED.** Native code had no GC
   safe point, so between tier-up and function return the heap
   ballooned: 343k objects / 446MB peak RSS on tier_bench_loop vs 11k /
   72MB interpreted, minor faults 13k -> 109k. The backedge GC checkpoint
   (offered every 64 taken backward edges, native frame's live values as
   extra roots) plus the iterator result reuse close it: tiered sys now
   matches interpreted (0.43s vs 0.41s on two_fns), and a native
   allocating loop collects mid-loop with RSS flat
   (test_issue_cranelift_gc_checkpoint_loop.hv).

## Phase C: where the self-hosted parse actually spends its CPU

**Headline (2026-10-11, post-diagnosis): the parse's CPU is not
bytecode dispatch — it is C++ map copying in the module call path.**
~80% of the parse phase's ~53s of user CPU (567-line workload, verified
by /proc jiffie sampling: utime 52.8s, stime 2.9s) goes to
`globals = *closure_globals` — a full copy of the callee's
module-globals map (~1600-2000 merged entries) inside `doCall` on every
identity-mismatched cross-module closure call — plus the module-wrapper
epilogue's sidecar write-back. The parser alternates pratt<->ast<->lexer
per AST node, defeating the identity gate ~95k times per parse.

| consumer | evidence (count-based probes where possible) |
|---|---|
| **cross-module closure calls: ~41.5 s (~80%)** | 94,794 closure calls at 437.4 us/call average (steady-clock, max 508 ms) |
| module-wrapper machinery: ~4.8 s | 6,598 wrapped calls: prologue 2.5 s (globals install + arg bind) + epilogue 1.5 s (result materialize + sidecar write-back) + dispatch 1.0 s |
| emit: containsYield O(n^2) walk | 65% of the emit window's periodic samples (110/169); deep-walks each compiled function's whole body AST |
| everything else | ~4.8M ops at ~200 ns/op (~1 s, matching the calibration loops); GC: 1 collection; pump yields: 217 x ~0.5 us; fiber saves: 71 |

Implications:

- **Tier-1 cannot fix the parse.** The lowering extension moved
  refusals 10 -> 0 and the parse stayed flat within noise — correctly:
  there is almost no dispatch left to remove.
- **The small-bench wins stand** — those benches are dispatch-bound,
  which is what tier-1 optimizes. They are not a proxy for the parse.
- **Next targets, in order:** (1) make the ambient-globals install O(1)
  for cross-module calls (pointer/shared-ambient or a swap-cache —
  touches GC-visible dual-write semantics, so it is its own task);
  (2) hoist containsYield (mark generator/coroutine bodies at parse
  time instead of re-walking per compiled function). The calls bench
  (0.88 row above) is the leading indicator for (1).

### Phase C history

- **2026-10-09 — first measurement.** 1927-line generated script through
  the pure self-hosted pipeline (`havel --run modules/lang/launcher.hv --
  --run wl.hv`, fresh `~/.cache/havel`): parseAST ~30s quiet / 36-70s
  loaded, ~93% of the pipeline; tokenize 42-66ms; typecheck 0.7-1.1s;
  emit 0.9-1.9s. Initial diagnosis — "ordinary dispatch, tier-1 only
  removes ~9% because can_lower declines the parser's shapes" — was
  built on the then-unknown instruction undercount and is superseded.
- **2026-10-11 — lowering extension (commit `f51c99b8d`).** DIV/MOD via
  the generic `havel_vm_binop` bridge (C++ opcode in the operand; the
  Rust lowering embeds no enum values), NEGATE via a unary
  `havel_vm_negate` bridge that runs the interpreter's own execNegate,
  TAIL_CALL as call-then-return. Refusals 10 -> 0; parity verified on
  division/mod-sign/negation/tail-recursion shapes; cargo 34/34 (4 new
  tests, operand forwarding disambiguated — MOD uses 40/6 so a
  mis-carried DIV opcode cannot pass); proto driver refusal case moved
  to STORE_IMMUT_VAR with MOD acceptance added.
- **2026-10-11 — the write-barrier root leak (same commit).** Tiering
  the parser made it *slower* and left `149019 external roots still
  pinned at VM shutdown` in every tiered run: `havel_gc_write_barrier`
  pinned every stored value forever, so each AST OBJECT_SET became a
  permanent root scanned by every collection. The bridge now mirrors
  the interpreter's marking barrier
  (`heap_.writeBarrierPublic(makeNull(), value)`); tiered runs pin zero.
  Regression smoke:
  `scripts/smoke/test_issue_cranelift_write_barrier_store.hv`.
- **2026-10-11 — instruction-count fix (commit `a09c032f4`).** Two
  stacked undercount mechanisms fixed: 29 of 48 dispatch tails lacked
  the periodic checkpoint entirely (jumps, NOT, DUP/SWAP, string-cursor
  ops — and the slow path counted nothing at all), and the per-entry
  counter reset lost every wrapped-call entry's sub-8192 remainder.
  Counter is now a cross-entry VM member; beginFastTick resets it per
  tick. This is what inverted the Phase C diagnosis: the parse executes
  ~4.8M ops, not the ~100M+ once estimated from the undercount.
- **2026-10-11 — module-wrapper identity gate (commit `09459fadb`).**
  Intra-module wrapper calls re-copied the module globals map
  (~1600-1968 entries, ~94us) per call; identity-gated now — a module
  fn calling a wrapped sibling 200k times went 18.8s -> 144ms. The
  cross-module case remains the open target.

### Post-fix measurement state (parse, tiering on vs off)

On this shared 4-core box, paired rusage CPU runs of the wl200 workload
overlap in both directions (interp {141, 157} vs tiered {131, 176} s;
wl40 medians 45.9 vs 49.0 s) — run-to-run spread +-8-17% swamps any
delta. What is deterministic: the leak is gone (0 vs 149k external
roots), parity holds. The final Phase C number needs a clean
quiet-machine pass over the paired rusage harness.

## Phase D: the module-globals fix (2026-10-11 evening)

The Phase C diagnosis below was acted on the same day. Root cause of the
bloat: `VM::loadModule` seeded EVERY module sidecar map with the full
`host_function_globals_` table (~1,200 names) plus the caller's
namespace objects, so each module map carried ~1,600-2,000 entries and
every identity-mismatched call copied all of it. The seeding was
redundant for reads: LOAD_GLOBAL already falls back to
`host_function_globals_` (and resolveGlobalPublic mirrors that for JIT
code).

Fix (commit follows this README update):

- module sidecar maps now hold ONLY module-own names — the seeding loops
  and the inherited-globals exports filter are gone
- a lazily-built shared base layer (`VM::baseModuleGlobals_`, captured
  once from the engine-main map at first module load) carries the
  object-typed namespace entries (fs, sys, math, bit, ...); LOAD_GLOBAL
  and resolveGlobalPublic consult it AFTER the closure-sidecar step, so
  a module's own value for a shared name still wins
- the CALL_METHOD module-vs-instance classification (three interpreter
  sites + the `havel_vm_call_method` bridge) previously scanned the
  whole ambient for the receiver's id; it now goes through one shared
  `VM::isNamespaceObjectGlobal` helper that checks ambient + base +
  pushed frames — de-seeded module maps no longer hide the namespace
  objects from it, and the O(entries) ambient scans shrank with the maps
- the module loader's circular-dependency escape hatch and the
  shadowing-module lookup now search the same layers
  (`findGlobalValueByName`) instead of only the ambient

Measured (`scripts/benchmarks/module_globals_bench.hv`, N=20k per
shape, wall ms under loadavg ~10-15):

| shape | before | after |
|---|---|---|
| direct (main -> module wrapper) | 2,107 ms (~105 us/call) | 42 ms (~2 us) |
| cross (module A -> module B closure) | 4,620 ms (~231 us/call) | 61 ms (~3 us) |
| self (same-module control) | 2,055 ms (~103 us/call) | 61 ms (~3 us) |

Self-hosted parse of the 567-line wl40 workload, same machine:

| metric | before (load ~10-17) | after (load ~3-5) |
|---|---|---|
| parse: parseAST | 44-65 s | **2.3-3.1 s** |
| whole-run CPU (rusage) | 17-56 s depending on window | **3.3-3.9 s** |

Verification: full smoke gate 322/329 — the 3 known UI-backend-absent
baseline failures plus 4 `test_issue_orc_*` tiering tests that fail
identically on a clean HEAD build without this diff (proven by stash
A/B; `orc_object_get_autocall` fails deterministically at k=829 with
no tiering, so it is a pre-existing latent GC/timing bug on HEAD,
likely exposed earlier by the already-committed dispatch-accounting
change shifting allocation cadence — see the next section). Cargo 34/34,
ctest 10/10, the three Cranelift GC smokes pass interpreted and tiered.

## Known open issue surfaced by this work: the orc quartet

`test_issue_orc_{object_get_autocall, object_subscript, classnew_field,
array_hof_gc_safe}` are flaky on current HEAD: `object_get_autocall`
fails deterministically at k=829 in a plain interpreted run on a clean
tree (stash-verified), the other three pass 3/3 in quiet direct runs
and fail under gate timing. Suspected area: incremental-GC mark/sweep
interaction with the tiered inline caches or the write-barrier shape
(f51c99b8d marks only the stored VALUE where the interpreter's
OBJECT_SET site marks the container too) — allocation-cadence changes
(a09c032f4's honest instruction counting changed tier-up timing) move
the failure onset around. This is its own investigation; the globals
work is not the cause (clean-tree A/B) but the gate result above
records it.

## Measurement-validity note (the shared-cache incident)

Numbers from before 2026-10-09 were measured with whatever bytecode
happened to be in `~/.cache/havel` — shared per-user across checkouts
and, until the pipeline fingerprint gained the binary ELF build-id,
unable to distinguish binaries. Two concurrent sessions on this machine
(two checkouts, one $HOME) poisoned each other's entries: a sibling
binary re-stamped `tier_bench_loop`'s .hvc with bytecode its own emitter
produced, and this checkout's tier-1 wedged executing it while the
interpreter ran it fine — costing hours of bisecting against source
that was not the source actually running. Cross-day absolute comparisons
from that era are unreliable twice over (machine load AND differing
executed bytecode). The committed fingerprint fix (build-id in
computePipelineFingerprint) makes foreign entries reject as
pipeline-fingerprint-drift and recompile; `HAVEL_STARTUP_TIMING=1` shows
the rejects. Clear the cache when in doubt.

## VM-OPT-001: save-and-swap ambient installs (2026-10-13)

Ticket VM-OPT-001 asked to eliminate the residual O(n) map copy on
cross-module calls (the de-seeding had already removed the ~437us/call
bloat; what remained was `globals = *target` — O(module-own-names) per
install, plus the wrapper epilogue's wholesale sidecar move-assign).

Design chosen (the ticket's "save-and-swap" variant, scoped):

- `ambient_cache_` in VM.hpp, keyed by the sidecar shared_ptr
  (owner_less — keys cannot dangle), holds each module's parked ambient
  between calls. Install = O(1) swap; pop = O(1) swap back. First
  install of a module copies once.
- The cache-managed paths are doCall's closure install, the JIT callee
  install, and TAIL_CALL frame reuse. The module-fn HOST WRAPPER stays
  on the copy path and its frames are flagged `!ambient_cached`: the
  wrapper epilogue wholesale move-assigns the ambient into the sidecar
  before popping, so parking its moved-from map would poison the cache.
- GC posture: cache entries are not directly scanned, exactly like the
  existing globals_stack_ pushed snapshots; liveness flows through
  the sidecars, which STORE_GLOBAL dual-writes keep authoritative.
  Same bounded over-retention class as the pushed frames.
- popGlobals parks under the identity CURRENT at pop time (TAIL_CALL
  chains may swap several modules through one stack frame), and only
  on the normal restore path — the fiber-restored/foreign-pop early
  returns are engine-main lineage (null identity), never cache-managed.

Found and fixed on the way (pre-existing, reproduced on a clean tree):
a Havel `throw` unwinding past a module wrapper transferred to the
outer try handler via handleScriptThrow, after which the wrapper's
completion path popStack()ed a result that was never pushed — stealing
an operand from the caller's catch block and corrupting every stack
access after it (surfaced as "Stack underflow" at the caller's
TRY_EXIT; minimal repro: `try { wrappedModuleFnThatThrows() } catch`).
Both wrappers ($module_fn_ and $module_closure_) now detect the
transfer (no result at or below the entry stack depth) and restore
their context + suppress the CALL-site result push via the existing
budget_unwind_no_result_ mechanism.

Measured (scripts/benchmarks/bigmap_bench.hv — 20k alternating
cross-module calls into a module with 400 own globals; rusage CPU,
3-run medians, paired stash A/B; post-swap runs taken under HIGHER
load than pre-swap, so the comparison is conservative):

| shape | pre-swap | post-swap |
|---|---|---|
| total CPU | 4.27 / 4.27 / 4.29 s | 2.19 / 2.30 / 1.60 s |
| user CPU | ~1.17 s | ~0.72 s |
| sys CPU  | ~3.09 s | ~1.42 s |

~2x total CPU; the sys collapse is the copy's node-allocation churn
(400-entry unordered_map copied per call -> 20k copies) disappearing.
On the tiny-map fixture (module_globals_bench, ~5-entry modules) the
win is small — the residual per-call cost there is frame machinery,
not map size; the O(n) term is what this change removes.

Correctness pinned by scripts/smoke/test_vm_opt_001_ambient_globals_swap.hv
(alternating writes, nested recursion, closure-created-in-module
observing state, exception mid-call, tail chains, GC pressure between
installs). Also on file: importing the same module under two path
spellings ("modglob/x" vs "x") loads two module instances with
separate sidecars — pre-existing loader behavior, worked around in
the fixtures; a fix belongs to the module loader, not this change.

## Module identity by path spelling, fixed (2026-10-13)

Found during VM-OPT-001 (worked around in its fixtures): importing the
same module file under two path spellings ("modglob/x" from a driver
vs "x" from a sibling module) loaded TWO module instances with separate
sidecars - silent state split-brain. Root cause: ModuleLoader::
canonicalizePath delegated to resolve(), whose cache-hit path returns
an EMPTY canonicalPath, so loadModule fell back to the RAW import
string as its cache key. canonicalizePath now uses
computeCanonicalPathNoCache (source-tree identity, spelling-stable).

Two more latent bugs the fix exposed (both now fixed):

1. **Warm .hvc loads resolved a module body's imports against the
   cache directory.** makeBcCache reports the .hvc path as the
   resolution's canonicalPath, and loadModule set current_script_dir_
   from it - so a module served from ~/.cache/havel executed its
   `use "x"` statements with the cache dir as base. Stdlib survived
   by accident (bare names resolve via lang.*/std.* prefixes inside
   the cache dir); user modules with sibling imports died with
   "Module not found" the moment their .hvc was warm. loadModule now
   prefers ResolvedModule::sourcePath for the script dir (lang/stdlib
   modules keep the old behavior - their cache copies sit in the same
   dir - and source-less bundles fall back to the .hvc path).

2. **The circular-dependency escape hatch rejected the merged
   namespace object.** With spelling-stable keys, the window <->
   protocols <- display cycle is detected correctly - and the escape
   then failed: its predicate (isHostFnNamespaceObject) requires every
   entry to be a host function, but the in-flight display namespace
   has already been shadow-merged with .hv exports (closures) and a
   nested namespace object (x11). The escape now uses a dedicated
   isCallableNamespaceObject predicate (callables + nested namespace
   objects, at least one callable required - arbitrary data objects
   are still rejected).

Verified: module-identity + vm-opt-001 smokes pass cold AND warm
(warm is the previously broken path); window_list, window_active,
hk_trigger_burst/e2e (the regressions the fix exposed mid-gate) all
pass; full gate 345/6 = exactly the pre-existing set (orc quartet +
repl + qt); ctest 12/12; both module benches intact.

## ORC vs Cranelift at tier 2 (2026-10-12, first-ever measurement)

Matched workloads on the LLVM-enabled build (`build-crane-release`, mode
19), now unblocked by the ORC LLVM-23 fix. Tier-1 fires at 15k hotness on
both builds so the comparison starts from the same point; the LLVM build
also queues ORC O2 tier-2 at 10k (nollvm's "tier 2" is a Cranelift
recompile - its optimizing backend is absent).

| bench | Cranelift tier-1 (nollvm) | + ORC O2 tier-2 (LLVM) |
|---|---|---|
| tier_bench_two_fns (3 runs) | 12.7 / 13.8 / 14.1 s | 13.0 / 13.7 / 14.3 s |
| tier_bench_loop (3 runs) | 3.2 / 3.7 / 3.1 s | 3.1 / 3.1 / 2.9 s |

Medians overlap: **ORC's O2 pipeline buys nothing measurable over
Cranelift tier-1 on these shapes** (tiny integer-arithmetic loop bodies
where Cranelift's int fast path is already near-native). Compile latency
is also a wash at this function size: queued->compiled timestamps are
sub-millisecond for both.

Caveats, honestly: these are synthetic benches of ~20-instruction
functions. ORC's real strengths (cross-function inlining, vectorization,
scheduling on larger bodies) are unexercised. What the measurement DOES
establish: for the dispatch-bound hot-loop profile tiering actually sees
in this codebase, tier-2 ORC does not earn its complexity as a default;
keep Cranelift the default tier-1 and ORC opt-in.

Also fixed on the way: tier-2 queueing was dead wiring. The enqueue sat
inside maybeTierUp's one-shot tier-1 guard, so a function could only ever
queue if its FIRST hot event had already crossed tier2_threshold - the
20k-invocation module-globals bench queued two functions by crossing both
thresholds at once; a 3k-invocation bench queued none. The tier-2 check
now re-runs on every hot event with its own dedup sets (steady-state
tier-2 works; measured with the threshold trick above because the bench
invocation counts sit below the default 10k).

## Fallback behavior (verified, by design)

`can_lower` declines functions with opcodes outside the subset (e.g.
STORE_IMMUT_VAR, collection literals) or with malformed jump targets.
Declined functions stay interpreted and outputs remain correct — "a
backend must never partially compile a function". In these benchmarks
1 of 2 (calls bench) and 2 of 3 (two_fns bench) tier-up attempts
compiled; the rest declined cleanly.

**Leading indicator for lowering-subset work:** the refusal histogram.
`HAVEL_LOG_LEVEL=debug HAVEL_CRANELIFT_TRACE=1 $B --tiering <script>` and
grep `can_lower refused` — it names exactly which functions are blocked
and by which opcode. The Phase C extension was driven by this histogram
(10 named parser functions on TAIL_CALL/NEGATE/DIV -> 0 after).

## Appendix: historical measurement epochs

Each table is accurate to its date and conditions. Load on this box
changed the absolute values between epochs; only same-epoch paired
ratios are comparable. All pre-2026-10-09 rows additionally carry the
shared-cache caveat above.

### 2026-10-08 evening (loadavg ~6) — superseded the table at the top
by nothing yet; this is the current-best epoch. Kept here for the
interpreter-side before/after:

| bench | interpreter (before iterator reuse) | interpreter (after) | allocations |
|---|---|---|---|
| tier_bench_loop | 3.6-4.1u | 2.18u | ~2M -> 2231 |
| tier_bench_two_fns | 18.9-22.9u | 12.16u | ~6M -> 6236 |

| bench | tier-1 (before) | tier-1 (after) | tier-1/interp |
|---|---|---|---|
| tier_bench_loop | 2.30-2.80u | 1.10u | 0.50 |
| tier_bench_two_fns | 11.6-14.4u | 6.88u | 0.57 |
| tier_while probe | 0.18u | 0.07u | 0.09 |

Verified with: iteration semantics identical (interpreter == tiered) for
arrays, objects, strings, sets, nested loops and value capture; range
inclusivity unchanged; full smoke gate of that date 323 passed / 3
failed (the 3 known UI-backend tests) / 0 skipped; proto driver exit 0.

### 2026-10-08 backedge rework (loaded, loadavg ~13, paired back-to-back)

The same binary and mode measured 18.2u and 51.6u at loadavg 13 vs 20 —
only paired same-conditions ratios are quoted.

| bench | interpreter | tier-1 | tier-1/interp |
|---|---|---|---|
| tier_bench_loop | 3.33u + 0.53s | 2.30u + 0.94s | 0.69 |
| tier_bench_two_fns | 18.25u + 0.76s | 11.60u + 2.06s | 0.64 |

Quiet-load confirmation (loadavg 1.4-1.6, two passes each, outputs
identical, tier1=3 on two_fns):

| bench | interpreter (A / B) | tier-1 (A / B) | tier-1/interp |
|---|---|---|---|
| tier_bench_calls | 0.91 / 0.88 | 0.85 / 0.88 | on par |
| tier_bench_loop | 3.62 / 3.64 | 2.68 / 2.80 | 0.74-0.77 |
| tier_bench_two_fns | 18.94 / 22.86 | 14.24 / 13.16 | 0.57-0.75 |

Under quiet conditions the tier-1 advantage narrows to 23-25% less CPU
on loop-heavy benches (oversubscription inflates interpreter dispatch
disproportionately, so loaded ratios overstate the win).

### 2026-10-07 post-binop-fix (loaded; cross-load absolutes unreliable)

| bench | interpreter | tier-1 | delta |
|---|---|---|---|
| tier_bench_loop | 5.33u + 0.76s | 3.11u + 1.60s | ~23% less CPU (user -42%) |
| tier_bench_two_fns | 29.82u + 0.80s | 18.91u + 3.74s | ~26% less CPU (user -37%) |

Loop-shape isolation of the same epoch (executed against cached
bytecode — see the validity note; the 306M vs 172k instruction ratio
held, absolute times predate the rework):

| loop shape | interpreter | tier-1 |
|---|---|---|
| while-counter (no ITER opcodes) | 1.14u | **0.34u** |
| range for-in (ITER_NEW/ITER_NEXT) | 5.67u | 3.45u |

Language-semantics note discovered while isolating: `for j in 0 .. 1000`
is INCLUSIVE of the end (verified: `for j in 0 .. 5` counts 6). The
while-counter probe therefore does 1000 iterations per call vs the
for-in bench's 1001 — a 0.1% work difference, irrelevant to the
comparison, but relevant to anyone writing benchmarks against these
ranges.

### Earliest baseline (pre-rework; kept for the record only)

| bench | interpreter | tier-1 | delta |
|---|---|---|---|
| tier_bench_calls | 2.37 wall, on par | 2.23 wall | on par |
| tier_bench_loop | 5.72u + 0.52s | 3.99u + 1.16s | ~18% less CPU |
| tier_bench_two_fns | 34.16u + 0.87s | 25.21u + 2.99s | ~20% less CPU |

Iteration-semantics note from the iterator-reuse change: the iterator's
{first, second, done} object is allocated once per ITER_NEW and
overwritten per step; field writes go through set() so the OBJECT_GET
inline cache re-resolves (with operator[] the cached done=false was
served forever and the compiled for-in loop never exited).
