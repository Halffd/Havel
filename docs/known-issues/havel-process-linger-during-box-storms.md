# havel processes linger (or never reach output) during host IO storms - smoke runs collapse into mass timeouts

**Status:** OPEN, environmental phenomenon with an unverified cause. NOT tied
to havel-2's extern commit 9fb44bdee: the control binary on main
(/host/havel/build-debug/havel) shows the same behavior.

## What was observed (Nov 2, evidence)

Trivial script (`print("hi"); exit(0)`):

- `havel p_ok.hv` (file-redirect) on main's binary: **did not finish in 30s**
  (killed; 34972 ms measured). Same script on havel-2's binary: sometimes
  finishes in ~1s, sometimes times out - flaky in both directions.
- Under a pipe (`havel p_ok.hv | cat`): inside one controlled `sh -c` run,
  `ps` at t+13s showed **the havel process itself still alive in state S**
  with `cat` waiting on it and **0 bytes produced** - it had not reached its
  first print. The process had to be killed.
- Both binaries affected; the box at the time had another session's
  `havel-wm` processes spinning (two in D state, load 8-26) plus repeated
  load spikes from concurrent builds.

## Why it matters (the damage it caused)

Two full-suite smoke runs on the same commit were destroyed by this:

| run | result | tests/sec avg |
|-----|--------|---------------|
| deferred_gates (load-gate 7.5) | 167 passed, 177 failed | 39.8 s/test |
| deferred_gates2 (load-gate 4.7) | 248 passed, 96 failed | 23.5 s/test |

95-96 of the failures were labelled `(timeout)` - including sub-second tests
(arithmetic_*, string_*). In a calm window earlier the same suite ran
342/342 at 5.2 s/test. So the failures measured the host, not the code; but
each ruined run costs ~2-3 hours.

## Observed mechanism (Nov 3, /proc + strace)

- During a hang, the havel process is `State: D (disk sleep)`,
  `wchan: folio_wait_bit_common`, 1 thread, fds normal. That wchan is a
  **page-fault wait on filesystem IO** - the process is not deadlocked, it
  is waiting for a page to arrive from storage.
- `strace -f -e trace=openat,read,mmap` of a hung run: startup proceeds
  normally (libraries open/mmap with results), then **no further syscalls
  for the rest of the timeout** - consistent with waiting on mapped-page
  faults (which produce no syscall entries).
- A fully LOCAL copy (binary + modules under /tmp, local disk) hangs the
  same way, so it is not specific to /host's virtiofs.
- Context at the time: `/host` is a 953G share at **98% full** under heavy
  concurrent writes from another session (its `havel-wm` processes sat in D
  state), and the VM's own disk is `/dev/vda2`, 76% full on the same host.
  Both /host and the VM disk back onto the same physical host - when the
  host's storage queues up, every page-in in the VM (from ANY mount) can
  stall for tens of seconds.

## Conclusion (mechanism observed; no havel-side defect identified)

The linger is **host storage congestion**, not a havel bug: havel waits on
page-ins like any process; under the congestion its startup/first-output
exceeds any downstream timeout, which the smoke runner reports as
`(timeout)`. `HAVEL_HEADLESS=1` is irrelevant (no external service is
involved).

## Next step

- Do not debug this inside havel. Re-run gates only in a quiet window.
  The usable signal is the STARTUP LATENCY of a havel process, measured
  directly - see "The gate condition is startup latency, not the load
  average" below. The load average misses IO-blocked D-state load
  entirely, and `ps -eo stat | grep -c '^D'` == 0 is only a snapshot.
- Optional harness help: make `hvtest` report per-test wall time in the
  summary so a storm is visible without log archaeology, and treat
  "first output never arrived" timeouts distinctly from real hangs.

## Recurrence: Nov 11 2026 (havel-3, post-reboot)

The storm recurred in the same shape, and this time the CONTROL binary was
measured in the same window as the tree under test, which pins the cause
outside havel-3:

- The box rebooted at 20:12 while the gate was running, so the gate never
  completed. Its log shows the familiar pattern - every failure is
  `(timeout)`, and the failures include sub-second tests
  (`arithmetic_mod`, `arithmetic_add`, `array_find_indexOf`), which cannot
  time out for any reason intrinsic to their code.
- `ps -eo stat | grep -c '^D'` was **19**. The D-state set included `ld`
  (the dynamic loader) and `havel-wm`, alongside `btrfs-transaction`,
  `systemd-journal` and btrfs flush kworkers. A process stuck in `ld` is
  stuck before `main`, so no havel code is even reached.
- `/host/havel/build-debug/havel --version` - **main's binary, which carries
  none of the changes under test** - timed out at 60s (rc=124) and again at
  180s (rc=124). havel-3's binary, under the same load, did eventually
  start and print `commit e53de05874ff (havel-3, dirty)` (rc=0), but only
  after ~245s.
