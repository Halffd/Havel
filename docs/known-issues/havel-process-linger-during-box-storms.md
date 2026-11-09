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

- Do not debug this inside havel. Re-run gates only in a quiet window
  (load-gated runners did `load<3`, but the load average misses IO-blocked
  D-state load - gate on `ps -eo stat | grep -c '^D'` == 0 instead).
- Optional harness help: make `hvtest` report per-test wall time in the
  summary so a storm is visible without log archaeology, and treat
  "first output never arrived" timeouts distinctly from real hangs.
