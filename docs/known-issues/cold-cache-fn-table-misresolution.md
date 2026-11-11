# Cross-session cold-cache fn-table misresolution

**Status:** OPEN - 3 sightings, 0 reproductions. The suspected trigger
(an invalid cache artifact) is confirmed to be REJECTED by the loader, so
the misresolution mechanism itself is still unexplained.

**Leading lead (new):** the shared, name-keyed bytecode cache. See
"Shared-cache TOCTOU" below and the related entry
`shared-bytecode-cache-cross-worktree-thrash.md` (havel-3, `b3c31da8e`).

## Symptom

A module's exported functions resolve to the wrong table entry after a
cold cache, so calls on them return `null`/false instead of the function.
Three sightings, all in the same shape: a module imported by a script that
had run in an earlier session, with the module's `.hvc` cache present but
stale.

## What has been ruled out

- **The 6-file WIP is not the fix.** An A/B across
  `{cold,warm} x {WIP,clean} x {uinput,kymap,mutex}` (Oct 25 and Oct 31
  records) produced 3/3 correct in every leg, with and without the
  `module_globals_by_chunk_`/`closureModuleGlobals` machinery. The defect
  does not reproduce without the WIP either.
- **A stale `.hvc` is not accepted.** `HavelLauncher.cpp` validates a
  cached chunk on FOUR independent gates before reusing it: embedded
  identity, pipeline fingerprint, compile options (`compiled_strict ==
  cfg.strictSemantics`), and the live source hash plus size
  (`sha256FileHex` compared against the hash embedded in the artifact).
  A stale artifact fails the hash gate and is recompiled. The mtime is
  explicitly not trusted ("mtime alone lies").
- **The cache is not shared unsafely.** Cross-binary reuse is a validated
  path, not a hazard: a cache written by one build was accepted by another
  only after the fingerprint and hash gates passed.

## Probe

`/tmp/opencode/cold_dispatch.hv` - external `uinput.sendEvent`/`sendKey`/
`sendAbs` on a fresh pad, with the `app.uinput.hvc` cache removed first.
Logs: `~/havel-logs/{h2_coldprobe,wipcheck_coldprobe}.log`.

Latest run (havel-2, binary self-reporting `a3ea877aa91c`, cache removed
before the run, 131792-byte artifact written by the run):

    sendEvent ext=true
    sendKey ext=true
    sendAbs ext=true

Requires `/dev/uinput` access (`sudo setfacl -m u:agent:rw /dev/uinput`
after each VM reboot). Without it the probe prints
`probe: uinput.create returned null` and exits 0 - that is NOT a pass.

### Cache-validation checks (run against the committed machinery)

| scenario | binary | cache | result |
|----------|--------|-------|--------|
| cold | havel-2 `a3ea877aa91c` | removed first | 3/3 true (artifact written by the run: 131792 bytes) |
| warm | havel-2 `a3ea877aa91c` | artifact from the cold run | 3/3 true |
| cross-session | main `bdd5eef5e70d` | artifact written by havel-2 | 3/3 true |
| corrupted artifact | main `bdd5eef5e70d` | 12 bytes overwritten at offset 100 | 3/3 true (rejected, module recompiled) |

The corrupted-artifact run is the direct check of the hash gate: the
artifact fails validation, is discarded, and the module is recompiled
from source - the calls still resolve correctly.

## The unresolved question

The sightings were all in a session where a `.hvc` existed that this
session's loader would reject. Three candidates remain:

1. The artifact was produced by a build whose fingerprint/hash gates
   behaved differently (an older loader, a different compile-options
   encoding), so it WAS accepted and the fn table inside it was genuinely
   wrong.
2. The misresolution is not in the cache at all - it is a runtime
   module-globals/closure-alias defect that only manifests in specific
   module shapes (the three sightings used different modules).
3. The shared, name-keyed cache (see below) served foreign bytecode.
   **NOW CLOSED AS A MECHANISM**: the bundled-module cache key is
   content-derived (`lang.debug.<8-hex-content-hash>`), so a foreign
   artifact can no longer be served under a key the reader validated -
   the writer's path carries the content hash the reader looked up, and a
   foreign artifact lands on a DIFFERENT path. See
   `shared-bytecode-cache-cross-worktree-thrash.md`.

To close this, a sighting needs its EXACT `.hvc` artifact and binary path
captured BEFORE invalidation. Without both, the next occurrence is
unreproducible.

## Shared-cache TOCTOU

`~/.cache/havel` is ONE flat namespace shared by every checkout on the
box. The entry embeds the identity of the source it was compiled from
(canonical path + sha256), and the loader validates that identity
against the live source it would resolve
(`ModuleLoader::checkBcCache`, `src/havel-lang/runtime/ModuleLoader.cpp`).

**The key is now CONTENT-derived** (`lang.debug.<8-hex-content-hash>`),
so two checkouts holding different content for the same module name get
DISTINCT entries and cannot overwrite each other - the ping-pong that
re-stamped the entry on every launch is fixed (see
`shared-bytecode-cache-cross-worktree-thrash.md`). Before that fix the
entry was keyed by NAME alone (`lang.debug.hvc`), each checkout rejected
the other's entry and recompiled it, and the entry was re-stamped on
every launch.

