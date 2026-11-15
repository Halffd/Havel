# A stale module plugin survives a reconfigure that drops its target

**Status:** OPEN - confirmed build-system hazard, measured on
`/host/havel-stable/build-release` 2026-11-15. No code defect; the fix is
build hygiene and is deliberately not landed from a bug investigation.

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

## Fix direction

At configure time, prune the modules output dir: after all
`add_havel_module_plugin` calls, enumerate `havel_mod_*.so` in
`MODULE_PLUGIN_OUTPUT_DIR` and `file(REMOVE ...)` any file whose
corresponding target was not added. Cheap, deterministic, and it turns
this failure mode into a clean "module not found".

Alternatively (or additionally) make the rejection message assert the
more likely cause when the file's mtime predates the last configure:

    module 'X': build id mismatch (plugin ..., host ...) - no plugin
    target 'havel_mod_X' was configured in this build; the .so is a
    leftover. Re-run cmake configure (not just build).

## Note on framing

This hazard is NOT itself a defect on havel-2 or main - both produce
consistent builds today. It is recorded because a build that partially
fails, a target that is renamed, or a `pkg-config` probe that
intermittently fails will hit it, and the resulting error message
actively points at the wrong remedy.

Related entries:

- `crane-build-modes-have-no-jit.md` - the other
  "ENABLE_* is silently wrong" family; FIXED on main (`e8c2057a9`).
- `python-bridge-cpython-module-leak.md` - also a module-plugin
  lifecycle failure, on the runtime side.

## Reproduction to be careful about

Do not test this by planting a fake `.so` in a shared build dir and
running a script - that pollutes the dir for other sessions. If a
reproduction is needed, use an isolated worktree.