# PLAT-001 investigation notes

Working notes behind the Windows port: what was measured, what was wrong with
early conclusions, and where the proven implementations live in history.
Kept because the reasoning is not recoverable from the diff alone.

## Toolchain

The ticket targets clang-cl + UCRT. On the development machine:

- MSVC 19.51.36260.0 (VS 18 Insiders, `VC/Tools/MSVC/14.51.36231`), Windows
  SDK 10.0.26100.0 targeting Windows 10.0.22631, CMake 4.4.4.
- `clang-cl.exe` is a **zero-byte file**. `clang.exe --driver-mode=cl` works, so
  the cl-mode *driver* is fine, but the MSVC C++ standard library is what the
  compiler needs and it ships only with MSVC. Everything measured here is
  therefore MSVC - the ticket's stated fallback, not its preferred compiler.
- MinGW g++ 14.2.0 is installed and does compile, but **only with
  `C:\msys64\mingw64\bin` on PATH**: `cc1plus` loads its DLLs from there and
  without it the compiler exits 1 printing nothing, which reads as a hang. It is
  useful as a syntax-check proxy and is not a shipping target.
- No Linux host: WSL2 is enabled but has no distribution, and nested QEMU
  virtualization prevents installing one. **No Linux or macOS verification is
  possible here**, which is the single biggest gap in this work.

Configure that works:

```bat
cmake -S . -B build-msvc2 -G "Visual Studio 18 2026" ^
      -DUI_BACKEND=HEADLESS -DENABLE_QT_UI_BACKEND=OFF ^
      -DGIT_EXECUTABLE=C:/PROGRA~1/Git/cmd/git.exe
cmake --build build-msvc2 --config Release --parallel 2
```

`--parallel 2` is load-bearing: MSVC runs out of heap at 8 jobs and the
resulting `C1060` / `MSB8071 OutOfMemoryException` failures bury the real error
list behind memory noise.

### Two environment gotchas that produced false results

- `/std:c++23` is **rejected by `cl`** with `warning D9002: ignoring unknown
  option`. A hand-rolled probe using it silently compiled as C++14 and
  cascaded into ~140 spurious `std::string_view` errors. The build itself uses
  `/std:c++latest`.
- "The file is absent from the error list" does **not** mean "the file
  compiled". MSBuild at minimal verbosity prints nothing for files that
  succeed. A direct `cl /c` of the single file is the only sound check.

## The Windows IO that used to work

Found via `git log -S`, not by filename search - the files moved directories
several times, so a path search misses them entirely.

Timeline of `keybd_event` / `RegisterHotKey` / `WH_KEYBOARD_LL` /
`GetAsyncKeyState` in the tree:

| Date | Commit | What |
|---|---|---|
| 2024-10-27 | `db432532` "windows, io, logger classes" | first commit; adds `RegisterHotKey`, `keybd_event` |
| 2024-11-01 | `e75b1184` "tests, binary function" | adds `GetAsyncKeyState` |
| 2024-11-12 | `8947b0bf` "keyboard hook" | adds `WH_KEYBOARD_LL` |
| 2024-11-29 | `c545b7d5` "IO x11 events and keys..." | X11 alongside Windows |
| **2025-03-24** | **`0bc9288f`** | **removes all four** while rewriting `src/core/IO.cpp` (897 lines, mostly deletions) |

`0bc9288f^` = **`433f4646`** is the last working Windows state: 1144 lines,
14 `#ifdef WINDOWS` regions, including `KeyboardProc` (the `WH_KEYBOARD_LL`
hook), `AssignHotkey`/`HotkeyListen` (`RegisterHotKey` + `WM_HOTKEY`),
`GetMouse`/`GetKeyboard` (`GetAsyncKeyState`), `Suspend`, `ControlSend`,
`GetState`, and the `+^!#` / `{Ctrl down}` parser.

