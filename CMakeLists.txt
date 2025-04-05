cmake_minimum_required(VERSION 3.10)
project(HvC)

# Find required packages
find_package(X11 REQUIRED)
find_package(Lua REQUIRED)

# Check for XRandR extension for multi-monitor support
find_package(X11 COMPONENTS Xrandr)
if(X11_Xrandr_FOUND)
    add_compile_definitions(HAVE_XRANDR)
    message(STATUS "XRandR found, enabling multi-monitor support")
endif()

# Add include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/include
    ${LUA_INCLUDE_DIR}
)

# Add source files for main executable
set(MAIN_SOURCES
    src/main.cpp
    src/core/DisplayManager.cpp
    src/core/HotkeyManager.cpp
    src/core/IO.cpp
    src/core/MPVController.cpp
    src/core/ScriptEngine.cpp
    src/core/SocketServer.cpp
    src/core/BrightnessManager.cpp
    src/window/CompizManager.cpp
    src/window/Window.cpp
    src/window/WindowManager.cpp
    src/window/WindowManagerDetector.cpp
    src/window/WindowMonitor.cpp
    src/utils/Logger.cpp
    src/utils/Utils.cpp
    src/utils/Printer.cpp
    src/utils/Util.cpp
)

# Add header files
set(HEADERS
    src/core/DisplayManager.hpp
    src/core/HotkeyManager.hpp
    src/core/IO.hpp
    src/core/MPVController.hpp
    src/core/ScriptEngine.hpp
    src/core/SocketServer.hpp
    src/core/BrightnessManager.hpp
    src/window/CompizManager.hpp
    src/window/Window.hpp
    src/window/WindowManager.hpp
    src/window/WindowManagerDetector.hpp
    src/window/WindowMonitor.hpp
    src/utils/Logger.hpp
    src/utils/Utils.hpp
    src/utils/Printer.hpp
    src/utils/Util.hpp
)

# Create main executable
add_executable(hvc ${MAIN_SOURCES} ${HEADERS})

# Link libraries for main executable
target_link_libraries(hvc
    ${X11_LIBRARIES}
    ${X11_Xtst_LIB}
    ${LUA_LIBRARIES}
    pthread
)

# Add XRandR library if found
if(X11_Xrandr_FOUND)
    target_link_libraries(hvc ${X11_Xrandr_LIB})
endif()

# Create test executable
add_executable(hotkey_test src/tests/hotkey_test.cpp ${MAIN_SOURCES} ${HEADERS})

# Add RUN_TESTS definition for test executable
target_compile_definitions(hotkey_test PRIVATE RUN_TESTS)

# Link libraries for test executable
target_link_libraries(hotkey_test
    ${X11_LIBRARIES}
    ${X11_Xtst_LIB}
    ${LUA_LIBRARIES}
    pthread
)

# Add XRandR library if found
if(X11_Xrandr_FOUND)
    target_link_libraries(hotkey_test ${X11_Xrandr_LIB})
endif()

# Install targets
install(TARGETS hvc hotkey_test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)