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

