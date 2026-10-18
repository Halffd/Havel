# Windows module status

Per-module triage for PLAT-001. Every row is a **source** classification:
whether the module's own code can compile on Windows, and separately whether
the subsystem behind it has a working Windows backend.

Three statuses:

- **portable** - no POSIX dependency in the module or the layer it calls.
  Compiles unchanged.
- **native-path** - a Windows implementation exists today, behind
  `HAVEL_PLATFORM_WINDOWS`. The POSIX path is untouched.
- **gated** - no Windows equivalent. Must compile out and raise a clear
  runtime error, not a crash and not a silent no-op.

## How this table was produced

Classification is mechanical, not a judgement call: a module is *portable*
only if neither its own source nor the core layer it calls includes a POSIX
header. Statuses below were read off the source. Rows marked
**needs audit** have not been verified end to end and must not be trusted as
shipping.

## Stdlib modules

| Module | Status | Notes |
|---|---|---|
| `array` | portable | |
| `bit` | portable | |
| `bytecodeBuilder` | portable | |
| `format` | portable | |
| `log` | portable | spdlog; verify sink paths before shipping |
| `math` | portable | |
| `object` | portable | |
| `option` | portable | |
| `pointer` | portable | |
| `regex` | portable | PCRE2, cross-platform |
| `string` | portable | |
| `timer` | portable | but see *Scheduler* below - timers ride the event loop |
| `type` | portable | |
| `fs` | native-path | `windows.h` already included under `HAVEL_PLATFORM_WINDOWS` |
| `random` | native-path | already has a Windows branch |
| `shell` | native-path | already has a Windows branch |
| `sys` | native-path | already has a Windows branch |
| `time` | native-path | already has a Windows branch |
| `state` | gated | includes POSIX headers unguarded; no Windows path |
| `brightness` | gated | module source is portable, but `core/BrightnessManager.cpp` reads sysfs/xrandr. Windows equivalent is `SetDeviceGammaRamp` + WMI. **Needs audit** on which surfaces the module exposes. |
| `hotkey` | gated | no Windows path. Ticket defers this to its own ticket: `WH_KEYBOARD_LL` is the only backend that matches evdev's modifier-only and key-up semantics. `RegisterHotKey` cannot. |
| `wayland` | gated | no Windows equivalent exists or will. Compile out permanently. |
| `browser` | needs audit | not yet triaged. Likely a socket or IPC backend with no Windows path. |
| `http` | portable | libcurl. Verify the Windows build of the dependency is what actually gets linked. |

## Core subsystems

| Subsystem | Status | Notes |
|---|---|---|
| Dynamic library loading | native-path | `src/dl/Loader.c`, `src/modules/ffi/DynamicLibrary.hpp`, `src/havel-lang/ffi/FFICall.cpp` already carry Windows branches. New shared primitive in `compiler/platform/dl.hpp`. |
| `window` | native-path | `src/core/window/backends/WindowsBackend.cpp` exists and is substantial. |
| `input` (injection) | native-path | `src/core/io/WindowsAdapter.cpp` + `WindowsIOBackend.cpp` exist. |
| `audio` | native-path | `core/media/backends/AudioBackendImpl.cpp` includes `windows.h` (WASAPI). |
| `process` | native-path | `src/core/process/Launcher.cpp` has a Windows branch. `ProcessManager.cpp` still includes POSIX headers unguarded. |
| `environment` | native-path | `src/core/util/Env.hpp` has a Windows branch. |
| `clipboard` | needs audit | `host/clipboard/Clipboard.cpp` has 4 unguarded POSIX includes; a Win32 `OpenClipboard`/`SetClipboardData` backend is required. |
| `net` | gated as written | `core/net/NetworkManager.cpp` has 5 unguarded POSIX includes. Winsock is a mechanical port, but it is a real port, not a shim. |
| `brightness` | gated | see stdlib table. |
| `signal handling` | needs audit | `core/io/SignalHandler.cpp` uses POSIX signals. Windows needs `SetConsoleCtrlHandler` for Ctrl-C and has no SIGPIPE. |

## What is *not* portable and should never be made so

- `havel-wm` / compositor role. DWM already is the Windows compositor. Porting
  this would be porting something Windows does not need. Explicitly out of
  scope.
- `uinput`, evdev. Linux kernel input model. Windows input injection goes
  through `SendInput` and stops there.

## Honest status of this document

Every "native-path" row means **a Windows implementation exists in the
source tree**. It does *not* mean it has been compiled, linked or run on
Windows. As of this writing no Windows build has been produced on any
machine - see `docs/build/windows.md` for why, and do not read this table as
a shipping claim.