The TOCTOU window described below was observed against the OLD name-only
key. With content-keyed entries the writer can no longer replace the
file a reader validated under the same path with a foreign one - the
writer's path now carries the content hash the reader looked up, so a
foreign artifact lands on a DIFFERENT path. The window is therefore
closed for bundled modules by construction.

The window that remains (for the paths still shared by name):

- `checkBcCache` validates the `.hvc` header (`peekSourceInfoFile`) and
  compares it against the live source.
- On a match it returns `makeBcCache(hvcPath, hvPath, modulePath)`, which
  stores only the PATH (`ModuleLoader.cpp:168`).
- The file is opened and deserialized much later, in
  `VM::loadModule` (`src/havel-lang/compiler/vm/VM.cpp:6809`):
  `serializer.loadChunk(resolved->canonicalPath, 65536)`.

Between the validation and that read, another checkout can `rename` a
different artifact over the same path. The writer IS atomic
(`autoCacheBytecodeChunk` writes a tmp file and renames under an
advisory `flock`, `RuntimeSupport.hpp:481`), so a reader never sees a
torn file - but it CAN see a COMPLETE, FOREIGN file that was never
validated. The read is not re-checked against the source identity.

That is the mechanism this defect's shape predicts: a module whose
export set differs between the two trees (havel-3's `debug.on`/`debug.off`
do not exist in the others) would resolve against a function table that
lacks the requested name.

Probe: `/home/agent/havel-logs/toctou_cache_probe.sh` - a havel-2 reader
racing a havel-3 writer on the shared `lang.debug.hvc` entry, checking
whether `debug.on()`/`debug.off()` ever resolve in havel-2 (they must
NOT; they only exist in havel-3's source). Log:
`~/havel-logs/toctou_cache_probe.log`.

**Result: 12/12 rounds, the reader never served foreign bytecode.** Under
concurrent load (the box was at D-state 15-21 during the run - the
widest the race window gets), every round produced:

    round N reader rc=0 on_resolved=1 :: READBACK isOn=false READBACK on=null READBACK off=null

`on=null` means the export did NOT resolve in havel-2 - the hash gate
rejected the foreign entry and the module was recompiled from havel-2's
own source. So the TOCTOU window is real in the code but did NOT
manifest here. It remains a theoretical lead, not a demonstrated cause:
the window is narrow (validate -> rename -> open -> deserialize) and the
writer's `flock` plus atomic rename make the swap rare.

Recorded as ruled-out-by-experiment, not as a fix. The defect stays open
with 3 sightings and now 4 independent ruled-out hypotheses (the WIP, a
stale artifact, unsafe cross-binary reuse, and the shared-cache TOCTOU).

## Related work

- `module_globals_by_chunk_` / `closureModuleGlobals` machinery (the
  former 6-file WIP, now committed in `9020effb9` on havel-2 and
  `79faf8066` on main) was written by efa39c for this class of defect. It
  is committed, not a demonstrated fix for these sightings.

### Provenance of the committed machinery

The machinery entered the history inside `9020effb9` ("fix(config): stop
capturing VMApi by reference across a dead frame") on havel-2 and the
equivalent `79faf8066` on main - commits whose SUBJECT was the config
capture, not this machinery. The six files it touched
(`JitRuntimeBridges.cpp`, `VM.cpp`, `VM.hpp`, `VMControlFlow.cpp`,
`VMDispatch.cpp`, `CoreRuntimeExports.cpp`) were efa39c's unstaged
working tree at the time. The commit message does not mention them, so
the machinery's origin is only discoverable from the preservation records.

Verified after the fact: the committed machinery is byte-identical to the
preserved patch - all 14 `module_globals_by_chunk_` / `closureModuleGlobals`
lines match `~/havel-logs/preserved/havel2-worktree-ALL-20261024-223358.patch`
exactly, and 524 of the patch's 531 other added lines are present in HEAD
(the remaining 7 are superseded by later work: the `wantsSelf` receiver
rule, `loadLocalRouted`, and the boxed-upvalue capture, all of which HEAD
carries in newer form). `wip/efa39c-vmcontrolflow` (tip `9858733c8`) and
`~/havel-logs/wip-efa39c-vmcontrolflow.patch` remain as the preservation
of record; its `VMControlFlow.cpp` content is now OLDER than HEAD.

efa39c's session was last seen ~18 days before this entry was written and
the ownership decision was therefore taken here: the machinery is
committed on both trees, gated, and its correctness test
(`scripts/smoke/test_vm_opt_001_ambient_globals_swap.hv`) passes.
- `docs/known-issues/havel-process-linger-during-box-storms.md` - the
  storage-congestion phenomenon that made several reproduction attempts
  inconclusive.
- Records: `~/texts/shared-coordination/2026-10-25-coldcache-verdict.md`
  and `~/texts/shared-coordination/2026-10-31-coldcache-reconfirm-b04a06659.md`.
