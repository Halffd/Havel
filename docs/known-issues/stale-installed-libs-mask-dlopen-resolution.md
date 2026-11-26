# Stale installed libraries mask dlopen/RUNPATH defects via the ld cache

**Status:** ENVIRONMENT HAZARD (observed 2026-11-25/26 on the chroot
masterdir; re-created every time `build.sh install` is verified)

## Symptom

A plugin that cannot resolve its own shared library loads anyway:

```text
INFO havel_loader_load_module: dlopen .../modules/havel_mod_ffi.so for ffi
(dlopen succeeds - although the plugin has no RUNPATH and
libhavel_ffi_types.so is not next to it)
```

while the same layout on another machine dies with:

```text
ERROR dlopen failed for module 'ffi': libhavel_ffi_types.so: cannot open
shared object file: No such file or directory
```

The mismatch silently flips RUNPATH-verification results and makes
"works in my chroot" claims worthless for anything dlopen-related.

## Root cause

`build.sh install` (and the package install rules) drop
`libhavel_*.so` into `/usr/lib` and plugins into `/usr/lib/havel/modules`.
Those files persist after the build tree moves on, and `ldconfig` caches
them. glibc resolves a dlopen'd plugin's own DT_NEEDED libraries through
the ld cache after RUNPATH - so an old, ABI-mismatched copy of the helper
"fixes" a plugin whose RUNPATH is broken, and every verification run
against that environment passes for the wrong reason.

Observed concretely (2026-11-26): the chroot masterdir carried a
Nov-15-era `/usr/lib/libhavel_ffi_types.so` (4 KB, previous commit). With
it present, `use "ffi"` loaded from the build tree even though the ffi
plugin had no RUNPATH; after `rm -rf /usr/lib/havel /usr/lib/libhavel_ffi_types.so
&& ldconfig`, the same script failed until the real fix (plugin
BUILD_RPATH/INSTALL_RPATH, commit fc2dc94b-era `9d1f7bae`) landed.

## Reproducer

In any environment with an old `havel` install:

```sh
ldconfig -p | grep havel          # what the cache will "fix" for you
ldd <build>/modules/havel_mod_ffi.so | grep -i 'not found'
HAVEL_STDLIB=modules/std <build>/havel run -e 'use "ffi"; print("ok")'
```

Then remove the stale files (`rm -rf /usr/lib/havel /usr/lib/libhavel_ffi*.so
&& ldconfig`) and run the same script: a PASS before and a FAIL after
means the environment, not the build, was supplying the resolution.

## Why this matters

- Verification of dlopen/RUNPATH/loader changes must run in an environment
  with no havel files under /usr (or with the ld cache inspected first).
- "It loads here" is not evidence; check `ldd <plugin>` for `not found`
  and the ld cache for havel entries before believing it.

## Next step

- The C++ plugin targets now carry their own
  `BUILD_RPATH "$ORIGIN:$ORIGIN/.."` / `INSTALL_RPATH
  "$ORIGIN:$ORIGIN/..:$ORIGIN/../.."`, so resolution no longer depends on
  the cache. `build.sh install` could run `ldconfig` with a note, or
  refuse to install over a differently-built tree, to stop seeding stale
  masks.
- When diagnosing any loader failure, first list the cache entries for
  havel (`ldconfig -p | grep -i havel`).
