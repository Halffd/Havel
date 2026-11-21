# The binary's `commit <sha>` stamp is captured at CMake configure time only

**Status:** OPEN. Symptom resolved once by hand (reconfigure); the root cause
is not fixed. This documents the root cause, the evidence, and the concrete
next step.

## Symptom

`havel --version` prints the commit the binary was built from. After an
incremental `ninja` build that follows new commits, it keeps printing the
commit CMake was last *configured* at, while the compiled code is current.

Observed 2026-11-21 while validating the smoke gate:

```
$ /builddir/havel-dev/havel --version
commit 9ad6a2fe2808856... (main, clean)

$ git -C /home/void/Havel rev-parse HEAD
90023b15768833bc2dc976d62fd138c13dafee34     # 4 commits later
```

The binary demonstrably contained the newer code — `translateShellLine` was
exported from that binary (it is declared `HAVEL_EXPORT` in a commit after
`9ad6a2fe`), and the `ProcessManager::isProcessAlive` zombie fix from the same
later commit changed its behaviour. Only the stamp was stale.

## Root cause

`CMakeLists.txt` lines 16-53 compute `HAVEL_BUILD_COMMIT`, `HAVEL_BUILD_BRANCH`
and `HAVEL_BUILD_DIRTY` with `execute_process(git ...)` at **configure** time,
and line 1969 turns them into compile definitions on `havel_core`:

```cmake
HAVEL_BUILD_COMMIT=\"${HAVEL_BUILD_COMMIT}\"
```

Nothing re-derives them on build. `ninja` reuses the cached `CMakeCache.txt`
entry until someone re-runs `cmake`. Measured evidence: the cache file was
last written `2026-11-15 08:25` while the binary under test was linked days
later, and the compiled-in macro in
`/builddir/havel-dev/havel_core_autogen/moc_predefs.h` held the Nov 15 sha.

## Why it matters

The repo's provenance rule (`AGENTS.md`, "Always name the exact binary path")
is that every result names the binary's *self-reported* commit, and
`scripts/which-havel.sh` implements exactly that by probing `--version`. A
stale stamp reverses the usual trap: instead of an old binary claiming to be
new, a **new** binary claims to be old. A gate run reported against
`commit 9ad6a2fe` is not evidence for the code at HEAD, and a reader cannot
tell the difference from the output.

`scripts/which-havel.sh` did not catch it either, for a second reason: it
scans `build-*` in the repo and sibling `../havel*/build-*` trees only. The
xbps-src chroot iteration layout used here puts the build at
`masterdir-x86_64/builddir/havel-dev`, which the script never looks at, so it
reported `no havel binary found` and exited 2 — safe (non-zero), but silent.
Note: that script exits **2** with no binaries; a reader must not mistake a
piped `$?` (which reports `head`'s status) for the script's own.

## Reproducer

```bash
# in a build dir configured at commit A, after committing A+1 and rebuilding
cd <builddir> && ninja            # recompiles, does NOT reconfigure
./havel --version                 # prints A, not A+1
git rev-parse HEAD                # A+1
```

## Resolution used this time

Reconfigure with the commit supplied explicitly (the chroot has no `git`, so
the `execute_process` fallback would stamp `unknown` instead):

```bash
SHA=$(git rev-parse HEAD)   # run on the host, where git exists
xbps-uchroot -b /home/void/Havel:/src masterdir-x86_64 -- \
  sh -c "cd /builddir/havel-dev && \
         cmake -DHAVEL_BUILD_COMMIT=$SHA -DHAVEL_BUILD_BRANCH=main /src && ninja"
./havel --version           # now 90023b15... = HEAD, clean
```

Cost: `HAVEL_BUILD_COMMIT` is a compile definition on `havel_core`, and Ninja
recompiles when a command line changes, so this rebuilt 131 objects (~2 min).
Both gates were then re-run to completion because the library was fully
recompiled, not merely relinked.

## Concrete next step

Make the stamp a **build-time** input rather than a configure-time one, so an
incremental rebuild cannot lie:

1. Add a `configure_file`-style custom command that regenerates
   `BuildIdentity.hpp` (holding `HAVEL_BUILD_COMMIT`, `HAVEL_BUILD_BRANCH`,
   `HAVEL_BUILD_DIRTY`) on every build, deriving the values from `git` when
   available and otherwise preserving the last non-empty value on disk.
2. Make `src/core/init/HavelLauncher.cpp` include that header instead of
   relying on the compile definitions, and add it as a source dependency so
   Ninja rebuilds it when the identity changes.
3. Delete the `execute_process` block (lines 16-53) and the three
   `target_compile_definitions` entries (line 1969).

That keeps the no-git case honest (`unknown` only when it has never been
stamped) and removes the "new code, old stamp" state entirely.

A second, cheaper step: teach `scripts/which-havel.sh` to also scan
`*/builddir/*/havel` (relative to the repo and its siblings), so a build laid
out by `xbps-src` is visible instead of silently absent.