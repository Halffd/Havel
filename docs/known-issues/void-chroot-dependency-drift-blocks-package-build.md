# Void chroot dependency drift blocks the havel package build

**Status:** FIXED (2026-11-28): package builds and installs from the local
repo. Summary of the blockers and resolutions below.

## Symptom

`./xbps-src pkg havel` fails while building unrelated dependencies, with a
different package each run:

1. `libjxl-0.12.0_1`: `cannot find -lOpenEXR-3_2` (the devel package was
   3.2.1, the runtime 3.5.3 - version skew inside one chroot).
2. `ffmpeg6-6.1.6_3`: `ERROR: libbluray not found using pkg-config`
   (local source libbluray 1.3.4 vs remote 1.5.0).
3. `gst-plugins-base1-1.28.9_1`: `relocation R_X86_64_PC32 against symbol
   OC_SET_CHROMA_MVS_TABLE ... recompile with -fPIC` (static libtheora
   built non-PIC linked into a shared object - unresolved, next blocker).

Each failure aborts the havel build before it starts: the failure is in
the dependency chain, never in havel itself.

## Root cause

The masterdir snapshot (Nov 14/15) contains binary packages and source
templates from a different point in time than the remote repository.
xbps-src prefers LOCAL source templates over remote binaries, so an
outdated local template (e.g. `libopenexr` pinned at 3.2.1) forces a
rebuild of an old, internally inconsistent dependency set instead of
pulling the remote's coherent set. Local binary packages in
`hostdir/binpkgs` then shadow the remote the same way.

## What fixed (locally) the first two

- `libopenexr`: installed the matching-runtime `libopenexr-devel-3.5.3_1`
  into the masterdir and moved the stale local
  `libopenexr{,-devel}-3.2.1_1.xbps` out of `hostdir/binpkgs` (xbps-src
  kept re-selecting 3.2.1 from there).
- `libjxl`: local template patched to
  `-DJPEGXL_ENABLE_OPENEXR=OFF` and `libopenexr-devel` dropped from
  makedepends, plus `./xbps-src clean libjxl` (the CMake cache retained
  the old 3.2 flags across a dep swap - a clean is required after any
  dependency-version change).
- `libbluray`: installed `libbluray-devel-1.5.0_1` from the remote into
  the masterdir; local `ffmpeg6` template patched to
  `--disable-libbluray` and its makedepends line trimmed.

All local-only (`/home/void/void-packages`), nothing published.

## Next step

The remaining blocker is the gst-plugins-base1 static-libtheora link. The
same pattern likely applies: either supply a matching binary from the
remote (`libtheora`/`gst-plugins-base1`) or patch the local template.
After each fix, `./xbps-src clean <pkg>` before retrying - stale CMake
caches made the OpenEXR fix appear ineffective twice.

## Why this belongs in known-issues

Anyone verifying "the Void package builds" in a snapshot chroot will hit
a different one of these each time and may spend the same hour we did
concluding the failures were havel's. They are not: check the failing
package's template age against the remote before touching havel.

## Resolution (2026-11-28)

1. `libjxl` + `ffmpeg6`: local templates patched as above (OpenEXR off,
   libbluray off); both built.
2. `gst-plugins-base1`: theora ext disabled (`-Dtheora=disabled`, deps line
   trimmed). Built clean - opencv uses the base interfaces, not the codec.
3. `libopenexr`: the stale binpkgs files (lost to a tmpfs wipe during a
   restart) were rebuilt from the local source template with
   `./xbps-src pkg libopenexr`, restoring libopenexr-3.2.1_1 +
   libopenexr-devel-3.2.1_1 for opencv's exact pin.
4. `libopencv`: the remote-binary `libopencv-4.12.0_3` in the masterdir
   required OpenEXR >= 3.5 and collided with the local template's
   4.12.0_2/3.2.1 pair; removed (`xbps-remove -r masterdir havel
   libopencv-devel libopencv`) so the local source template takes over.

Also hit while working through this (unrelated to the drift, same
session):

- masterdir autodeps: xbps-src removes autodeps after a package build, so
  the freshly-built opencv disappeared from the masterdir between the
  package build and the next dev-tree build - reinstall from
  hostdir/binpkgs when a dev configure loses a dep.
- latent source bug found while fixing the fallout:
  src/core/ocr/OCR.cpp included tesseract headers inside
  `#ifdef HAVE_OPENCV` but used them under `#ifdef HAVE_TESSERACT`
  (commit 1be4a44a).

Final package: havel-0.3.9.20261128_1 from commit b45a7edb, 51 plugin
.so, installed and verified in the chroot (`havel --version` stamps the
commit; lua.sum / py scripts run through the installed binary).
