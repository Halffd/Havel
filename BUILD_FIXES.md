# HvC Project Build Fixes

This document outlines the issues fixed to make the HvC project build successfully.

## Fixed Issues

1. **DisplayManager Type References**
   - Fixed `Display*` and `Window` type reference issues in `DisplayManager.cpp` and `DisplayManager.hpp`
   - Added proper X11 includes and fixed static member variable definitions

2. **MPVController Method Signatures**
   - Fixed method signatures in `MPVController.cpp` and `MPVController.hpp` to match
   - Updated `Initialize()` to return a bool instead of void
   - Changed `SendCommand` to accept a vector of strings instead of a single string

3. **Window Class X11 Integration**
   - Fixed `Window` class to properly work with X11 `Window` type
   - Added proper type casting between `havel::Window`, `havel::wID`, and X11's `Window`
   - Defined the `display` variable in `Window.cpp`

4. **IO Class Implementation**
   - Created the missing `IO.cpp` file with implementation of all required methods
   - Added proper destructor for the `IO` class to fix linker errors
   - Added stub implementations for all interface methods

## Remaining Issues

The application can now build successfully, but there are still runtime issues:

1. The application crashes when handling Ctrl+C signals
2. Some hotkey configurations show "Invalid key name" warnings
3. Missing configuration file: "Could not open config file: config/config.json"

## Project Structure Notes

- All X11 display management is now handled through the `DisplayManager` class
- The project is designed to work on Linux with X11 (Wayland support is placeholder)
- The application uses a hotkey system for controlling media playback and window management

## Building the Project

To build the project:

```bash
mkdir build && cd build
cmake ..
cmake --build .
``` 