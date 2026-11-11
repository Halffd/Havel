# The python bridge leaks CPython module state when the bytecode cache is not at `$HOME/.cache/havel`

**Status:** OPEN. Pre-existing - it reproduces on a binary that carries
none of the cache-key work (verified below). It makes
`scripts/smoke/test_python_bridge.hv` fail under AddressSanitizer, so it
turns into a smoke-gate failure for anyone who points the cache
elsewhere.

## Symptom

Running the python bridge test with a bytecode cache outside the
default location makes ASan report a leak and the test fail:

    Direct leak of 1520 byte(s) in 1 object(s) allocated from:
      #5 PyModule_FromDefAndSpec2
    Direct leak of 768 byte(s) in 1 object(s) allocated from:
      #5 PyEval_EvalCode
    Direct leak of 1 byte(s) in 1 object(s) allocated from:
      #2 PyModule_ExecDef
    SUMMARY: AddressSanitizer: 2289 byte(s) leaked in 3 allocation(s)

All three allocations are inside CPython's module machinery
(`PyModule_FromDefAndSpec2`, `PyEval_EvalCode`, `PyModule_ExecDef`),
reached from the module import in
`src/modules/python/PythonModule.cpp:596` (`pyImport` ->
`PyImport_ImportModule`). The test itself still prints every `PASS:`
line - the failure is the leak, not a wrong result.

## What triggers it

The variable is the LOCATION of the bytecode cache, not its contents.
Same binary, same cache CONTENTS (a full `cp -a` copy), different
location:

| cache location | result |
|----------------|--------|
| `$HOME/.cache/havel` (the default) | **PASS**, no leak |
| `$XDG_CACHE_HOME=/home/agent/.cache` (same dir, different spelling) | **PASS**, no leak |
| `$XDG_CACHE_HOME=/home/agent/.cache/havel3/..` (same dir via `..`) | **PASS**, no leak |
| `$XDG_CACHE_HOME=/home/agent/xxcache` (under `$HOME`, not `.cache`) | **LEAK** |
| `$XDG_CACHE_HOME=/tmp/pc-pre` (outside `$HOME`) | **LEAK** |
| `$HOME=/tmp/pc-home` (cache at `/tmp/pc-home/.cache/havel`) | **LEAK** |

The filesystem does not matter: `/home/agent/xxcache` and
`$HOME/.cache/havel` are on the same btrfs volume, and only the path
differs. The contents do not matter either: a byte-identical copy of
the default cache leaks when it is moved to a non-default path, and the
default path with the same copy passes.

## It is pre-existing

The pre-fix binary reproduces it identically:

    /host/havel/build-debug/havel - commit f77a69e434ee (main, dirty)
    (this build has NO cacheFileNameForContent - it predates the
     content-keyed cache work)

    $ XDG_CACHE_HOME=/tmp/pc-pre ./build-debug/hvtest --smoke test_python_bridge
    [FAIL] test_python_bridge (exit=1)
    SUMMARY: AddressSanitizer: 2289 byte(s) leaked in 3 allocation(s)

    $ ./build-debug/hvtest --smoke test_python_bridge
    smoke: 1 passed, 0 failed, 0 skipped | 1 files

Same binary, same test, same moment - only the cache location differs.

## Why it matters for the gate

`scripts/ci/jit-parity-test.sh` and any gate that points
`XDG_CACHE_HOME` at a scratch directory (to isolate a tree from the
box-wide `~/.cache/havel`) hits this and reports
`test_python_bridge` as failed. The obvious workaround - isolate the
cache so the tree does not fight the other checkouts - therefore
introduces a gate failure that has nothing to do with the code under
test.

## Next step

The leak is inside CPython's module exec path, so the likely cause is
a reference that is not released when the interpreter's module state is
torn down in a non-default configuration - e.g. the `PyGuard` in
`PythonModule.cpp` finalizing in a different order, or the plugin's
module object outliving the interpreter finalization. It needs a
targeted look at `src/modules/python/PythonModule.cpp` (the `PyGuard`
lifetime around `pyImport`) and at how the plugin initializes and
finalizes CPython.

Minimal reproducer:

    XDG_CACHE_HOME=$(mktemp -d)/havel ./build-debug/havel \
        scripts/smoke/test_python_bridge.hv
    # -> AddressSanitizer: 2289 byte(s) leaked in 3 allocation(s)

The `XDG_CACHE_HOME` override is honoured by
`ModuleLoader::getDefaultCacheDir`, so the whole binary follows it.
