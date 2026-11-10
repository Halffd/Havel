# The shared bytecode cache thrashes between worktrees: every launch recompiles the same modules

**Status:** OPEN. Confirmed defect in the cache-key scheme, not an
environmental artifact. Distinct from
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

## Reproducer

```bash
# two checkouts with different content for the same module name
cd /host/havel-3   && ./build-debug/havel /tmp/triv.hv
cd /host/havel-2   && ./build-debug/havel /tmp/triv.hv
strings ~/.cache/havel/lang.debug.hvc | grep -m1 /host/   # -> havel-2
cd /host/havel-3   && ./build-debug/havel /tmp/opencode/triv.hv
strings ~/.cache/havel/lang.debug.hvc | grep -m1 /host/   # -> havel-3
```

## Next step

Make the cache key carry the source identity, so diverged content gets
distinct entries instead of fighting over one. Options, in order of
preference:

1. **Key by content, not by name.** The cache name already embeds a path
   hash for user modules (`<stem>.<8-hex-path-hash>.hvc`); the bundled
   `lang.`/`std.`/`app.` prefixes do not. Extend the same scheme to the
   bundled names so two checkouts with different content land in different
   files.
2. **Key by source path.** Include the checkout in the name (e.g.
   `lang.debug.<path-hash>.hvc`). This over-partitions - identical content
   in two checkouts would no longer share - so it is worse than (1) but
   simpler.
3. Keep one file per name but store multiple content variants inside it.
   Most invasive; no benefit over (1).

Whatever is chosen, the loader's lookup and the writer's path derivation
(`ModuleLoader::cacheFileNameForSource`, `autoCacheBytecodeChunk`,
`HavelLauncher`'s precompile path) must agree, and the pipeline-fingerprint
inputs (`lang.emitter`/`pratt`/`lexer`/`scope`) must keep resolving to a
single canonical entry or the self-hosted fingerprint logic breaks.
