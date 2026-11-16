# A stale module plugin survives a reconfigure that drops its target

**Status:** FIXED (havel-2, 2026-11-16). The modules output dir is now
pruned of plugins whose target was not configured in this build.

## What it looks like

    [ERROR] module 'lua': build id mismatch (plugin 0xf66e51bc,
        host 0xac26b51e) - stale plugin compiled against different VM
        headers; rebuild the module
    [ERROR] module 'python': build id mismatch (plugin 0xf66e51bc,
        host 0xac26b51e) - stale plugin compiled against different VM
        headers; rebuild the module

and the two modules are then **silently unavailable** - every call into
them throws, and the only hint is this line at startup. To the user it
looks like the lua/python feature itself is broken.

## Mechanism

1. The plugin build id is an md5 of the 7 ABI headers computed **at
   configure time** (`CMakeLists.txt:2931-2947`,
   `HAVEL_MODULE_BUILD_ID`, `add_compile_definitions`).
2. A plugin `.so` is written into `${CMAKE_BINARY_DIR}/modules`
   (`MODULE_PLUGIN_OUTPUT_DIR`, `add_havel_module_plugin`'s
   `LIBRARY_OUTPUT_DIRECTORY`).
3. **Nothing removes a `.so` whose target no longer exists.** There is no
   `file(REMOVE ...)` for the modules directory anywhere in
   `CMakeLists.txt` - verified on havel-2 and main (byte-identical
   `CMakeLists.txt` blob `5466d94c2`, `grep -c 'file(REMOVE'` = 0) and on
   havel-stable (also 0).
4. So if a configure drops a plugin target - e.g. `pkg_check_modules`
   fails for `lua5.4` / `python3-embed` once, or a plugin is removed from
   `CMakeLists.txt` - the previous build's `.so` stays in the output dir
   forever.
5. A later configure recreates the id and the host plus all the *other*
   plugins get rebuilt with the new id. The orphan `.so` is not rebuilt
   (its target is gone) and keeps its old id.
6. The loader compares `abi->build_id` against
   `HAVEL_MODULE_BUILD_ID_STR` (`src/dl/Loader.c:529-531` and `:1046-1048`,
   `src/havel-lang/runtime/ModuleLoader.cpp:1194-1199`,
   `src/havel-lang/compiler/vm/VM.cpp:6746-6750`) and rejects it. Correct,
   but the message leads you to "rebuild the module" - which does
   nothing, because there is no target to rebuild.

## Evidence (2026-11-15, /host/havel-stable/build-release)

- host `havel`: `0xac26b51e`
- 48 of 50 plugins: `0xac26b51e` (match)
- `havel_mod_lua.so`, `havel_mod_python.so`: `0xf66e51bc` (mismatch),
  both timestamped 2026-11-08 20:21
- reconfigure ran 20:40 (`CMakeCache.txt` mtime 20:40:43); the 48 were
  rebuilt 20:41-20:46, the host at 20:59
- at that reconfigure the two targets were **not even created**:
  `grep -c havel_mod_lua Makefile` = 0, `grep -c havel_mod_python
  Makefile` = 0, neither appears in `CMakeFiles/TargetDirectories.txt`;
  their `CMakeFiles/havel_mod_lua.dir/flags.make` still carries
  `HAVEL_MODULE_BUILD_ID=0xf66e51bc` (20:21) while
  `havel_mod_app.dir/flags.make` and `havel.dir/flags.make` carry
  `0xac26b51e` (20:40)
- a fresh build fixes it completely: host `0x27a2110f` (the md5 of the 7
  ABI headers in that source) and **all 50 plugins `0x27a2110f`**,
  lua + python matching, `lua.eval` working

## The fix

`CMakeLists.txt` now does two things.

1. `add_havel_module_plugin` appends the target name it just created to a
   directory-scope list, `HAVEL_MODULE_PLUGIN_TARGETS`.
2. After every `add_havel_module_plugin` call (i.e. at the end of the
   `ENABLE_MODULE_PLUGINS AND ENABLE_HAVEL_LANG` block), it globs
   `havel_mod_*.so` in `MODULE_PLUGIN_OUTPUT_DIR` and `file(REMOVE ...)`s
   any file whose stem has **no corresponding target**. The check is
   `if(NOT TARGET ${_so_name})`, so it does not re-derive the name list -
   it asks the build system what exists now, which is the same
   non-duplication rule as the one for host-function name lists.
   Pruning happens once at configure time, so it cannot race a build.

Two things worth knowing if you edit it:

- **Seed the prune list with paths, never with target names.** An earlier
  draft seeded it with `HAVEL_MODULE_PLUGIN_TARGETS` (names) and appended
  paths; `file(REMOVE)` then removed *every* file whose path appeared, and
  the test that planted one orphan deleted all of them. Paths only.
- **Strip `.so` with `string(REGEX REPLACE)`, not
  `cmake_path(REMOVE_EXTENSION)`.** The latter is also fine here, but it
  is easy to mis-audit; the regex form makes the intent obvious and was
  used in the verified version.

## Verification of the fix

Run against a real build dir (`/home/agent/gate-merge-8bb2d47d`,
mode 18), with one orphan planted:

    before: 51 plugins
    $ cmake -S . -B build-crane-nollvm ...
    -- pruned orphan module plugin: havel_mod_deadplugin.so
        (no target configured in this build)
    -- Configuring done (61.8s)
    -- Generating done (2.8s)
    RC=0
    after: 50 plugins

and the surviving 50 are byte-for-byte the known-good set:

    $ ls build-crane-nollvm/modules/*.so | sed 's|.*/||; s|\.so$||' | sort
      > after.txt
    $ ls build-crane-nollvm/modules/*.so | sed 's|.*/||; s|\.so$||' | sort
      > known_good.txt && cmp after.txt known_good.txt   # no output = identical

The host build id was unchanged (`0x558d88ed`) - the prune does not
trigger a spurious rebuild. The same block also runs clean standalone:

    cmake -P <block>.cmake   # RC=0, correct message per orphan

## Note on framing

This hazard was not a defect on havel-2 or main - both produce
consistent builds today. It is recorded because a build that partially
fails, a target that is renamed, or a `pkg-config` probe that
intermittently fails will hit it, and the resulting error message
actively points at the wrong remedy.

Related entries:

- `crane-build-modes-have-no-jit.md` - the other
  "ENABLE_* is silently wrong" family; FIXED on main (`e8c2057a9`).
- `python-bridge-cpython-module-leak.md` - also a module-plugin
  lifecycle failure, on the runtime side.

## Testing the prune

Plant one orphan in an **isolated** build dir (never a shared one), then
configure. The prune message is the pass condition, and the plugin set
must be unchanged apart from the orphan:

    cp $BUILD/modules/havel_mod_app.so $BUILD/modules/havel_mod_fake.so
    cmake -S . -B $BUILD ...        # expect: "pruned orphan ... havel_mod_fake.so"
    ls $BUILD/modules/havel_mod_*.so | wc -l   # back to the original count

Note the prune runs at **configure** time, so it only fires when cmake
re-runs (a `CMakeLists.txt` edit, a flag change, or an explicit
configure) - a plain incremental `cmake --build` will not show the
message. This is intentional: a build-only run is exactly when a stale
dir must not be silently mutated.