- Load average was 22-34 with 11 users on the box.

Conclusion unchanged and now doubly confirmed: these `(timeout)` failures
measure the host, not the code. A gate started into such a window will
report mass timeouts that have nothing to do with the tree under test.

Practical note for the next session: the gate has to be deferred, not
retried in place. The runner used here waits for `D-state == 0` AND
`load < 3` before invoking the gate at all, and refuses to start the gate
after a 6h budget instead of burning a doomed 2-3h run.

### The gate condition is startup latency, not the load average

The `D-state == 0 AND load < 3` condition proved unusable on this box and
was replaced by a direct measurement. Two samples of the SAME box, minutes
apart, while both conditions looked acceptable:

| condition sample | D-state | load | actual `havel --version` |
|------------------|---------|------|--------------------------|
| 1 | 0 | 12 | 21706 ms |
| 2 | 3 | 10 | 92873 ms |

So the proxies said "quiet" while a havel process took 93 seconds to
start. The load average does not count IO-blocked load at all, and the
D-state count is a snapshot of a condition that comes and goes.

What the smoke runner actually measures is whether a test process reaches
its first output before its per-test timeout (`hvtest --timeout`, default
30 s, the gate uses 300 s). If startup alone costs 93 s, every test times
out regardless of the code under test. So the gate condition is now the
STARTUP LATENCY of a havel process, measured directly, with the D-state
count kept only as a diagnostic in the log.

### Control measurement on the JIT-parity gate (Nov 12 2026)

`scripts/ci/jit-parity-test.sh` reported `7 passed, 3 failed`, and all
three failures were `interpreted run hung (> 300s)` - never a tiered
hang, never a tier1=0, never a parity mismatch. The failing scripts were
`for_loop_break`, `loop_with_break` and `selfhost`.

The "interpreted run" is the REFERENCE run - the plain interpreter with no
JIT flags. A control settled what that meant:

| binary | tree | JIT backend | result on `for_loop_break` |
|--------|------|-------------|---------------------------|
| `/host/havel-3/build-crane-nollvm/havel` | havel-3 (merged) | Cranelift | rc=124 after 305906 ms |
| `/host/havel-3/build-debug/havel` | havel-3 (merged) | **none** | rc=124 after 281932 ms |
| `/host/havel/build-debug/havel` | **main** | **none** | rc=124 after 280005 ms |

Main's binary - a different tree, no JIT, untouched by havel-3 - hangs
identically on the same script. The script prints nothing unless it
FAILS, so a hung run produces no output at all, which is consistent with
a stall before the script body rather than with a defect in it.

Load during the measurement was 53-80, and `ps` showed four `cpuhp/4`,
`cpuhp/8`, `cpuhp/10`, `cpuhp/12` processes pinned at ~40% CPU each for
an hour - a deliberate CPU stress test from another session, saturating
the box. Every script in that window was going to time out.

Conclusion: a parity FAIL whose message is `interpreted run hung` is a
host-storm report, not a JIT finding. Re-run it in a window where the
startup probe is fast; only a failure in the TIERED run, or a tier1=0,
or an output mismatch is a JIT result.

## Second, code-level contributor: the shared cache lock convoy (Nov 12 2026)

The doc above attributes the mass timeouts to host storage congestion.
That attribution is INCOMPLETE. A separate, reproducible, code-level
contributor was measured on this box on 2026-11-12: **launching against
the SHARED `~/.cache/havel` is several times slower than launching against
a private cache, and the difference is LOCK WAIT, not IO.**

### The measurement

Same binary, same script (`print("hi")`), same tree, back to back:

| cache | wall | longest blocking waits |
|-------|------|-------------------------|
| shared `~/.cache/havel` | 14234 ms, 14784 ms | 4 x ~25 s (futex) |
| private `XDG_CACHE_HOME=/tmp/...` | 9544 ms, 3506 ms, 3512 ms | 4 x ~5.3 s (futex) |

The difference is NOT attributable to any of the usual suspects - all of
these were measured and are equal between the two runs:

| quantity | shared | private |
|----------|--------|---------|
| total syscalls | 1299 | 1288 |
| module `.hv` opens | 270 | 268 |
| `openat` of module sources | identical set | identical set |
| cache rejections | 1 | 1 |

`--version` costs ~350 ms in both cases and `havel --lint` ~2.5 s, so the
cost is in the EXECUTE phase, not in process startup or in compilation.
`time` on the trivial script gives `real 31 s / user 12.2 s / sys 2.4 s`:
~16 s of the wall time is the process NOT running.

### What the process waits on

