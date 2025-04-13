# HvC Project Rules and Guidelines

## Project Architecture

### Core Components
1. **Core System (`src/core/`)**
   - Hotkey management system
   - Input/Output handling
   - Configuration management
   - Script engine integration

2. **Window Management (`src/window/`)**
   - Window detection and manipulation
   - Multi-monitor support
   - Window state tracking

3. **Media Control (`src/media/`)**
   - MPV controller integration
   - Media hotkey handling
   - Audio/video control

4. **GUI System (`src/gui/`)**
   - GTK-based interface
   - Theme management
   - Window management UI

5. **Utility Layer (`src/utils/`)**
   - Logging system
   - Common utilities
   - Helper functions

### Design Patterns
1. **Singleton Pattern**
   - Used for global managers (e.g., `WindowManager`, `HotkeyManager`)
   - Implemented using static instance methods
   - Thread-safe initialization

2. **Observer Pattern**
   - Used for event handling
   - Callback-based notification system
   - Event queue management

3. **Factory Pattern**
   - Used for creating window objects
   - Platform-specific implementations
   - Abstract base classes

## Naming Conventions

### Files and Directories
- Use PascalCase for file names: `HotkeyManager.cpp`
- Header files use `.hpp` extension
- Source files use `.cpp` extension
- Test files prefix with `Test`: `TestHotkey.cpp`

### Classes and Types
- PascalCase for class names: `HotkeyManager`
- CamelCase for method names: `registerHotkey()`
- UPPER_CASE for constants: `MAX_HOTKEYS`
- Prefix member variables with `m_`: `m_hotkeys`
- Prefix static members with `s_`: `s_instance`

### Namespaces
- Use `H` as the root namespace
- Nested namespaces for subsystems: `H::Window`, `H::Media`

## Dependencies and Imports

### External Libraries
1. **X11**
   - Required for window management
   - Used for input handling
   - Multi-monitor support

2. **GTK**
   - GUI implementation
   - Theme management
   - Window creation

### Internal Dependencies
- Core components depend on utils
- GUI depends on window management
- Media control depends on hotkey system

## Code Organization

### Header Files
```cpp
#pragma once

// System includes
#include <string>
#include <memory>

// Project includes
#include "utils/Logger.hpp"
#include "core/ConfigManager.hpp"

namespace H {
    class ClassName {
    public:
        // Public interface
        static ClassName& Instance();
        
    private:
        // Private implementation
        struct Impl;
        std::unique_ptr<Impl> pImpl;
    };
}
```

### Source Files
```cpp
#include "ClassName.hpp"
#include "utils/Logger.hpp"

namespace H {
    struct ClassName::Impl {
        // Implementation details
    };
    
    ClassName& ClassName::Instance() {
        static ClassName instance;
        return instance;
    }
}
```

## Hotkey System Rules

### Hotkey Registration
1. Use `AddHotkey` for global hotkeys
2. Use `AddContextualHotkey` for window-specific hotkeys
3. Format: `modifier+key` (e.g., `alt+shift+z`)
4. Support multiple modifiers

### Window Conditions
1. Format: `Window.Active('title:WindowName')`
2. Format: `Window.Active('class:ClassName')`
3. Support both title and class matching

## Logging Guidelines

### Log Levels
1. `FATAL` - Critical errors
2. `ERROR` - Recoverable errors
3. `WARNING` - Potential issues
4. `INFO` - General information
5. `DEBUG` - Detailed debugging
6. `TRACE` - Very detailed tracing

### Log Format
```cpp
lo.info("Operation completed: " + std::to_string(count));
lo.error("Failed to initialize: " + errorMessage);
```

## Build System

### CMake Configuration
1. Use modern CMake practices
2. Include all necessary dependencies
3. Set appropriate compiler flags
4. Handle platform-specific code

### Build Scripts
1. `build.sh` - Main build script
2. `setup.sh` - Environment setup

## Testing Guidelines

### Unit Tests
1. Place in `src/tests/` directory
2. Use descriptive test names
3. Test both success and failure cases
4. Include edge cases

### Integration Tests
1. Test component interactions
2. Verify system behavior
3. Check performance metrics

## Documentation

### Code Comments
1. Use Doxygen-style comments for public APIs
2. Document complex algorithms
3. Explain non-obvious code
4. Keep comments up-to-date

### README Files
1. Include setup instructions
2. Document configuration options
3. List known issues
4. Provide usage examples

## Version Control

### Git Workflow
1. Use feature branches
2. Write descriptive commit messages
3. Review changes before merging
4. Keep master branch stable
