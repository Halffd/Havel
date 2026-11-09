# `cfg.Gaming.Classes` returned an empty vivify shell (config section chain)

**Status:** FIXED on havel-2 (`649a3ee7f`). The root cause spanned three
places; fixing any subset leaves the chain broken (or breaks something else).
Sightings burned multiple sessions before the instrumented trace below
nailed it.

## Symptom

Any script doing the hotkeys0.3 pattern

    use "std/config" as cfg
    classes = cfg.Gaming.Classes.lower().trim().split(",")

read an empty object (or null) instead of the stored string. Older-style
`config.get("Gaming.Classes")` worked, which is why this survived gates: the
value was in the store, only the dot-read chain was broken. On pre-fix main
the failure surfaced as `Execution error: Length operator requires array,
string, object, or set` (null.lower()).

## Root cause (instrumented, Oct 31)

1. `loadModule("std/config")` bound the `.hv` wrapper namespace, not the host
   config object the wrapper's own comment calls `_store`. The wrapper only
   re-exports functions; sections live on the object. (v0.3.8 bound the host
   object; the pre-refactor binary matched.)
2. The naive fix - return the same-basename global object whenever plugin
   fns with that prefix are registered - ALSO fired for bit, math_native,
   log, random: their global objects carry only private fns (`bit._popcount`)
   while the public API lives in an `.hv` sidecar. Binding those objects
   regressed `bit.arshift/xor/...` to null across 16 smoke files (bisected:
   binding-only leg fails, WIP-only leg passes).
3. With the object bound, the wrapper's live-section publish
   (`_G[_sec] = _store[_sec]`, from havel-2 d3e21ce7b) became destructive:
   `_G` IS the bound object, and `_store[_sec]` is a bracket field read
   returning null for any not-yet-materialised field; the assignment wrote
   those nulls back over Gaming/Compiler/Mouse. OBJECT_GET only checks
   `!found_val.isNull()` (not key presence), so every later dot read of a
   nulled section fell into the autovivification branch and returned a fresh
   empty shell.

Instrumented trace (temporary prints, reverted): registration repopulate
sets the field (`nkeys=9 hasGaming=1`) -> `PUB Gaming t=null` -> `dot={}`.
With the publish removed: `dot={Titles: Genshin Impact, Classes:
steam_app_default,proton}`.

## Fix (649a3ee7f, one commit, three files)

- `modules/std/config.hv`: publish loop REMOVED (nothing needs to publish;
  registration repopulate already materialises the sections).
- `src/modules/config/ConfigModule.cpp`: the object declares itself with
  `__module_object` (set outside the create/reuse branch so re-registration
  cannot skip it).
- `src/havel-lang/compiler/vm/VM.cpp`: loadModule returns the same-basename
  global object IFF it carries `__module_object`.

## Verification

- chain probe: `classes=steam_app_default,proton`, type=string, ALL_GOOD -
  on the working tree AND on a WIP-less clean-base build
- `hvtest --smoke bit_`: 20/20 (clean base and tab)
- full fast-tier smoke 342/342; `scripts/ci/jit-parity-test.sh` 10/10
- `test_config_shadow.hv` passes (it does NOT cover this defect - it passed
  in every broken configuration; it exercises config{} writes and cfg
  member writes, not startup-loaded section reads through the binding)

## Regression-lock note

The chains that DO catch this class: the external probe
`final_chain.hv` + `reset_fixture.sh` (shared-coordination, used as the
post-merge gate) and `hvtest --smoke bit_` (catches the binding regression).
An in-repo self-contained test is not possible without a startup config
fixture; the probe is the tracked artifact.

## Related

- `docs/known-issues/hotkey-binding-external-root-pin-leak.md` (same
  session cluster)
- shared-coordination/2026-10-30-postmerge-chainfix-followup.md (merge set +
  resolution file)
