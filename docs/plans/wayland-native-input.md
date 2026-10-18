---
plan name: wayland-native-input
plan description: Native Wayland input protocols
plan status: active
---

## Idea
Implement native Wayland protocol-based input simulation (keyboard, mouse, clipboard, window listing) to replace the current uinput-only approach and subprocess-based clipboard. Add 4 Wayland protocols: virtual-keyboard-unstable-v1, wlr-virtual-pointer-unstable-v1, ext-data-control-v1, and wlr-foreign-toplevel-unstable-v1. Create a shared WaylandProtocolClient that manages the display connection and registry global binding. Create VirtualKeyboard, VirtualPointer, WaylandClipboardBackend, and ForeignToplevel classes that use these protocols. Keep uinput as fallback when protocols are unavailable. Update WaylandIOBackend to use VirtualKeyboard/VirtualPointer instead of raw uinput. Update Clipboard to use WaylandClipboardBackend instead of popen("wl-copy"/"wl-paste"). Update WaylandBackend window listing to use ForeignToplevel when available.

## Implementation
- 1. Add protocol XML files to protocols/wlr/ (virtual-keyboard, virtual-pointer, foreign-toplevel) and update CMakeLists.txt with wayland-scanner generation + xkbcommon dependency. Add generated sources to wayland-protos library.
- 2. Create src/core/wayland/WaylandProtocolClient.hpp/.cpp — shared wl_display connection, wl_registry global binding, compositor capability flags (hasVirtualKeyboard, hasVirtualPointer, hasDataControl, hasForeignToplevel). Model after BrightnessManager's Wayland setup but as a reusable singleton.
- 3. Create src/core/wayland/VirtualKeyboard.hpp/.cpp — uses zwp_virtual_keyboard_v1 to send key events. Keymap setup via xkbcommon (compile default keymap to memfd, pass fd to protocol). PressKey/ReleaseKey using protocol key() request. Modifier tracking. Fallback to UinputDevice.
- 4. Create src/core/wayland/VirtualPointer.hpp/.cpp — uses zwlr_virtual_pointer_v1 for mouse events. Relative motion via motion(), absolute via motion_absolute(), buttons via button(), scroll via axis()+frame(). Fallback to UinputDevice.
- 5. Create src/core/wayland/WaylandClipboardBackend.hpp/.cpp — implements IClipboardBackend using ext_data_control_v1 (primary) or zwr_data_control_v1 (fallback). getText/setText via direct Wayland data offer/source, no subprocess. Replace Clipboard.cpp's getTextWayland/setTextWayland popen calls.
- 6. Create src/core/wayland/ForeignToplevel.hpp/.cpp — uses zwlr_foreign_toplevel_v1 for window enumeration. Maintains live window list with titles, app-ids, states (active/fullscreen/maximized). Supplements WaylandBackend's hyprctl/swaymsg parsing for window listing.
- 7. Update WaylandIOBackend to use VirtualKeyboard and VirtualPointer instead of raw uinput events. Auto-detect available protocols and fall back to uinput when compositor doesn't support them. Implement MovePointerTo via virtual pointer absolute motion.
- 8. Build, fix compilation errors, run bytecode smoke test and existing test suite to verify no regressions.

## Required Specs
<!-- SPECS_START -->
- wayland-input-protocols
- remove-call-host
- wayland-havel-api
<!-- SPECS_END -->