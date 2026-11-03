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

## Hypotheses (unverified, in order)

1. Host-level IO starvation on the shared /host mount (D-state clang/cargo/
   havel-wm seen repeatedly) stalls havel's startup before its first output;
   anything with a pipe + timeout downstream then reports a timeout.
2. A startup-init wait that polls an external service (IO devices, X11,
   dbus, at-spi) with no timeout - slow/hung during the storms.
   `HAVEL_HEADLESS=1` was set for all the runs above, so headless did not
   prevent it.

## Next step

Reproduce on a quiet box with `strace -f -e trace=openat,connect,read,poll`
(or gdb attach during the hang's S state) and capture the startup wait.
Also add per-run timing instrumentation to `hvtest` (report wall time per
test) so a future storm is obvious from the summary instead of requiring
log archaeology.
