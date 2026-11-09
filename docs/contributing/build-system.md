---
title: "Build System"
description: "Build modes, commands, dependencies, and configuration."
---

# Build System

## Quick Start

```bash
./build.sh build        # mode 6: Debug + tests + Havel Lang, no LLVM
./build.sh test --kind quick   # seconds-long gtest subset
./build.sh status       # what is built where, and how stale
```

Full command reference: `./build.sh help`.

## Build Modes

```bash
./build.sh [--mode N] <command>
```

| Mode | Type | Tests | Havel Lang | LLVM | Build Dir |
|------|------|-------|------------|------|-----------|
| 0 | Debug | ✓ | ✓ | ✓ | build-debug |
| 1 | Release | ✗ | ✗ | ✓ | build-release |
| 2 | Debug | ✗ | ✓ | ✓ | build-debug |
| 3 | Debug | ✗ | ✗ | ✗ | build-debug |
| 4 | Debug | ✓ | ✓ | ✗ | build-debug |
| 5 | Release | ✓ | ✓ | ✓ | build-release |
| 6 | Debug | ✓ | ✓ | ✗ | build-debug (default) |
| 7 | Release | ✗ | ✗ | ✗ | build-release |
| 8 | Debug | ✗ | ✓ | ✗ | build-debug |
| 9 | Release | ✓ | ✓ | ✗ | build-release |
| 10 | Debug | ✗ | ✓ | ✓ | build/ |
| 11 | Release | ✗ | ✓ | ✓ | build/ |
| 12 | Debug | ✓ | ✓ | ✗ | build-headless |
| 13 | Release | ✗ | ✓ | ✗ | build-headless |
| 14 | Debug | ✗ | ✓ | ✗ | build-headless |
| 15 | Release | ✓ | ✓ | ✗ | build-headless |
| 16 | Debug | ✗ | ✓ | ✗ | build-tsan |
| 17-20 | crane/nollvm variants | see `BUILD_CONFIGS` in `build.sh` |

## Commands

### Build Lifecycle

```bash
./build.sh build                # configure + build
./build.sh rebuild              # clean + build
./build.sh clean                # remove the mode's build dir
./build.sh 5 build              # explicit mode
./build.sh run script.hv        # run the built binary (prints its commit)
```

### Tests

Four kinds, because "run the tests" means very different durations:

| Kind | What runs | Duration |
|------|-----------|----------|
| `quick` | fast gtest subset (`config_store_test`, `window_manager_detector_test`) | seconds |
| `gate` | `hvtest --smoke --slow-too --timeout 300` — **the pre-merge gate** | ~6-10 min |
| `smoke` | `hvtest --smoke --timeout 180` (skips slow-tier) | ~3-5 min |
| `ctest` | full ctest | ~8-15 min |

```bash
./build.sh test --kind gate
```

### Fleet: Working Across Checkouts

Several copies of this repo usually exist side by side (`/host/havel`,
`/host/havel-2`, `/host/havel-3`, `/host/havel-stable`). `fleet` runs the same
operation in each, and `status` reports every build directory in every tree —
including which commit each binary actually contains.

```bash
./build.sh fleet status         # the cross-tree table
./build.sh fleet build          # build every workspace (sequential)
./build.sh fleet test           # test every workspace
./build.sh fleet gc             # disk report everywhere
```

Binaries are identified by the commit they self-report, never by file
timestamp: a newer mtime can carry an older commit, and the same relative path
(`build-debug/havel`) exists in several trees. `./build.sh which` prints the
recommended binary; `scripts/which-havel.sh --json` gives the same data
machine-readably.

### Disk Management

Build directories across all workspaces total ~23 GB. `gc` reports sizes and
reclaim candidates, and is **dry-run by default** — nothing is deleted without
`--yes`.

```bash
./build.sh gc                               # report only
./build.sh gc --prune --older-than 14d --yes    # delete build dirs idle >= 14 days
./build.sh gc --hardlink --yes              # hardlink identical plugin .so across trees
./build.sh gc --ccache-zero --yes           # clear the shared compiler cache
```

Compiler caching is shared: every workspace uses the same ccache directory, so
rebuilding the same source in a second tree is mostly cache hits.

### Resource Limits

```bash
./build.sh limit show
./build.sh limit set max-parallel 2     # fleet concurrency (default 1)
./build.sh limit set timeout 3600       # per-command timeout (0 = none)
./build.sh limit set ccache-size 8G     # also applied to ccache immediately
```

Limits persist in `~/.cache/havel-build/limits.conf`.

### Managing Running Builds

```bash
./build.sh jobs                 # running cmake/ninja/clang/hvtest/ctest, all agents
./build.sh pause <pid>          # SIGSTOP the process and its descendants
./build.sh resume <pid>         # SIGCONT
./build.sh stop <pid>           # SIGTERM, then SIGKILL after 5s
```

Builds and tests always run in the foreground and report their real exit
status; nothing is detached and polled. `jobs` discovers live processes rather
than relying on bookkeeping, so it works across agent sessions.

### Queueing (Foreground)

Two agents building the same directory interleave two cmake runs over one dir
and corrupt each other. Every build and test takes a per-directory lock, so the
second one waits instead of colliding — and the wait is always in the
foreground, with progress, never detached:

```bash
./build.sh queue build              # wait if busy, build, notify on completion
./build.sh queue test --kind gate
./build.sh await                    # block until the lock is free; run nothing
./build.sh locks                    # who holds what right now
./build.sh notify "text"            # bell + desktop notification + log
```

While queued you see the holder and elapsed time every 30 s, so a long wait
never looks like a hang. On completion the notifier fires: a terminal bell, a
desktop notification when a display is reachable, and an entry in
`~/.cache/havel-build/notifications.log`.

Because the lock lives in the build/test functions themselves, plain `build`
and `queue build` serialize identically — `queue` is preferred because it is
the only one that notifies. A lock whose holder died is reported free on the
next `locks` call, so nothing can wedge the tree.

### Timing History

Every build and test run records duration and exit status to
`~/.cache/havel-build/times.jsonl`:

```bash
./build.sh report
# WORKSPACE      MODE     COMMAND   RUNS    AVG-s    MIN-s    MAX-s
# havel-3        6        build        1   1356.0   1356.0   1356.0
# havel-3        6        test:quick   1      7.0      7.0      7.0
```

Logs land in `~/.cache/havel-build/logs/`, one file per configure/build/test.

### Sanitizer Flags (Debug builds)

```bash
./build.sh --asan-level full build      # strict ASAN
./build.sh --ubsan-full build           # all UBSAN checks
./build.sh --tsan build                 # ThreadSanitizer (mode 16)
./build.sh --no-asan build              # disable ASAN
./build.sh --fsan address,undefined build  # extra sanitizers
```

### Environment Variables

```bash
THREADS=8 ./build.sh build              # parallel jobs (--threads also works)
```

---

## Build Artifacts

### Executables

| Binary | Location | Purpose |
|--------|----------|---------|
| `havel` | `build-debug/havel` or `build-release/havel` | Main application |
| `havel-lsp` | `build-debug/havel-lsp` | Language Server Protocol server |
| `havel-bytecode-smoke` | `build-debug/havel-bytecode-smoke` | Bytecode smoke test (Debug only) |

### Running Havel Scripts

```bash
./build-debug/havel script.hv           # Run a script
./build-debug/havel run script.hv       # Same, explicit
./build-debug/havel                     # Start REPL
```

### Debug Flags

```bash
./build-debug/havel run script.hv -d -dl -dp -da --debug-bytecode --debug-jit
```

| Flag | Description |
|------|-------------|
| `-d` | General debug |
| `-dl` | Debug lexer |
| `-dp` | Debug parser |
| `-da` | Debug AST |
| `--debug-bytecode` | Trace bytecode execution |
| `--debug-jit` | Trace JIT compilation |

---

## Troubleshooting

### Clang Not Found

```
error: clang not found
```

Install Clang and ensure it's in PATH:
```bash
sudo apt install clang
which clang clang++
```

### Missing X11 Headers

```
fatal error: X11/Xlib.h: No such file or directory
```

Install X11 development packages:
```bash
sudo apt install libx11-dev libxtst-dev libxrandr-dev libxinerama-dev libxcomposite-dev
```

### spdlog/nlohmann_json Not Found

```
Could not find spdlog via pkg-config
```

These are looked up via pkg-config. Install both the library and its development files:
```bash
sudo apt install libspdlog-dev nlohmann-json3-dev pkg-config
```

### Qt6 MOC Conflicts

Havel defines `True=1` and `False=0` as macros to avoid keyword conflicts with Qt's MOC system. If you see errors related to `True`/`False` keywords, ensure the build defines these macros before including Qt headers.

### LLVM Linker Errors

If building with LLVM (modes 0, 5) and you get linker errors, either:
1. Install LLVM development libraries: `sudo apt install llvm-dev`
2. Switch to non-LLVM modes: `./build.sh 6 build`

### Release LTO Relocation Overflow

The `havel-bytecode-smoke` binary is Debug-only because Release LTO causes relocation overflow. This is a known limitation — use Debug builds for the smoke test.

### Lua 5.4 Not Found

```
Could not find Lua 5.4
```

Ensure both the runtime and development files are installed:
```bash
sudo apt install lua5.4 liblua5.4-dev
```

### Tesseract/OpenCV Issues

```
Could not find Tesseract
```

These are critical dependencies. Install the full set:
```bash
sudo apt install libtesseract-dev libleptonica-dev libopencv-dev
```

---

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (x86_64) | Primary | Full feature support |
| Linux (ARM64) | Should work | Not regularly tested |
| macOS | Partial | No X11; requires Qt or ImGui backend |
| Windows | Not supported | No X11; would require significant porting |

Havel is primarily designed for Linux desktop automation. Other platforms require adapting the X11-dependent core modules.

---

## Dependency Detection

```bash
./build.sh detect
```

This shows which dependencies are found and which are missing, with install hints.

---

## Self-Hosted Pipeline

```bash
# Build self-hosted modules (runs after main build)
./emit_pipeline.sh ./build-debug/havel
```

Compiles `modules/lang/*.hv` to `out/modules/lang/*.hvc`.

---

**Previous:** [Extern Types Reference](/reference/extern-types)
**Next:** [Directory Structure →](/contributing/directory-structure)