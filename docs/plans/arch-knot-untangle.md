---
plan name: arch-knot-untangle
plan description: Untangle architectural knots
plan status: done
---

## Idea
The module survey revealed that the real problem isn't converting C++ to Havel line-by-line — it's that the architecture has three systemic diseases:

1. **Dead ServiceRegistry entries**: 7 of 18 registered services are never retrieved (HotkeyService, WindowService, ProcessService, BrowserService, TimerService, NetworkService, TextChunkerService). They're declared, registered, and never used — each module bypasses ServiceRegistry with direct singletons or static methods.

2. **ModeModule is 100% redundant**: ModeModule.cpp (87L) exposes 3 functions via ServiceRegistry→ModeService→ModeManager. But ModeBridge in ModularHostBridges.cpp already exposes 7 functions directly (mode.register, mode.current, mode.previous, mode.set, mode.list, mode.time, mode.transitions). Worse: ModeModule's `mode.setMode()` is buggy — it doesn't call `HotkeyManager::setMode()` like ModeBridge's `mode.set()` does. ModeService (91L) is a thin wrapper with zero additional logic. Both should be deleted.

3. **HotkeyModule is the largest architectural knot**: 1188 lines, 50 prototype methods on a `Hotkey` object, all backed by a `g_hotkeyContexts` static map that shadows state already in HotkeyManager/Scheduler. Only ~6 methods actually need C++ (X11 grab/ungrab, scheduler wake/unpark/policy). The other ~44 are data operations on HotkeyContextData that could be a Havel `Hotkey` class. The condition evaluation logic (which was partially cleaned up) still has the `HotkeyConditionCompiler` as a no-op stub.

The goal: Delete dead code, fix the redundant access paths, and establish the C++ = event source / Havel = behavior pattern that the sidecar system already supports.

Specific actions:
- Delete ModeModule.cpp + ModeService.hpp/.cpp (replaced by ModeBridge)
- Delete 7 dead ServiceRegistry entries from HostModules.cpp
- Gut HotkeyModule.cpp to a thin C++ shim (register only the 6 OS-bridge methods as `hotkey._*`), create `modules/app/hotkey.hv` sidecar with Hotkey class
- Delete HotkeyConditionCompiler.hpp/.cpp (no-op stubs)
- Delete TimerService.hpp/.cpp (registered but never used; TimerModule is pure VM dispatch)
- Convert StringModule to thin C++ shim (only `fromCodePoint` + regex `replace` stay; 13 duplicates removed)
- Convert BitModule to thin C++ shim (only 4 CPU intrinsics stay; 14 bitwise ops move to sidecar)
- Convert TypeModule to thin C++ shim (only 4 enum intrinsics stay; 6 type checks move to sidecar)
- Fix ServiceRegistry: consolidate to only services that are actually retrieved

## Implementation
- Delete ModeModule.cpp + ModeService.hpp/.cpp + dead stub in src/modules/hotkey/HotkeyModule.hpp
- Remove 7 dead ServiceRegistry declarations and registrations from HostModules.cpp (HotkeyService, WindowService, ProcessService, BrowserService, TimerService, NetworkService, TextChunkerService)
- Delete TimerService.hpp/.cpp (registered but never used by TimerModule)
- Delete HotkeyConditionCompiler.hpp/.cpp (no-op stubs from previous cleanup)
- Gut HotkeyModule.cpp to thin C++ shim: only keep 6 OS-bridge methods as hotkey._*, remove g_hotkeyContexts and 44 data-operation prototype methods
- Create modules/app/hotkey.hv sidecar with Hotkey class wrapping hotkey._* shim
- Convert StringModule.cpp to thin C++ shim: keep only string._fromCodePoint and string._regexReplace, remove 13 duplicate prototype methods
- Create modules/app/string.hv sidecar with string helpers that use StringPrototype methods
- Convert BitModule.cpp to thin C++ shim: keep only bit._popcount, bit._ctz, bit._clz, bit._parity (4 CPU intrinsics)
- Create modules/app/bit.hv sidecar with 14 bitwise operations using Havel &|^~<<>> operators
- Convert TypeModule.cpp to thin C++ shim: keep only type._isEnum, type._newEnum, type._getVariant, type._getVariantPayload (4 VM intrinsics)
- Create modules/app/type.hv sidecar with isNumber/isString/isArray/isObject/isNull/isBoolean using type() builtin
- Verify all changes compile (debug + release) and pass smoke tests

## Required Specs
<!-- SPECS_START -->
- bootstrap-compiler
- remove-call-host
- wayland-input-protocols
- shim-sidecar-pattern
<!-- SPECS_END -->