# Building Havel on Windows

Status: **toolchain works, build does not complete yet.** MSVC is installed
and CMake configures cleanly; the remaining failures are real porting work,
enumerated under "What still fails to compile" below.

## Toolchain

Verified present on the machine this was developed against:

- MSVC 19.51.36260.0 (Visual Studio 18 Insiders),
  `VC/Tools/MSVC/14.51.36231`, including `vcruntime.h` and `libcpmt.lib`
- Windows SDK 10.0.26100.0, targeting Windows 10.0.22631
- CMake 4.4.4

CMake configures to completion (`CONF_EXIT=0`), resolving fmt 11.0.2 via
`FetchContent`, PCRE2 10.44 via `FetchContent`, and nlohmann_json as
multi-header. Not found, and therefore stubbed: PkgConfig, libcurl, libffi,
Minizip, OpenCV, Tesseract, Leptonica, GTest, readline, LLVM JIT, Qt6.

### Configure and build

```bat
cmake -S . -B build-msvc2 -G "Visual Studio 18 2026" ^
      -DUI_BACKEND=HEADLESS -DENABLE_QT_UI_BACKEND=OFF
cmake --build build-msvc2 --config Release --parallel 2
```

Three things about that command line are load-bearing:

- `-DUI_BACKEND=HEADLESS`. The default is `QT`, and the build hard-fails with
  "Qt6 backend requested but Qt6 not found" when Qt6 is absent. The Qt shell
  is out of scope for PLAT-001.
- `-DENABLE_QT_UI_BACKEND=OFF`. Otherwise `$<$<BOOL:...>:havel_gui>` generator
  expressions reference a target that is never created.
- `--parallel 2`. MSVC runs out of heap at 8 jobs on this machine and the
  resulting `C1060` / `MSB8071 OutOfMemoryException` failures bury the real
  error list.

There is no `ENABLE_WINDOWS` or `ENABLE_LINUX` option. Those were proposed in
the ticket, but the build keys off CMake's built-in `WINDOWS` variable, which
comes from the toolchain file. Adding options that gate a platform CMake
already knows about would be two sources of truth for one fact.

## What still fails to compile

Counted from an actual MSVC Release build, not estimated: **36 distinct
diagnostics in 9 files**, down from 132 when the port started.

1. **X11 display** - `core/display/DisplayManager.hpp` (15) uses Xlib `Display`
   and `Window`. Linux-only subsystem, needs gating.
2. **evdev input** - `core/io/IO.hpp` (11) uses `input_event` and
   `gettimeofday`. Linux-only, needs gating or a Win32 equivalent.
3. **VM** - `compiler/vm/VM.hpp` (3).
4. **Pixel automation** - `host/automation/IPixelAutomation.hpp` (2).
5. **Missing headers** - `Config.c` (`poll.h`, inotify), `LoggerC.c`
   (`pthread.h`), `BrightnessManager.hpp` (`X11/Xlib.h`),
   `ShellExecutor.cpp` (`unistd.h`), `SystemMonitorModule.cpp`
   (`sys/statvfs.h`).

Already fixed, but listed because each is invisible until you compile on
Windows: Qt GUI sources were being compiled with Qt6 absent (`_QT_MODULES`
missed every `*_gui` directory - now filtered by naming convention);
`Device.hpp` / `CallbackTypes.hpp` include `<linux/input.h>` for
constants they hand-define or never use, and the seven `BTN_*` mouse codes
are now spelled out in `Device.hpp` beside the codes already there.

## Win32 macro collisions

Windows defines names that collide with identifiers in this codebase. Every
one of these was found by the MSVC build, not by reading, and each reproduces
only in a whole-TU compile - the header compiles fine on its own, which is
exactly why they survive review.

Two were fixed with `#pragma push_macro` / `pop_macro` around the
declaration, so the Win32 macro is restored immediately afterwards and no
caller or public API name changes:

- `ERROR` - `wingdi.h` does `#define ERROR 0`, so
  `VMExecutionResult::Type::ERROR` expanded to `0` and broke the enum, taking
  every later member of `VM.hpp` with it.
- `stdout` / `stderr` - UCRT's `corecrt_wstdio.h` defines
  `#define stdout (__acrt_iob_func(1))`, so `std::string stdout;` became a
  bogus member declaration. Hits `Launcher.hpp`, `ShellExecutor.hpp` and
  `HavelDaemon.hpp`/`.cpp`.

Two more were renames rather than suppression, because the name was load
bearing elsewhere and shadowing it permanently was not an option:

- `AudioBackend::WINDOWS` - `include/types.hpp` does `#define WINDOWS 1`
  on a Windows build, and eleven real `#ifdef WINDOWS` sites depend on that
  macro, so it cannot go. The enumerator expanded to the literal `1`, which
  still parses and silently aliases the Windows backend onto `PULSE`.
  Renamed to `WASAPI`.
- `DeviceCapabilities` - the Windows SDK aliases it to
  `DeviceCapabilitiesA`, the real ANSI entry point of the Win32
  `DeviceCapabilitiesA` API. Every use became the function rather than the
  struct. Renamed to `InputDeviceCapabilities`.

`pid_t` is also absent on MSVC; it is typedef'd to `int` in
`havel_platform.h` (matching what `_getpid()` returns) rather than in each
consumer.

## Platform shim


`src/havel-lang/compiler/platform/` holds the portable primitives. Platform
*detection* is not there - it already lives in `include/havel_platform.h`,
and the shim builds on it rather than redefining it.

| Header | Provides | POSIX | Windows |
|---|---|---|---|
| `platform_posix.hpp` | header set only | - | - |
| `platform_win.hpp` | header only, `WIN32_LEAN_AND_MEAN` + `NOMINMAX` | - | `<windows.h>` |
| `time.hpp` | `monotonic_now_ns`, `realtime_now_ns`, `sleep_ns/ms`, `yield` | `std::chrono` | `std::chrono` |
| `dl.hpp` | `library_open/symbol_get/close`, `last_error_message`, RAII `library` | `dlopen` trio | `LoadLibrary` trio |
| `thread.hpp` | `set_current_thread_name`, `current_thread_id` | `pthread_setname_np` | `SetThreadDescription`, resolved dynamically |
| `file.hpp` | `list_directory`, `make_temp_directory`, `executable_path/directory` | `readdir`, `mkdtemp`, `/proc/self/exe` | `FindFirstFileW`, `GetTempPathW`, `GetModuleFileNameW` |

`platform.hpp` is an umbrella that deliberately includes only `time.hpp`.
The other three pull in `<windows.h>` on Windows, and a file that wants a
clock should not inherit that macro surface.

Three things are intentionally absent because a portable wrapper would be a
lie, not a shim:

- `mmap`/`mprotect` - different granularity, and no analogue of an
  over-committed file mapping.
- `eventfd`/`signalfd` - no file-descriptor equivalent. "Wakeup the loop"
  needs a redesign around HANDLE and `WaitForMultipleObjects`, not a wrapper.
- `fork`/`execv`/`waitpid` - `CreateProcess` is a different shape. It lives
  in `src/core/process` behind its own backend.

## Known limitations

- `havel-wm` is not ported. Windows has DWM. See `docs/windows-modules.md`.
- The `io`, `hotkey` and `brightness` modules have no Windows path yet;
  `WH_KEYBOARD_LL` and `SendInput` are deferred to their own tickets.
- No Windows CI job exists.
