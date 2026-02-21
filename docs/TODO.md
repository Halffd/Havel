# Havel TODO

## Missing Features

### Core Features
- [ ] Complete Wayland support for Window/WindowManager (currently placeholders)
- [ ] Implement multi-hotkey declaration support (e.g., `H => F5 => audio.mute()`)
- [ ] Full mode system support for ConditionSystem
- [ ] Help builtin supporting all modules/functions documentation/args
- [ ] Window Show/Hide methods implementation
- [ ] Improve screenshot manager, with io hotkeys, screenshot view window and current monitor screenshot
commit

### Havel Language
- [ ] LLVM JIT compilation support
- [ ] More comprehensive error messages with line numbers
- [ ] Debugger integration
- [ ] Language server protocol (LSP) support
- [ ] More standard library functions

### Build System
- [ ] Fix linker warnings for readline library conflicts
- [ ] Optimize build times
- [ ] Better dependency management

### Documentation
- [ ] Complete API documentation
- [ ] More code examples
- [ ] Tutorial series
- [ ] Video demonstrations

### Testing
- [ ] Increase test coverage
- [ ] Integration tests
- [ ] Performance benchmarks
- [ ] Stress testing

### GUI
- [ ] More themes and customization
- [ ] Better error reporting in GUI
- [ ] Configuration editor
- [ ] Profile management UI

### Platform Support
- [ ] Better multi-monitor support
- [ ] Touchscreen support
- [ ] Gamepad/controller support
- [ ] More window manager compatibility

## Known Issues
- Segfaults in certain edge cases (see IMPLEMENTATION_STATUS.md)
- Some X11 event handling race conditions
- Memory leaks in long-running sessions (needs profiling)

## Future Enhancements
- Plugin system
- Remote control via network
- Cloud sync for configurations
- AI-powered automation suggestions
- Voice control integration