`strace -f -T -tt -e trace=futex,...` shows four worker threads plus one
waiter blocked in `futex` for the whole duration of the stall, and the
stall length tracks the cache choice exactly (25.2 s shared, 5.3 s
private). These are the same four threads that queue on the per-entry
`flock` used by the bytecode-cache writer (`autoCacheBytecodeChunk`,
`RuntimeSupport.hpp`: `flock(lockFd, LOCK_EX)` around the write+rename).

The shared cache is written by EVERY checkout on the box - `havel`,
`havel-2`, `havel-3`, `havel-stable` all use `~/.cache/havel` - so a
launch can queue behind another tree's cache rewrite. The smoke gate
launches one process per test, so a 390-file suite pays the convoy
hundreds of times, and each queued launch eats the per-test timeout
budget. **This is present even on a quiet box**, which is why a gate that
starts calm can still collapse.

### The specific fight: the four pipeline-fingerprint inputs

The four self-hosted-compiler modules (`lang.emitter`, `lang.pratt`,
`lang.lexer`, `lang.scope`) are deliberately NOT content-keyed - their
`.hvc` files ARE the pipeline fingerprint's inputs (see
`shared-bytecode-cache-cross-worktree-thrash.md`, "The fingerprint inputs
still thrash"). Their embedded source paths show who owns them:

    lang.emitter   /host/havel/modules      <- main owns it
    lang.pratt     /host/havel/modules
    lang.lexer     /host/havel/modules
    lang.scope     /host/havel/modules

so havel-3's launches fight main's launches over these four files on
every run. Deleting the four entries stops the drift for the binary that
recreates them first (measured: 7672 ms -> 7396 ms -> 7127 ms on
successive launches of the same script).

### Practical consequence for gating

`XDG_CACHE_HOME` points the whole binary at a scratch cache
(`ModuleLoader::getDefaultCacheDir` honours it) and removes the convoy,
but it is NOT a usable gate environment on its own: a cache outside
`$HOME/.cache/havel` makes `test_python_bridge` fail under
AddressSanitizer with a 2289-byte CPython leak (see
`python-bridge-cpython-module-leak.md`).

So the shared-cache convoy cannot be gated around by redirecting the
cache. The fix has to be per-tree fingerprinting (the residue the
content-keyed cache fix deliberately left behind), or a per-tree cache
directory by default. Until then, a gate on this box measures the convoy,
not the tree.

### Update (2026-11-16): the private-cache workaround is usable again

The blocker named above - "a cache outside `$HOME/.cache/havel` makes
`test_python_bridge` fail under AddressSanitizer with a 2289-byte
CPython leak" - is **gone**. The leak was in the python bridge's own
teardown (the module/builtin-function reference cycle and the reference
`PyImport_ImportModule()` returned), not in the self-hosted pipeline it
was blamed on; it is fixed in `src/modules/python/PythonModule.cpp` and
recorded in `python-bridge-cpython-module-leak.md`.

The doc's own minimal reproducer now passes with a non-default cache:

    D=$(mktemp -d)
    XDG_CACHE_HOME=$D /host/havel-3/build-debug/havel \
        --run --self-hosted-path out --minimal \
        scripts/smoke/test_python_bridge.hv
    # rc=0, 16 PASS lines, no AddressSanitizer report

So a gate can now point `XDG_CACHE_HOME` at a scratch directory and
avoid the box-wide convoy. Measured on this box, the same binary and
the same trivial script (`print("x")`):

| cache | wall |
|-------|------|
| shared `~/.cache/havel` | 11.85 s - 27.45 s |
| private `XDG_CACHE_HOME=/tmp/...` (cold, first launch) | 47.72 s |
| private `XDG_CACHE_HOME=/tmp/...` (warm) | **1.45 s - 1.75 s** |

The private cache is warm after the first launch, and the warm path is
roughly **8-15x faster** than the shared one - which is the difference
between a smoke test that starts inside its per-test budget and one that
does not.

Note that the cold first launch is SLOWER (47.72 s): every module is
compiled from source and written to the fresh cache. A gate should
therefore point `XDG_CACHE_HOME` at a **persistent** scratch directory
for the tree, not at a `mktemp -d` that is discarded between runs.

This does NOT fix the convoy itself - the shared cache is still fought
over by four checkouts. It makes a gate measurable on this box.


### Distinct from the serve-path defect closed on Nov 13

The convoy above is about LOCK WAIT on entries that are written
successfully. A separate defect, closed on 2026-11-13, made the
incremental serve path reject the entry it had just written - so the
cache never served and every launch rewrote the entry (see
`shared-bytecode-cache-cross-worktree-thrash.md`, "Fixed defect (Nov 13
2026)"). The two compound: an entry that cannot be served is rewritten,
and a rewritten entry queues on the lock. Closing the serve-path defect
removes the rewrite; it does not remove the cross-tree fingerprint fight
over the four pipeline-fingerprint inputs.