Note the ticket describes the low-level hotkey hook as "deferred to its own
ticket, it's its own beast". It was already written and working in 2024.

The key-name to `VK_*` table survived the rewrite in
`src/core/io/KeyMap.cpp` (`AddKey("enter", KEY_ENTER, XK_Return, VK_RETURN)`).

## Macro collisions

Every one of these was found by compiling, not by reading, and each reproduces
**only in a whole-TU compile** - the header is fine on its own. That is why they
survive review, and why preprocessing the real failing translation unit is the
only reliable way to see them:

```bat
cl /nologo /E /utf-8 /std:c++latest /DWIN32 /D_WINDOWS <includes> HostModules.cpp
```

| Project identifier | Colliding macro | Resolution |
|---|---|---|
| `VMExecutionResult::Type::ERROR` | `wingdi.h:118` `#define ERROR 0` | renamed to `THREW` |
| `.stdout` / `.stderr` members | `corecrt_wstdio.h:37-38` `(__acrt_iob_func(1))/(2)` | renamed to `.out` / `.err` |
| `AudioBackend::WINDOWS` | `include/types.hpp` `#define WINDOWS 1` | renamed to `WASAPI` |
| `struct DeviceCapabilities` | SDK alias `#define DeviceCapabilities DeviceCapabilitiesA` | renamed to `InputDeviceCapabilities` |
| `pid_t` | absent on MSVC | `typedef int` in `havel_platform.h` |

`WINDOWS` cannot simply be removed: eleven real `#ifdef WINDOWS` sites depend on
it (`WindowsIOBackend`, `IO.cpp`, `KeyMap.cpp`, `DisplayBridge.cpp`).

`AudioBackend::WINDOWS` was worse than a compile error - expanding to `1` still
parses, and silently aliases the Windows backend onto `PULSE`.

All of these were first attempted with `#pragma push_macro`/`pop_macro`, which
is **not ISO C++** (a compiler extension). A macro has no scope, so no
bracketing survives on a toolchain without the extension; the portable answer is
that the identifier never appears. Renaming also turned out to be strictly
better: it took the build from 36 errors to 35.

## Encoding

The sources are UTF-8 and this machine's ANSI code page is **932
(Shift_JIS)**, where the em-dash `E2 80 94` decodes as one character and the
trailing byte swallows whatever follows - frequently the newline, which
comments out the next line. `/utf-8` is mandatory for MSVC, not cosmetic. The
502 em-dashes across 129 files were also removed outright; error count was
identical before and after.

## Wrong turns worth recording

- **Codepage theory.** I attributed a ~85-error cascade to Shift_JIS line
  swallowing. `/utf-8` removed every `C4819` warning and left all 85 errors. The
  real causes were the macro collisions above.
- **"The header compiles standalone, so `WINDOWS` is not defined here."**
  True, and wrong. Preprocessing `HostModules.cpp` showed the expansion
  immediately. Header-local reasoning cannot settle a whole-TU problem.
- **"The Windows adapter is already ported."** I read the 100-line
  `WindowsIOBackend.hpp` interface and never opened the 118-line `.cpp`, which
  had `RegisterHotkey` returning `false` and no `SendInput` at all. The
  interface existing is not the implementation existing.

## Build progress

| Milestone | Distinct errors |
|---|---|
| first MSVC build | 132 |
| after excluding Qt sources with Qt6 absent | 85 |
| after `AudioBackend` / `DeviceCapabilities` renames | 50 |
| after `BTN_*` codes and `Device.hpp` | 36 |
| after replacing `push_macro` with renames | 35 |

Remaining: `DisplayManager.hpp` 15 (X11 `Display`/`Window`),
`IO.hpp` 11 (`input_event`/`gettimeofday`), `VM.hpp` 2,
`IPixelAutomation.hpp` 2, and five missing-header sites (`poll.h`,
`pthread.h`, `X11/Xlib.h`, `unistd.h`, `sys/statvfs.h`).
