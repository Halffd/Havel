---
plan name: evdev-pure-havel
plan description: Replace evdev/EventListener with pure Havel
plan status: active
---

## Idea
Replace the C++ EventListener (2125 lines), EvdevAdapter (~1000 lines), HotkeyManager (1215 lines), and X11HotkeyMonitor (233 lines) with pure Havel modules that use FFI to interact with /dev/input, uinput, and X11 directly.

Architecture:
- The existing modules/app/evdev.hv already reads /dev/input/eventX via FFI (open, read, ioctl)
- The existing modules/app/uinput.hv already creates uinput devices via FFI
- We need to build on these to create a full event loop + hotkey matching system in Havel

Phase 1: Create Havel event loop module (modules/lang/eventloop/)
- eventloop.hv: Main event loop using FFI select()/poll() on evdev fds
- Uses evdev.hv FFI for device open/read/ioctl/grab
- Uses uinput.hv FFI for event forwarding (uinput synthesis)
- Processes input_event structs from FFI reads
- Maintains key state, modifier state, active inputs in Havel data structures

Phase 2: Create Havel hotkey engine (modules/lang/hotkey/)
- hotkey.hv: Hotkey matching engine (replaces HotkeyManager.cpp)
- Register hotkeys with key+modifier patterns
- Match incoming events against registered patterns
- Support: single keys, combos, mouse buttons, wheel, gestures
- Dispatch matched hotkeys via goroutines/scheduler

Phase 3: Create Havel X11 monitor (modules/lang/x11monitor/)
- x11monitor.hv: X11-based hotkey monitoring (replaces X11HotkeyMonitor)
- Uses FFI to X11 for XGrabKey/XNextEvent
- Runs in separate goroutine

Phase 4: Create C++ shim for VM integration
- Minimal C++ shim that:
  - Exposes scheduler/executor hooks for Havel to call back into VM
  - Provides FFI bindings for select(), poll(), signalfd()
  - Registers a few host functions for the Havel event loop to hook into
  - The event loop itself runs as a Havel goroutine

Phase 5: Wire up and test
- Replace IO::eventListener creation with Havel module loading
- Replace HotkeyManager C++ usage with Havel hotkey engine
- Remove EventListener.cpp, EvdevAdapter.cpp, HotkeyManager.cpp, X11HotkeyMonitor.cpp
- Build and run hvtest --smoke

Critical constraints:
- The event loop MUST be able to call into the VM to execute hotkey callbacks
- The event loop MUST run concurrently with VM execution (goroutine or host thread)
- Hotkey dispatch MUST respect ExecutorMode (scheduler/executor/sync/thread)
- Emergency release (SIGINT/SIGTERM) MUST still work — signal handler stays C++
- X11 path stays for hotkeys that can only work via XGrabKey

## Implementation
- Create modules/lang/eventloop/evdev_reader.hv — FFI-based evdev device reader: open device, read input_event structs, parse key/mouse/wheel events, track key state and modifier state
- Create modules/lang/eventloop/uinput_writer.hv — FFI-based uinput event synthesis: create uinput device, forward non-blocked events, emergency release all keys
- Create modules/lang/eventloop/eventloop.hv — Main event loop: select/poll on evdev fds + signal fd, process events, route to hotkey engine or uinput forwarding
- Create modules/lang/hotkey/hotkey.hv — Hotkey registration and matching: register patterns, match against incoming InputEvents, support combos/wheel/gestures, dispatch callbacks
- Create modules/lang/hotkey/x11monitor.hv — X11 hotkey monitoring: XGrabKey via FFI, XNextEvent loop in goroutine, dispatch to hotkey engine
- Create C++ shim (src/modules/eventloop/EventLoopShim.cpp) — host functions for: _eventloop.select, _eventloop.signalfd, _eventloop.vmDispatch, _eventloop.getExecutorMode
- Wire Havel eventloop into IO initialization — replace IO::eventListener->Start() with loading the Havel module + starting the event loop goroutine
- Remove EventListener.cpp, EvdevAdapter.cpp, HotkeyManager.cpp, X11HotkeyMonitor.cpp from CMake build
- Build mode 9 and run hvtest --smoke to verify no regressions
- Run valgrind to check for leaks

## Required Specs
<!-- SPECS_START -->
- bootstrap-compiler
- remove-call-host
- wayland-input-protocols
<!-- SPECS_END -->