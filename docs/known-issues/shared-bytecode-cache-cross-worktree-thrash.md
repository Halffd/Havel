# The shared bytecode cache thrashes between worktrees: every launch recompiles the same modules

**Status:** FIXED (cache key is now content-keyed). The fix landed on
havel-2; the known-issue entry is kept because the failure mode is subtle
and the fix touches a shared interface. Distinct from
`havel-process-linger-during-box-storms.md` (that one is host storage
congestion); this one reproduces on a calm box with load ~1.

## Mechanism

`~/.cache/havel` is a single flat namespace shared by every checkout on the
box, and a module's cache entry is keyed by **name**, not by source path:
`lang.debug.hvc`, `lang.math.math.hvc`, `std.array.hvc`, ...

The entry embeds the identity of the source it was compiled from: the
canonical path plus the sha256 of the source text
(`ValueSerializer::peekSourceInfoFile`). The loader validates the embedded
hash against the LIVE source it would resolve (`ModuleLoader.cpp`,
`checkBcCache`):

```
if (liveHash != embeddedHex) return std::nullopt;  // stale -> recompile
```

When two checkouts contain **different content** for the same module name,
each one's launch rejects the other's entry and recompiles it, re-stamping
the entry with its own identity. The next checkout's launch then rejects
that one. The result is a permanent ping-pong: the cache entry is rewritten
on essentially every launch, for every affected module.

The path embedded in the entry is not what decides the staleness - the
**content hash** is. Two checkouts with identical content share the entry
happily. But any checkout that has diverged (a feature branch, a cherry-pick,
an in-flight edit) thrashes with every other checkout that has not.

## Evidence (Nov 10, /host/havel-3)

Four checkouts share the cache; `modules/lang/debug.hv` has diverged:

```
havel          b980ac796eb0
havel-2        b980ac796eb0
havel-stable   b980ac796eb0
havel-3        ef8d77e6beb1   <- diverged
```

Alternating launches between two of them re-stamps the entry every time:

```
havel-3 wrote -> embedded: /host/havel-3/modules/lang/debug.hv
havel-2 wrote -> embedded: /host/havel-2/modules/lang/debug.hv
havel-3 wrote -> embedded: /host/havel-3/modules/lang/debug.hv
havel-2 wrote -> embedded: /host/havel-2/modules/lang/debug.hv
```

Three modules are rewritten per launch on this tree (`lang.debug`,
`lang.math.math`, `lang.math.physics`). Cost, same tree, back-to-back
launches:

| launch | wall |
|--------|------|
| after the other checkout ran (recompile) | 2431 ms |
| warm (cache hit) | 1735 ms |

So the thrash costs roughly 700 ms per launch per affected module, on top
of serializing concurrent launches on the per-entry `flock`.

## Why it matters

The smoke suite launches one havel process per test, so a full gate pays
the thrash ~390 times. Under concurrent load (other sessions running their
own gates against the same cache) the launches queue on the same lock, and
startup latency - not the tests - is what exceeds the per-test timeout
budgets. That is the mechanism behind the mass `(timeout)` failures in
`havel-process-linger-during-box-storms.md`: the entry there is "host
storage congestion", but shared-cache contention is a second, code-level
contributor that is present even on a quiet box.

The rewrite is also not free of correctness risk: `autoCacheBytecodeChunk`
writes under an exclusive `flock` and renames, so a reader that has the
old inode mapped is safe, but every rewrite still forces a full recompile
and a full re-serialization of the module.

## Reproducer (pre-fix behavior)

```bash
# two checkouts with different content for the same module name
cd /host/havel-3   && ./build-debug/havel /tmp/triv.hv
cd /host/havel-2   && ./build-debug/havel /tmp/triv.hv
strings ~/.cache/havel/lang.debug.hvc | grep -m1 /host/   # -> havel-2
cd /host/havel-3   && ./build-debug/havel /tmp/opencode/triv.hv
strings ~/.cache/havel/lang.debug.hvc | grep -m1 /host/   # -> havel-3
```

After the fix the same sequence produces TWO files, one per checkout's
content, and neither is re-stamped:

```bash
ls ~/.cache/havel/lang.debug*.hvc
#   lang.debug.<8hex-of-havel-3-content>.hvc
#   lang.debug.<8hex-of-havel-2-content>.hvc
```

