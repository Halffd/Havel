# The `build-crane*` modes build a binary with NO JIT backend

**Status:** FIXED (Nov 11 2026, havel-2 `e8c2057a9`). Modes 17/18/19
now configure with `ENABLE_CRANELIFT=ON`.

## Symptom

A JIT gate run against a crane-mode binary fails every script with the
same message, even though the build itself succeeded:

    FAIL for_loop_range: JIT never tiered (tier1=0) - a tiered run that stays interpreted is not a JIT test
    FAIL for_loop_array: JIT never tiered (tier1=0) - a tiered run that stays interpreted is not a JIT test
    FAIL for_loop_break: JIT never tiered (tier1=0) - a tiered run that stays interpreted is not a JIT test

The build reports success (`CRANE_BUILD_RC=0`, `[100%] Built target ...`)
and the binary runs - it simply cannot tier anything, because it has no
JIT backend linked in. `scripts/ci/jit-parity-test.sh` treats a
`tier1=0` run as a FAIL, so the gate is red.

This is the exact failure mode the parity script's own NOTE warns about
("Pointing this gate at a backend-less binary fails every script with
'JIT never tiered' - which is correct: you cannot verify a JIT change on
a binary without a JIT"). The trap is that the binary's build DIRECTORY
says `build-crane*`, which reads as "this has Cranelift".

## Root cause

`build.sh` never passed `-DENABLE_CRANELIFT=ON` to CMake.

`CMakeLists.txt:117` declares the option, defaulting to OFF:

    option(ENABLE_CRANELIFT "Build the Cranelift backend prototype (requires cargo)" OFF)

The derivation existed and worked:

- `6f2a325f4` ("build: add cranelift build modes 17/18/19 to build.sh")
  added the modes AND derived the flag from the build-dir prefix:

      ENABLE_CRANELIFT="OFF"
      if [[ "$BUILD_DIR" == build-crane* ]]; then
        ENABLE_CRANELIFT="ON"
      fi
      ...
      cmake_cmd+=" -DENABLE_CRANELIFT=ON"

- `7c6635ac4` ("build: unified orchestrator for modes, tests, fleet,
  limits and disk") rewrote `build.sh` into the current orchestrator and
  DROPPED that derivation. The rewrite kept the mode table (modes 17/18/19
  still named `build-crane*`) but not the flag that gives the mode its
  meaning.

Verified in the history:

    git show 6f2a325f4 -- build.sh | grep -E '^[+-].*CRANELIFT'
    +  ENABLE_CRANELIFT="OFF"
    +  if [[ "$BUILD_DIR" == build-crane* ]]; then
    +    ENABLE_CRANELIFT="ON"
    +  cmake_cmd+=" -DENABLE_CRANELIFT=ON"

    git show 7c6635ac4 -- build.sh | grep -E '^[+-].*CRANELIFT'
    -  ENABLE_CRANELIFT="OFF"
    -  if [[ "$BUILD_DIR" == build-crane* ]]; then
    -    ENABLE_CRANELIFT="ON"
    -  cmake_cmd+=" -DENABLE_CRANELIFT=ON"

So every crane-mode build since `7c6635ac4` configured with
`ENABLE_CRANELIFT=OFF` and produced a JIT-less binary.

## Evidence

Measured on havel-2 after a clean mode-18 build:

    grep '^ENABLE_CRANELIFT:BOOL=' build-crane-nollvm/CMakeCache.txt
    ENABLE_CRANELIFT:BOOL=OFF

The same tree's other build dirs, showing what a correctly configured
crane build looks like:

    build-crane-release/CMakeCache.txt:  ENABLE_CRANELIFT:BOOL=ON
    build-crane-nollvm-hl/CMakeCache.txt: ENABLE_CRANELIFT:BOOL=ON

Those two were configured by hand or by an older `build.sh`; the
orchestrator-generated ones were not.

## Fix

`e8c2057a9` restores the derivation in `ws_build()`, keyed on the
build-dir prefix so all three crane modes inherit it uniformly and no
other mode is affected:

    local enable_cranelift="OFF"
    case "$bdir" in build-crane*) enable_cranelift="ON" ;; esac
    ...
    -DENABLE_CRANELIFT="$enable_cranelift"

The flag follows the same rule the original implementation used
(`build-crane*` prefix), so no second list of crane mode numbers exists.

## Verification

The flag reaches CMake for the right modes (dry run, no build):

    ./build.sh --dry-run 18 build  -> -DENABLE_CRANELIFT=ON
    ./build.sh --dry-run 19 build  -> -DENABLE_CRANELIFT=ON
    ./build.sh --dry-run  6 build  -> -DENABLE_CRANELIFT=OFF

After rebuilding mode 18 with the fix:

    grep '^ENABLE_CRANELIFT:BOOL=' build-crane-nollvm/CMakeCache.txt
    ENABLE_CRANELIFT:BOOL=ON

The binary self-reports `commit e8c2057a9e01 (havel-2, clean)`, and the
jit-parity gate against it is green - every script now actually tiers:

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

Log: `~/havel-logs/thrashfix_parity2.log`. Before the fix the same gate
reported `FAIL ... JIT never tiered (tier1=0)` on every script
(`~/havel-logs/thrashfix_verify.log`) - the binary had no JIT at all, so
`tier1` could only ever be 0.

## Note for anyone using a crane-mode binary

A crane-mode binary needs `cargo` and a writable `CARGO_HOME` at
configure time; `CMakeLists.txt` warns and disables the prototype if
cargo is missing. The Rust staticlib is built by a `cargo build
--release` custom command, which needs the crate registry - on this box
the offline registry cache is sufficient (`cargo build --release
--offline` succeeds).

## Related

- `scripts/ci/jit-parity-test.sh` - the gate that caught this.
- `docs/known-issues/shared-bytecode-cache-cross-worktree-thrash.md` -
  the cache-key fix whose parity gate ran into this defect.
