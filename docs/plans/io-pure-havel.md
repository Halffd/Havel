---
plan name: io-pure-havel
plan description: Convert IO module to pure Havel
plan status: done
---

## Idea
Replace C++ IOService.cpp (237 lines) and IOModule.cpp (340 lines) with a pure-Havel io.hv sidecar. The C++ IO facade (IO.hpp/cpp, IOBackend, EventListener, etc.) stays permanently — it's the native system layer. The IOService was just a thin wrapper delegating to havel::IO* methods, and IOModule was just a Value→C++ type adapter. Both can be replaced by a Havel module that exposes the same 28 functions via the existing host function system, OR by using FFI to call the IO methods directly. 

Strategy: Keep a minimal C++ shim that exposes havel::IO* methods as individual host functions (io._sendKeys, io._sendKey, etc.) — but move ALL the business logic (key string parsing, executor mode mapping, double-click timing, error handling, null-safety) into pure Havel. The C++ shim becomes pure passthrough with no logic.

Actually, a better approach: the existing modules/app/ sidecars already have FFI-based keyboard.hv, mouse.hv, uinput.hv, evdev.hv. The io.hv should WRAP those existing sidecars into a unified `io` namespace that matches the current C++ IOModule API. This way:
1. io.sendKeys → delegates to keyboard.type or X11 FFI
2. io.mouseClick → delegates to mouse.click  
3. io.isKeyPressed → delegates to keyboard.isDown
4. etc.

But this creates two IO systems. The cleaner approach is:
- Keep the C++ IO* object alive (it's created by the app init and manages device state)
- Expose its methods as thin host functions (already done via IOModule)
- Replace the IOService+IOModule C++ code with a pure Havel io.hv that calls the host functions

Wait — the host functions ARE registered by IOModule.cpp. If we delete IOModule.cpp, we lose the host function registrations. So the approach must be:
1. Create a minimal C++ IOShim that ONLY registers raw passthrough host functions (io._send, io._keyDown, io._keyUp, io._mouseMove, io._mouseClick, etc.) — no business logic, no IOService
2. Create modules/app/io.hv that wraps the shim functions with the full API (sendKeys parsing, error handling, default args, executor mode strings)
3. Delete IOService.hpp/cpp and IOModule.cpp
4. IOShim.cpp replaces IOModule.cpp in CMake

The shim functions call havel::IO* methods directly — no IOService middleman. The Havel sidecar adds all the nice API sugar.

## Implementation
- Create src/modules/io/IOShim.cpp — minimal C++ shim that registers raw passthrough host functions calling havel::IO* methods directly (io._send, io._sendKeys, io._keyDown, io._keyUp, io._mouseMove, io._mouseClick, io._scroll, io._getMousePos, io._isKeyPressed, io._getModifiers, io._emergencyRelease, io._ungrabAll, io._suspend, io._resume, io._isSuspended, io._setExecutorMode, io._getExecutorMode, io._map, io._remap, io._setMouseSensitivity, io._getMouseSensitivity, io._mouseDown, io._mouseUp)
- Create modules/app/io.hv — pure Havel IO module that wraps the raw shim functions into the public io.* API with proper arg handling, defaults, error catching, and the io namespace object. Functions: sendKeys, sendKey, keyDown, keyUp, map, remap, block, unblock, suspend, resume, isSuspended, grab, ungrab, emergencyRelease, isKeyPressed, isShiftPressed, isCtrlPressed, isAltPressed, isWinPressed, getCurrentModifiers, mouseMove, mouseMoveTo, mouseClick, mouseDoubleClick, mousePress, mouseRelease, scroll, getMousePosition, setMouseSensitivity, getMouseSensitivity
- Update CMakeLists.txt — replace IOModule.cpp with IOShim.cpp, remove IOService from build if it was a separate target
- Delete src/host/io/IOService.hpp and src/host/io/IOService.cpp
- Delete src/modules/io/IOModule.cpp and src/modules/io/IOModule.hpp
- Update ServiceRegistry references — remove IOService registration if it was registered there
- Build and run smoke tests to verify no regressions
- Run valgrind to verify no new leaks

## Required Specs
<!-- SPECS_START -->
- bootstrap-compiler
- remove-call-host
<!-- SPECS_END -->