## Fix: content-keyed cache entries

Implemented option (1) from the original plan - **key by content, not by
name**.

`ModuleLoader::cacheFileNameForContent(canonicalSourcePath)` is now the
single derivation point for the flat cache key:

| source | key |
|--------|-----|
| bundled module, normal | `<ns>.<rel>.<8-hex-content-hash>` (`lang.debug.a1b2c3d4`) |
| bundled module, self-hosted compiler | `<ns>.<rel>` (`lang.emitter`) - unchanged |
| user module | `<stem>.<8-hex-path-hash>` - unchanged |

The content hash is the SHA-256 of the module's SOURCE TEXT, so two
checkouts whose content for the same module name differs get DIFFERENT
files, and two checkouts with identical content still share one entry.
The thrash is gone because neither checkout can invalidate the other's
entry.

### The load-bearing constraint: the pipeline fingerprint

The four self-hosted-compiler modules (`lang.emitter`, `lang.pratt`,
`lang.lexer`, `lang.scope`) are the INPUTS of the pipeline fingerprint
(`computePipelineFingerprint`, `RuntimeSupport.cpp`): the fingerprint is
the hash of their `.hvc` files. Content-keying them would make the
fingerprint depend on which variant a loader happened to pick, and the
fingerprint would change every time the entry was rewritten - the endless
rebuild cycle the existing comment in `sha256_hvc_stable` describes.

