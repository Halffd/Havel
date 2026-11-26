# hvtest counts a script that SKIPs (exit 0 without a PASS line) as passed

**Status:** OPEN (behavior verified 2026-11-26; no change made yet)

## Symptom

A smoke test whose header decides it cannot run on the current machine
prints `SKIP ...` and exits 0. `hvtest` then counts the file as passed:

```text
smoke: 399 passed, 0 failed, 0 skipped | 399 files
```

The skipped work is invisible in the headline. This bit for real: the
hpkg toolchain store test has a top-of-file guard
(`git --version` -> `SKIP: no git on this machine`), and the chroot build
environment has no git - so for months that test contributed a
silently-skipped "pass" to every gate run in that environment while its
subcommands (install/use/run) were never executed. The skip was only
discovered when a mutation test "passed" on a build where the tested code
was deliberately broken.

## Root cause

`hvtest` decides pass/fail per file from the child process exit code. A
SKIP exits 0 on purpose (foreign machines must stay green), so
"skipped" and "passed" are indistinguishable in the summary. The scripts
cannot print a machine-readable skip marker that hvtest would report,
because there is no such protocol - the `// smoke: env =` header keys
shape inputs, not verdicts.

## Evidence

- `scripts/smoke/test_hpkg_toolchain_store.hv` lines ~50-56: the git guard.
- Chroot masterdir `git --version` -> `command not found` (fixed locally by
  `xbps-install -r <masterdir> git`, 2026-11-26).
- Same pattern in every test that guards on environment: qt toolkits,
  DISPLAY, network.

## Impact

Any environment-missing test silently stops covering its feature in that
environment while every gate summary stays green. A regression in the
covered code lands unnoticed for as long as the guard holds - which can be
forever, since nothing records "this test has not actually run here".

## Next step

Pick one:

1. hvtest gains a verdict protocol: a script prints `SKIP: <reason>` (or
   `// smoke: allow-skip` header) and hvtest counts it in a real
   `skipped:` bucket; gates then either fail on unexpected skips or print
   them prominently.
2. Gate environments are pinned: CI installs git (done for the chroot
   masterdir on this box), and the skip guards stay as foreign-machine
   courtesy only.

Until either lands, treat a green gate in a fresh environment as
"everything that could run passed" - check the raw output for `SKIP`
before trusting coverage claims.
