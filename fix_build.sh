#!/bin/bash

# Script to fix build issues in HvC

echo "HvC Build Fix Script"
echo "===================="

# Step 1: Fix the Sol2 library
echo "Checking for Sol2 library..."
if [ ! -d "include/sol2" ]; then
    echo "Installing Sol2 library..."
    git clone https://github.com/ThePhD/sol2.git -b v3.3.0 include/sol2
    
    # Update ScriptEngine.hpp to use the correct include path
    echo "Updating ScriptEngine.hpp include path..."
    if [ -f "src/core/ScriptEngine.hpp" ]; then
        sed -i 's/#include <sol\/sol.hpp>/#include <sol2\/sol.hpp>/' src/core/ScriptEngine.hpp
    fi
else
    echo "Sol2 library already installed"
fi

# Step 2: Fix types.hpp conflicts
echo "Fixing type definition conflicts..."
mkdir -p src/common

# Create combined types.hpp file in common directory
cat > src/common/types.hpp << 'EOF'
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace havel {

// Type aliases
using str = std::string;
using cstr = const std::string&;
using wID = unsigned long;
using pID = unsigned long;

// Group type definition
using group = std::unordered_map<std::string, std::vector<std::string>>;

// Process method enum
enum class ProcessMethod {
    Invalid = -1,
    WaitForTerminate = 0,
    ForkProcess = 1,
    ContinueExecution = 2,
    WaitUntilStarts = 3,
    CreateNewWindow = 4,
    AsyncProcessCreate = 5,
    SystemCall = 6,
    SameWindow = 7,
    Shell = 8
};

// Helper function to convert string to lowercase
inline std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Common typedefs for key handling
#ifndef Key_typedef
#define Key_typedef
typedef unsigned long Key;
#endif

// Helper struct for Windows EnumWindows
#ifdef _WIN32
struct EnumWindowsData {
    std::string targetProcessName;
    wID id = 0;
    
    explicit EnumWindowsData(const std::string& name) : targetProcessName(name) {}
};
#endif

} // namespace havel
EOF

# Fix window/types.hpp to include the common types
echo "Updating window/types.hpp..."
if [ -f "src/window/types.hpp" ]; then
    cat > src/window/types.hpp << 'EOF'
#pragma once
#include "../common/types.hpp"

// This file now just includes the common types.hpp
// All type definitions are centralized in src/common/types.hpp
EOF
fi

# Step 3: Fix DisplayManager static initialization
echo "Fixing DisplayManager.hpp static initialization..."
if [ -f "src/core/DisplayManager.hpp" ]; then
    # Ensure DisplayManager.hpp has proper static initialization
    grep -q "Display\* DisplayManager::display = nullptr;" src/core/DisplayManager.hpp
    if [ $? -ne 0 ]; then
        # Add static initialization if missing
        echo "// Static member initialization" >> src/core/DisplayManager.hpp
        echo "Display* DisplayManager::display = nullptr;" >> src/core/DisplayManager.hpp
        echo "Window DisplayManager::root = 0;" >> src/core/DisplayManager.hpp
    fi
fi

# Step 4: Fix IO.hpp to declare all methods
echo "Checking IO.hpp for missing declarations..."
if [ -f "src/core/IO.hpp" ]; then
    grep -q "void PressKey(const std::string& keyName, bool press);" src/core/IO.hpp
    if [ $? -ne 0 ]; then
        # Add missing method declarations
        echo "Updating IO.hpp with missing method declarations..."
        sed -i '/\/\/ Key methods/a\    void PressKey(const std::string& keyName, bool press);' src/core/IO.hpp
    fi
    
    # Add display and root as member variables if missing
    grep -q "Display\* display;" src/core/IO.hpp
    if [ $? -ne 0 ]; then
        echo "Adding display and root member variables to IO class..."
        sed -i '/private:/a\    Display* display{nullptr};\n    Window root{0};' src/core/IO.hpp
    fi
fi

# Step 5: Create include for all X11 headers
echo "Creating X11 includes file..."
mkdir -p include/x11
cat > include/x11/x11_all.hpp << 'EOF'
#pragma once

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/XF86keysym.h>
#endif

namespace havel {
    // Forward declarations of common X11 types to avoid conflicts
    #ifdef __linux__
    extern Display* GetDisplay();
    extern Window GetRootWindow();
    #endif
}
EOF

echo ""
echo "Build fixes applied. Please rebuild your project:"
echo ""
echo "mkdir -p build && cd build && cmake .. && cmake --build ."
echo ""
echo "If you still encounter issues, please see BUILD_ISSUES.md for detailed solutions." 