Those four therefore keep their canonical names. The exclusion asks the
single source of truth (`isPipelineFingerprintInput`, which matches
`RuntimeSupport.cpp`'s `kFingerprintInputs`) rather than restating the
four names - a second hardcoded copy of that list would drift. Every
other bundled module is content-keyed.

### Every call site derives the key the same way

The loader and the writer MUST agree on the name, or the entry written is
not the entry read. All sites now call `cacheFileNameForContent`:

- `ModuleLoader::flatCacheFor` (loader lookup for a resolved source)
- `ModuleLoader::resolve` - the bare-name path resolves the live source
  first, then hashes it to find the content-keyed entry
- `autoCacheBytecodeChunk` (the writer, `RuntimeSupport.hpp`)
- `loadCachedScriptChunk` (the incremental serve path,
  `RuntimeSupport.cpp`)
- `HavelLauncher`'s precompile and `--build` / `--build-many` paths
- `VM::loadModule`'s persistent hash-index key

Two places parse a cache key back into a stem and had to learn the new
shape:

- `findLiveSourceForCacheName` strips the trailing 8-hex content hash
  before resolving the live source (`lang.debug.a1b2c3d4` -> `debug`).
- `computeCanonicalPathNoCache`'s cache-dir lookup no longer looks for the
  exact file `lang.<name>.hv`; it scans the cache dir for
  `lang.<name>` or `lang.<name>.<8hex>` and takes the newest match.

### The module identity must stay THIS tree's

`computeCanonicalPathNoCache` produces the key the VM identifies a loaded
module by. With content-keyed entries, the cache directory can hold
several entries for one module name - one per checkout's content - so
picking "the newest" arbitrarily would let one process identify a module
by another checkout's path. The lookup therefore resolves the LIVE source
first and asks for THAT source's exact content-keyed entry; the
newest-match scan is only the fallback for the installed-bundle case
(no live source on the search path), where there is only ever one
entry per name.

### Regression test

`~/havel-logs/test_cache_thrash.sh` - two checkouts with DIFFERENT
content for the same module name:

1. the two checkouts must end up with DISTINCT cache files,
2. the first checkout's entry must NOT be re-stamped by the second one
   (this is the thrash),
3. a repeat launch by the first checkout must REUSE its own entry (the
   warm-cache path still works).

It fails before the fix (both checkouts write the same file; the entry is
re-stamped on every launch) and passes after.

Measured against the pre-fix binary (`/host/havel`, which has no
`cacheFileNameForContent`): **FAIL** - `found 1 - the cache key is still
name-only`. Measured against the fixed binary (`/host/havel-2`, commit
100b9e49af4d): **PASS** - `lang.thrashprobe.dc061a34.hvc` and
`lang.thrashprobe.ecf150d9.hvc`, neither re-stamping the other.

The key-derivation logic itself was checked standalone (the same rules
mirrored into a throwaway C++ program, `/tmp/opencode/cachekey_test.cpp`):

    a -> lang.debug.286686ea
    b -> lang.debug.841e1ae7      <- diverged content: DIFFERENT keys
    c -> lang.debug.286686ea      <- identical content: SAME key
    emitter -> lang.emitter       <- fingerprint input: canonical name
    pratt   -> lang.pratt
    lexer   -> lang.lexer
    scope   -> lang.scope
    other   -> lang.other.52f042e1
    userthing -> userthing.baecef1e   <- user module: path-hash name
    ALL CHECKS PASSED

### Gate result on the fixed tree

Build: `./build.sh queue build` on commit 100b9e49af4d, rc=0, binary
self-reports `commit 100b9e49af4d (havel-2, clean)`.

JIT-parity gate (`scripts/ci/jit-parity-test.sh`), run against a
cranelift-enabled build of the same tree, commit
`e8c2057a9e01 (havel-2, clean)`, binary
`/host/havel-2/build-crane-nollvm/havel`
(`ENABLE_CRANELIFT:BOOL=ON`): **10 passed, 0 failed**, exit 0 - every
script actually tiers:

    PASS for_loop_range (tier1=4)
    PASS for_loop_array (tier1=4)
    PASS for_loop_break (tier1=4)
    PASS loop_with_break (tier1=4)
    PASS do_while (tier1=4)
    PASS closure_loop_capture (tier1=5)
    PASS chunk_navigator (tier1=8)
    PASS test_sorted_bare_global (tier1=6)
    PASS test_issue_jit_string_ordering (tier1=9)
    PASS selfhost (tier1=67)
    jit-parity: 10 passed, 0 failed

Log: `~/havel-logs/thrashfix_parity2.log`. The first attempt at this
gate failed with `JIT never tiered (tier1=0)` on every script - the
cause was NOT this fix but a defect in `build.sh`, which never passed
`-DENABLE_CRANELIFT=ON`, so the crane modes built a binary with no JIT
backend at all. See `crane-build-modes-have-no-jit.md`.

The cache-key regression test (`~/havel-logs/test_cache_thrash.sh`)
passes against the same tree: `PASS: diverged checkouts get distinct
cache entries and neither re-stamps the other's`, exit 0.

Smoke gate: `./build.sh queue test --kind gate` = **369 passed, 21 failed,
0 skipped** of 390 files. Every one of the 21 failures is a load
artifact, not a code defect:

- 20 are `(timeout)`, 1 is `test_issue_qt_namespace_restored (exit=1)`,
- the box ran the gate at load 11-23 with up to **18 processes in
  D-state** (`path_openat` / `read_extent_buffer_pages` / `btrfs-transaction`
  - the host-storage congestion recorded in
  `havel-process-linger-during-box-storms.md`),
- **all 21 pass when re-run individually** on the same binary:
  `~/havel-logs/rerun_gate_failures.sh` = **21 passed, 0 failed, 0 skipped**
  of 21 files,
- the same tests time out on `main` WITHOUT this fix, in the same window
  (`~/havel-logs/gate_main_postmerge.log`: 375 passed, 15 failed - the
  same closure_*, test_debug_on_off, test_fs_glob_absolute,
  test_extern_missing, test_hpkg_toolchain_store,
  test_issue_process_pipe_sigpipe set).

### The added cost

The key derivation hashes the module source, so the fix does one extra
sha256 per bundled-module lookup. Measured with the project's own sha256
(1000 iterations per file, `~/havel-logs/measure_hash_cost.sh`):

| module | bytes | us per hash |
|--------|-------|-------------|
| `lang/debug.hv` | 18937 | 148.1 |
| `lang/math/math.hv` | 2616 | 12.4 |
| `std/array.hv` | 2554 | 19.0 |
| `app/window.hv` | 60063 | 361.9 |

A full sweep of `modules/lang` (575953 bytes) costs about 5 ms. The
pre-fix code already hashed the live source during validation
(`checkBcCache`), so the marginal cost is well under the recompile the
thrash used to force (2431 ms vs 1735 ms warm, measured in the defect
section above).

### Not done: the cache directory is not cleaned up

The old name-only entries (`lang.debug.hvc`) are left on disk. They are
regenerable and harmless - nothing looks them up any more - but they are
not garbage-collected. A future cleanup pass could remove entries whose
name no longer matches any live module.

