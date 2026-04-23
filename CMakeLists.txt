cmake_minimum_required(VERSION 3.16)
project(Havel VERSION 1.0.0 LANGUAGES C CXX)

# ==============================================================================
# BUILD OPTIONS - Define these FIRST before any logic
# ==============================================================================
option(USE_CLANG "Force use of Clang compiler" ON)
option(ENABLE_LLVM "Enable LLVM JIT compilation" OFF)
option(ENABLE_TESTS "Enable building tests" ON)
option(ENABLE_HAVEL_LANG "Enable Havel language compilation" ON)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

# UI Backend Options
set(UI_BACKEND "QT" CACHE STRING "UI Backend to use (QT, GTK, IMGUI, AUTO)")
set_property(CACHE UI_BACKEND PROPERTY STRINGS QT GTK IMGUI AUTO)
option(ENABLE_GTK "Enable GTK4 native backend" OFF)
option(ENABLE_IMGUI "Enable Dear ImGui native backend" OFF)
option(ENABLE_QT "Enable Qt6 native backend (default)" ON)

# ==============================================================================
# COMPILER AND STANDARDS CONFIGURATION
# ==============================================================================
# Validate compiler choice
if(USE_CLANG AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING 
        "USE_CLANG is ON but compiler is ${CMAKE_CXX_COMPILER_ID}. "
        "Set CC=clang CXX=clang++ before running cmake for best results."
    )
endif()

# Language standards
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-fuse-ld=lld -Wl,--as-needed -Wl,--gc-sections -Wl,-z,nocopyreloc")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto=auto -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden -Wno-nan-infinity-disabled -fabi-version=14 -fno-gnu-unique")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto=auto -Wl,--as-needed -Wl,--gc-sections -Wl,-z,nocopyreloc")
endif()

# LTO configuration - use thin LTO for faster builds, disable for debug
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_OUTPUT)
if(LTO_SUPPORTED AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
    message("Using ccache")
endif()
# ==============================================================================
# DEPENDENCY VALIDATION
# ==============================================================================
# Critical: LLVM requires Havel Lang
if(ENABLE_LLVM AND NOT ENABLE_HAVEL_LANG)
    message(STATUS 
        "LLVM JIT compilation requires Havel Language support. "
        "Automatically enabling ENABLE_HAVEL_LANG."
    )
    set(ENABLE_HAVEL_LANG ON CACHE BOOL "Auto-enabled for LLVM dependency" FORCE)
endif()

# ==============================================================================
# REQUIRED SYSTEM DEPENDENCIES
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Optional: GNU Readline for REPL line-editing/history
find_library(READLINE_LIB readline)
if(READLINE_LIB)
    message(STATUS "Readline:       ✓ ENABLED")
else()
    message(STATUS "Readline:       ✗ NOT FOUND (REPL falls back to std::getline)")
endif()

# D-Bus
pkg_check_modules(DBUS REQUIRED dbus-1)

# PipeWire
pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire:        ✓ ENABLED (${PIPEWIRE_VERSION})")
else()
    message(STATUS "PipeWire:        ✗ NOT FOUND (Falling back to PulseAudio/ALSA)")
endif()

# Lua (try 5.4 first, fallback to pkg-config)
find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    pkg_search_module(LUA REQUIRED lua5.4 lua-5.4 lua54 lua)
    if(NOT LUA_FOUND)
        message(FATAL_ERROR "Lua 5.4 not found. Install lua5.4-dev or lua-devel")
    endif()
endif()

# libffi (for FFI - Foreign Function Interface)
set(HAVE_LIBFFI 0)
pkg_check_modules(LIBFFI QUIET libffi)
if(LIBFFI_FOUND)
    message(STATUS "libffi:       ✓ FOUND (${LIBFFI_VERSION})")
    set(HAVE_LIBFFI 1 CACHE BOOL "libffi available")
else()
    message(STATUS "libffi:       ✗ NOT FOUND (FFI will be limited)")
    set(HAVE_LIBFFI 0 CACHE BOOL "libffi available")
endif()

# X11 extensions
find_library(XTEST_LIB Xtst REQUIRED)
find_library(XRANDR_LIB Xrandr REQUIRED)  
find_library(XINERAMA_LIB Xinerama REQUIRED)
pkg_check_modules(XCOMPOSITE REQUIRED xcomposite)

if(NOT XTEST_LIB OR NOT XRANDR_LIB OR NOT XINERAMA_LIB)
    message(FATAL_ERROR "Missing X11 extension libraries. Install libxtst-dev libxrandr-dev libxinerama-dev")
endif()

# ==============================================================================
# WAYLAND SUPPORT
# ==============================================================================
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-protocols)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Protocol generation setup
set(PROTOCOLS_DIR /usr/share/wayland-protocols)
set(GENERATED_PROTO_DIR ${CMAKE_BINARY_DIR}/generated/wayland)
file(MAKE_DIRECTORY ${GENERATED_PROTO_DIR})

# Gamma control protocol
set(GAMMA_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-gamma-control-unstable-v1.xml)
if(NOT EXISTS ${GAMMA_XML})
    message(FATAL_ERROR "Gamma control protocol not found: ${GAMMA_XML}")
endif()

set(GAMMA_HEADERS
    ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-client-protocol.h
    ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-protocol.c
)

add_custom_command(
    OUTPUT ${GAMMA_HEADERS}
    COMMAND ${WAYLAND_SCANNER} client-header ${GAMMA_XML} 
        ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${GAMMA_XML} 
        ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-protocol.c
    DEPENDS ${GAMMA_XML}
    COMMENT "Generating gamma control protocol headers"
)

# XDG output protocol
set(XDG_XML ${PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml)
if(NOT EXISTS ${XDG_XML})
    message(FATAL_ERROR "XDG output protocol not found: ${XDG_XML}")
endif()

set(XDG_HEADERS
    ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-client-protocol.h
    ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-protocol.c
)

add_custom_command(
    OUTPUT ${XDG_HEADERS}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_XML} 
        ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_XML} 
        ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-protocol.c
    DEPENDS ${XDG_XML}
    COMMENT "Generating XDG output protocol headers"
)

# Create wayland protocols library
add_library(wayland-protos STATIC ${GAMMA_HEADERS} ${XDG_HEADERS})
target_include_directories(wayland-protos PUBLIC ${GENERATED_PROTO_DIR})
target_link_libraries(wayland-protos PUBLIC ${WAYLAND_LIBRARIES})

# ==============================================================================
# QT6 GUI FRAMEWORK (CONDITIONAL - Default)
# ==============================================================================
if(ENABLE_QT AND UI_BACKEND MATCHES "^(QT|AUTO)$")
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON) 
    set(CMAKE_AUTOUIC ON)

    find_package(Qt6 COMPONENTS Core Widgets Gui Charts Test)
    
    if(Qt6_FOUND)
        # Handle X11/Qt keyword conflicts in MOC
        set(CMAKE_AUTOMOC_MOC_OPTIONS 
            "-DMOC_PARSING"
            "-DNone=0L"
            "-DTrue=1"
            "-DFalse=0"
            "-DStatus=int"
            "-DBool=int" 
            "-DSuccess=0"
        )
        set(QT_BACKEND_AVAILABLE TRUE)
        message(STATUS "Qt6 backend:     ✓ AVAILABLE")
    else()
        set(QT_BACKEND_AVAILABLE FALSE)
        message(STATUS "Qt6 backend:     ✗ NOT FOUND")
        if(UI_BACKEND STREQUAL "QT")
            message(FATAL_ERROR "Qt6 backend requested but Qt6 not found")
        endif()
    endif()
else()
    set(QT_BACKEND_AVAILABLE FALSE)
endif()

# ==============================================================================
# TESTING FRAMEWORK (OPTIONAL)
# ==============================================================================
if(ENABLE_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        message(STATUS "✓ GTest found - tests will be built")
    else()
        message(STATUS "⚠ GTest not found - tests will be skipped")
        set(ENABLE_TESTS OFF)
    endif()
endif()

# ==============================================================================
# LLVM JIT COMPILATION (OPTIONAL)
# ==============================================================================
if(ENABLE_LLVM AND ENABLE_HAVEL_LANG)
    message(STATUS "Searching for LLVM...")
    find_package(LLVM REQUIRED CONFIG)
    
    if(LLVM_FOUND)
        message(STATUS "✓ LLVM ${LLVM_PACKAGE_VERSION} found")
        
        # Use llvm-config directly for library information - it handles all dependencies
        execute_process(
            COMMAND llvm-config --libdir
            OUTPUT_VARIABLE LLVM_LIBDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        
        execute_process(
            COMMAND llvm-config --libs all
            OUTPUT_VARIABLE LLVM_LINKFLAGS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        
        execute_process(
            COMMAND llvm-config --system-libs
            OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        
        # Combine all libraries - this returns raw flags like "-lLLVMCore -lLLVMSupport ..."
        set(LLVM_LIBS "${LLVM_LINKFLAGS} ${LLVM_SYSTEM_LIBS}")
        
        # Add LLVM library search directory to linker search path
        if(LLVM_LIBDIR)
            link_directories(${LLVM_LIBDIR})
            message(STATUS "  LLVM lib directory: ${LLVM_LIBDIR}")
        endif()
        
        message(STATUS "  LLVM link flags: ${LLVM_LINKFLAGS}")
        
        
    else()
        message(FATAL_ERROR 
            "LLVM not found but ENABLE_LLVM=ON. Install LLVM development packages:\n"
            "  Ubuntu/Debian: apt install llvm-dev libllvm-dev\n" 
            "  Fedora/RHEL: dnf install llvm-devel\n"
            "  Arch: pacman -S llvm\n"
            "Or disable with -DENABLE_LLVM=OFF"
        )
    endif()
else()
    message(STATUS "LLVM JIT compilation disabled")
endif()

# ==============================================================================
# SOURCE FILE COLLECTION
# ==============================================================================
# ==============================================================================
# CORE sources (always required)
file(GLOB_RECURSE CORE_SOURCES
    "src/core/*.cpp"
    "src/core/io/*.cpp"
    "src/core/net/*.cpp"
    "src/core/process/*.cpp"
    "src/extensions/ExtensionLoader.cpp"
    # "src/havel-lang/compiler/bytecode/HavelAPI.cpp"  # C API - disabled
    # "src/havel-lang/compiler/bytecode/HavelValue.cpp"  # C API - disabled
    "src/host/app/*.cpp"
    "src/host/async/*.cpp"
    "src/host/audio/*.cpp"
    "src/host/automation/*.cpp"
    "src/host/brightness/*.cpp"
    "src/host/browser/*.cpp"
    "src/host/chunker/*.cpp"
    "src/host/clipboard/*.cpp"
    "src/host/filesystem/*.cpp"
    "src/host/hotkey/*.cpp"
    "src/host/io/*.cpp"
    "src/host/media/*.cpp"
    "src/host/module/*.cpp"
    "src/host/mouse/*.cpp"
    "src/host/mode/*.cpp"
    "src/host/network/*.cpp"
    "src/host/process/*.cpp"
    "src/host/screenshot/*.cpp"
    "src/host/timer/*.cpp"
    "src/host/ui/*.cpp"
    "src/host/window/*.cpp"
    "src/window/*.cpp"
    "src/utils/*.cpp"
    "src/fs/*.cpp"
    "src/process/*.cpp"
    "src/media/*.cpp"
    "src/io/*.cpp"
    "src/utils/*.cpp"
    "src/ai/ocr/*.cpp"
)

# GUI sources (conditionally included based on backend)
file(GLOB_RECURSE GUI_SOURCES "src/extensions/gui/*/*.cpp")

# Note: gui extensions are independent GUI modules, each with its own __init__.py
# for Python import support. Common utilities are in src/extensions/gui/common/

# Havel language sources (conditional)
set(HAVEL_LANG_SOURCES "")
if(ENABLE_HAVEL_LANG)
    file(GLOB_RECURSE HAVEL_LANG_SOURCES "src/havel-lang/*.cpp")
    # Exclude LSP sources (they have their own target)
    # Exclude C API sources (not implemented)
    list(REMOVE_ITEM HAVEL_LANG_SOURCES
        ${CMAKE_SOURCE_DIR}/src/havel-lang/lsp/main.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/lsp/LanguageServer.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/tools/BytecodeSmokeMain.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/runtime/HostBridgeNew.cpp
        # Note: Disabled C API files (not implemented)
        # ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/module/HavelAPI.cpp
        # ${CMAKE_SOURCE_DIR}/src/extensions/HavelValue.cpp
    )
    
    # Exclude BytecodeOrcJIT if LLVM is not enabled (it requires LLVM libraries)
    if(NOT ENABLE_LLVM)
        list(REMOVE_ITEM HAVEL_LANG_SOURCES
            ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp
        )
    endif()
    
    # -----------------------------------------------------------------------
    # Apply LLVM include fixes to BytecodeOrcJIT.cpp globally
    # This prevents LLVM 23's bare `namespace __detail` from polluting std::__detail
    # The fix applies to all targets that include HAVEL_LANG_SOURCES
    # -----------------------------------------------------------------------
    if(ENABLE_LLVM)
        set(JIT_SRC "${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp")
        set(LAUNCHER_SRC "${CMAKE_SOURCE_DIR}/src/core/init/HavelLauncher.cpp")
        # Convert list of include dirs to properly formatted -I flags
        # LLVM_INCLUDE_DIRS is a semicolon-separated list, so we need to prefix each with -I
        string(REGEX REPLACE ";" " -I" LLVM_INCLUDE_FLAGS "-I${LLVM_INCLUDE_DIRS}")
        set_source_files_properties(${JIT_SRC} ${LAUNCHER_SRC} PROPERTIES
            COMPILE_FLAGS "${LLVM_INCLUDE_FLAGS}"
        )
    endif()
endif()

# Host modules (language bindings to system managers)
file(GLOB_RECURSE HOST_MODULES_SOURCES "src/modules/*.cpp")

# Explicitly add new modules (GLOB might not catch new files)
list(APPEND HOST_MODULES_SOURCES "src/modules/window/WindowMonitorModule.cpp")
list(APPEND HOST_MODULES_SOURCES "src/modules/mouse/MouseModule.cpp")
list(APPEND HOST_MODULES_SOURCES "src/modules/automation/AutomationModule.cpp")
list(APPEND HOST_MODULES_SOURCES "src/modules/ui/UIModule.cpp")

# Test sources (conditional)
set(TEST_SOURCES "")
if(ENABLE_TESTS)
    file(GLOB_RECURSE TEST_SOURCES "src/tests/*.cpp" "tests/*_test.cpp" "tests/phase2_integration_test.cpp")
endif()




# ==============================================================================
pkg_check_modules(XI REQUIRED xi)

# PulseAudio
pkg_check_modules(PULSEAUDIO libpulse)
pkg_check_modules(PCRE2_8 REQUIRED libpcre2-8)
pkg_check_modules(PCRE2_16 REQUIRED libpcre2-16)
# ALSA
pkg_check_modules(ALSA alsa)
# Minizip for ZIP functionality
pkg_check_modules(MINIZIP minizip)

# OCR libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

# OpenCV with workaround for FFmpeg symbol version mismatch
# Some systems have libavfilter/libavutil version conflicts
# We ignore unresolved symbols in OpenCV's FFmpeg dependencies since we don't use video
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# Only link essential OpenCV modules (reduces binary size by ~200MB)
# Full OpenCV has 40+ modules, we only use:
# - core: Basic data structures and functions
# - imgproc: Image processing (pixel automation)
# - imgcodecs: Image reading/writing (screenshots)
set(OPENCV_ESSENTIAL_LIBS
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
)

if(OpenCV_LIBS MATCHES "avfilter")
    message(STATUS "OpenCV found with FFmpeg - applying linker workaround for libavutil symbols")
    # Add workaround for av_alpha_mode_* symbols that may be missing in some FFmpeg versions
    set(OPENCV_FFMPEG_WORKAROUND "-Wl,--unresolved-symbols=ignore-in-shared-libs")
endif()

# ==============================================================================
# UI BACKEND LIBRARIES (DYNAMIC - Qt linked to main for core, extensions for additional UI)
# ==============================================================================
set(UI_BACKEND_DEFS "")
set(UI_BACKEND_INCLUDES "")

if(UI_BACKEND STREQUAL "QT" AND QT_BACKEND_AVAILABLE)
    # Qt6 include directories - use standard paths
    list(APPEND UI_BACKEND_INCLUDES 
        /usr/include/qt6 
        /usr/include/qt6/QtCore
        /usr/include/qt6/QtWidgets
        /usr/include/qt6/QtGui
    )
    list(APPEND COMMON_LIBS Qt6::Core Qt6::Widgets Qt6::Gui)
    list(APPEND UI_BACKEND_DEFS HAVE_QT_EXTENSION)
    message(STATUS "Qt6:             ✓ Linked to main + dynamic extension available")
elseif(UI_BACKEND STREQUAL "GTK" AND GTK_BACKEND_AVAILABLE)
    list(APPEND UI_BACKEND_LIBS ${GTK4_LIBRARIES})
    list(APPEND UI_BACKEND_INCLUDES ${GTK4_INCLUDE_DIRS})
    list(APPEND UI_BACKEND_DEFS HAVE_GTK_BACKEND)
elseif(UI_BACKEND STREQUAL "IMGUI" AND IMGUI_BACKEND_AVAILABLE)
    list(APPEND UI_BACKEND_LIBS ${GLFW3_LIBRARIES} OpenGL::GL)
    list(APPEND UI_BACKEND_INCLUDES ${GLFW3_INCLUDE_DIRS} ${IMGUI_INCLUDE_DIR})
    list(APPEND UI_BACKEND_DEFS HAVE_IMGUI_BACKEND)
endif()

# ==============================================================================
# GTK4 BACKEND (OPTIONAL)
# ==============================================================================
if(ENABLE_GTK OR UI_BACKEND STREQUAL "GTK")
    pkg_check_modules(GTK4 gtk4)
    if(GTK4_FOUND)
        set(GTK_BACKEND_AVAILABLE TRUE)
        set(ENABLE_QT OFF)  # Disable Qt when GTK is selected
        set(UI_BACKEND "GTK")
        message(STATUS "GTK4 backend:    ✓ ENABLED")
        # Also check for cairo (required for canvas API)
        pkg_check_modules(CAIRO cairo)
        if(CAIRO_FOUND)
            list(APPEND GTK4_INCLUDE_DIRS ${CAIRO_INCLUDE_DIRS})
        endif()
    else()
        set(GTK_BACKEND_AVAILABLE FALSE)
        message(STATUS "GTK4 backend:    ✗ NOT FOUND (gtk4-dev not installed)")
        if(UI_BACKEND STREQUAL "GTK")
            message(FATAL_ERROR "GTK backend requested but GTK4 not found")
        endif()
    endif()
else()
    set(GTK_BACKEND_AVAILABLE FALSE)
endif()

# ==============================================================================
# IMGUI BACKEND (OPTIONAL)
# ==============================================================================
if(ENABLE_IMGUI OR UI_BACKEND STREQUAL "IMGUI")
    pkg_check_modules(GLFW3 glfw3)
    find_package(OpenGL)
    
    # Find ImGui (typically bundled or system-installed)
    find_path(IMGUI_INCLUDE_DIR imgui.h
        PATHS 
            /usr/include/imgui
            /usr/local/include/imgui
            ${CMAKE_SOURCE_DIR}/third_party/imgui
            ${CMAKE_SOURCE_DIR}/vendor/imgui
    )
    
    if(GLFW3_FOUND AND OpenGL_FOUND AND IMGUI_INCLUDE_DIR)
        set(IMGUI_BACKEND_AVAILABLE TRUE)
        set(ENABLE_QT OFF)  # Disable Qt when ImGui is selected
        set(UI_BACKEND "IMGUI")
        message(STATUS "ImGui backend:   ✓ ENABLED")
        message(STATUS "  GLFW3:         ${GLFW3_VERSION}")
        message(STATUS "  OpenGL:        ${OPENGL_VERSION}")
    else()
        set(IMGUI_BACKEND_AVAILABLE FALSE)
        message(STATUS "ImGui backend:   ✗ NOT FOUND")
        if(NOT GLFW3_FOUND)
            message(STATUS "  Missing:       glfw3-dev")
        endif()
        if(NOT OpenGL_FOUND)
            message(STATUS "  Missing:       OpenGL development libraries")
        endif()
        if(NOT IMGUI_INCLUDE_DIR)
            message(STATUS "  Missing:       Dear ImGui headers")
        endif()
        if(UI_BACKEND STREQUAL "IMGUI")
            message(FATAL_ERROR "ImGui backend requested but dependencies not found")
        endif()
    endif()
else()
    set(IMGUI_BACKEND_AVAILABLE FALSE)
endif()

# Auto-select backend if not specified
if(UI_BACKEND STREQUAL "AUTO")
    if(QT_BACKEND_AVAILABLE)
        set(UI_BACKEND "QT")
    elseif(GTK_BACKEND_AVAILABLE)
        set(UI_BACKEND "GTK")
    elseif(IMGUI_BACKEND_AVAILABLE)
        set(UI_BACKEND "IMGUI")
    else()
        message(FATAL_ERROR "No UI backend available. Install Qt6, GTK4, or ImGui+GLFW3+OpenGL")
    endif()
    message(STATUS "Auto-selected UI backend: ${UI_BACKEND}")
endif()

set(COMMON_LIBS
    # System libraries
    ${PCRE2_8_LIBRARIES}
    ${PCRE2_16_LIBRARIES}
    ${X11_LIBRARIES}
    ${XI_LIBRARIES}
    ${XTEST_LIB}
    ${XRANDR_LIB}
    ${XINERAMA_LIB}
    ${XCOMPOSITE_LIBRARIES}
    Xfixes
    ${PULSEAUDIO_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${ALSA_LIBRARIES}
    Threads::Threads
    ncurses
    ${DBUS_LIBRARIES}
    ${LUA_LIBRARIES}
    # OCR libraries
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
    ${OPENCV_ESSENTIAL_LIBS}
    # Optional Readline
    ${READLINE_LIB}
    dl crypto ssl pthread m
    # ZIP libraries (use minizip if available, otherwise fall back to generic zip)
    $<$<BOOL:${MINIZIP_FOUND}>:${MINIZIP_LIBRARIES}>
    $<$<NOT:$<BOOL:${MINIZIP_FOUND}>>:zip>
    # Note: Qt6 is loaded dynamically as an extension, not linked here
)

# ==============================================================================
# NATIVE EXTENSIONS (MODULE libraries loaded via dlopen)
# ==============================================================================

# Extension runtime library (shared by all extensions)
add_library(havel_extension_runtime STATIC
    src/extensions/HavelValue.cpp
)
target_include_directories(havel_extension_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
)

# Image extension (OpenCV-based)
add_library(image_extension MODULE src/extensions/image/image_extension.cpp)
target_include_directories(image_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(image_extension PRIVATE 
    havel_extension_runtime
    ${OPENCV_ESSENTIAL_LIBS}
)
set_target_properties(image_extension PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(image_extension PRIVATE -fno-lto)
target_link_options(image_extension PRIVATE -fno-lto)

# OCR extension (Tesseract-based)
add_library(ocr_extension MODULE src/extensions/ocr/ocr_extension.cpp)
target_include_directories(ocr_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
)
target_link_libraries(ocr_extension PRIVATE
    havel_extension_runtime
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
)
target_compile_definitions(ocr_extension PRIVATE HAVE_TESSERACT)
set_target_properties(ocr_extension PROPERTIES
    PREFIX ""
    C_STANDARD 11
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(ocr_extension PRIVATE -fno-lto)
target_link_options(ocr_extension PRIVATE -fno-lto)

# GTK4 extension (GTK4-based UI)
# Uses dynamic loading - no compile-time dependency on GTK4
add_library(gtk_extension MODULE src/extensions/gtk/gtk_extension.cpp)
target_include_directories(gtk_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${GTK4_INCLUDE_DIRS}
    /usr/include/cairo
    /usr/include/freetype2
    /usr/include/libpng16
    /usr/include/pixman-1
)
target_link_libraries(gtk_extension PRIVATE
    havel_extension_runtime
    ${GTK4_LIBRARIES}
    dl
)
target_compile_definitions(gtk_extension PRIVATE HAVE_GTK_BACKEND)
set_target_properties(gtk_extension PROPERTIES
    PREFIX ""
    C_STANDARD 11
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(gtk_extension PRIVATE -fno-lto)
target_link_options(gtk_extension PRIVATE -fno-lto)

# ImGui extension (Dear ImGui with GLFW/OpenGL)
# Uses dynamic loading - no compile-time dependency on ImGui/GLFW/OpenGL
add_library(imgui_extension MODULE src/extensions/imgui/imgui_extension.cpp)
target_include_directories(imgui_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${GLFW3_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIR}
)
target_link_libraries(imgui_extension PRIVATE
    havel_extension_runtime
    ${GLFW3_LIBRARIES}
    OpenGL::GL
    dl
)
target_compile_definitions(imgui_extension PRIVATE HAVE_IMGUI_BACKEND)
set_target_properties(imgui_extension PROPERTIES
    PREFIX ""
    C_STANDARD 11
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)

# Qt6 extension (Qt6 Widgets) - for additional dynamic UI capabilities
# Uses dynamic loading but main executable still links Qt for core functionality
add_library(qt_extension MODULE 
    src/extensions/qt/qt_extension.cpp
)
target_include_directories(qt_extension PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${UI_BACKEND_INCLUDES}
)
target_link_libraries(qt_extension PRIVATE
    havel_extension_runtime
    Qt6::Core
    Qt6::Widgets
    dl
)
set_target_properties(qt_extension PROPERTIES
    PREFIX ""
    C_STANDARD 11
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(qt_extension PRIVATE -fno-lto)
target_link_options(qt_extension PRIVATE -fno-lto)

# ==============================================================================
# MAIN EXECUTABLE
# ==============================================================================
add_executable(havel
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${HAVEL_LANG_SOURCES}>
    ${HOST_MODULES_SOURCES}
)

# ==============================================================================
# LANGUAGE SERVER PROTOCOL SERVER
# ==============================================================================

# LSP-specific sources (just lexer and parser, no Qt dependencies)
set(LSP_SOURCES
    src/havel-lang/lsp/main.cpp
    src/havel-lang/lsp/LanguageServer.cpp
    src/havel-lang/ast/AST.cpp
    src/havel-lang/lexer/Lexer.cpp
    src/havel-lang/parser/Parser.cpp
    src/utils/Logger.cpp
)

add_executable(havel-lsp ${LSP_SOURCES})

set_target_properties(havel-lsp PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_options(havel-lsp PRIVATE -fno-lto)
target_link_options(havel-lsp PRIVATE -fno-lto)

target_include_directories(havel-lsp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/havel-lang
)

 target_link_libraries(havel-lsp PRIVATE
 spdlog::spdlog
 nlohmann_json::nlohmann_json
 )

 if(ENABLE_COVERAGE)
 target_compile_options(havel-lsp PRIVATE --coverage)
 target_link_options(havel-lsp PRIVATE --coverage)
 endif()

# ==============================================================================
# UNIFIED TEST RUNNER
# ==============================================================================
if(ENABLE_HAVEL_LANG)
set(HVTEST_SOURCES
  src/hvtest/main.cpp
  src/hvtest/smoke_runner.cpp
  src/utils/Logger.cpp
)

add_executable(hvtest ${HVTEST_SOURCES})
set_target_properties(hvtest PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_compile_options(hvtest PRIVATE -fno-lto)
target_link_options(hvtest PRIVATE -fno-lto)

target_include_directories(hvtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/havel-lang
)

target_link_libraries(hvtest PRIVATE
  spdlog::spdlog
  havel_lang
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  enable_testing()
  add_test(NAME hvtest-smoke COMMAND hvtest --smoke)
  if(ENABLE_COVERAGE)
    target_compile_options(hvtest PRIVATE --coverage)
    target_link_options(hvtest PRIVATE --coverage)
  endif()
endif()
endif()

# Set target properties
set_target_properties(havel PROPERTIES
  CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Precompiled headers - standard library only (Qt headers cause issues)
target_precompile_headers(havel PRIVATE
    # Standard library (frequently used)
    <iostream>
    <string>
    <memory>
    <vector>
    <unordered_map>
    <algorithm>
    <functional>
    <future>
    <thread>
    <chrono>
    <fstream>
    <sstream>
    <regex>
    # Common system headers
    <unistd.h>
    <signal.h>
    <sys/types.h>
)

 # Sanitizers - only for Debug builds to reduce binary size
 if(CMAKE_BUILD_TYPE STREQUAL "Debug")
 target_compile_options(havel PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(havel PRIVATE -fsanitize=address,undefined)
 endif()

 # Coverage instrumentation (mutually exclusive with sanitizers in CI)
 if(ENABLE_COVERAGE)
 target_compile_options(havel PRIVATE --coverage -fno-omit-frame-pointer)
 target_link_options(havel PRIVATE --coverage)
 endif()

# Include directories
target_include_directories(havel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${X11_INCLUDE_DIR}
    ${XI_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
    ${WAYLAND_INCLUDE_DIRS}
    ${GENERATED_PROTO_DIR}
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${UI_BACKEND_INCLUDES}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
    # Note: LLVM_INCLUDE_DIRS intentionally omitted here - scoped to BytecodeOrcJIT.cpp only
    # to prevent LLVM 23 namespace __detail from polluting std::__detail in all TUs.
)

# Link libraries
target_link_libraries(havel PRIVATE
    ${COMMON_LIBS}
    $<$<BOOL:${QT_BACKEND_AVAILABLE}>:Qt6::Gui>
    $<$<BOOL:${QT_BACKEND_AVAILABLE}>:Qt6::Widgets>
    spdlog::spdlog
    ${OPENCV_FFMPEG_WORKAROUND}
    havel_extension_runtime
    $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
)

# Compile definitions
target_compile_definitions(havel PRIVATE
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:ENABLE_HAVEL_LANG>
    $<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
    $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
    $<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
    $<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
    ${UI_BACKEND_DEFS}
    UI_BACKEND=\"${UI_BACKEND}\"
)

# ==============================================================================
# HAVEL LANGUAGE LIBRARY (CONDITIONAL)
# ==============================================================================
if(ENABLE_HAVEL_LANG)
    set(HAVEL_LANG_LIB_SOURCES ${HAVEL_LANG_SOURCES})
    list(REMOVE_ITEM HAVEL_LANG_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/runtime/HostBridge.cpp
    )
    # HostBridge.hpp inlines loadModule / isModuleAvailable → HostModuleLoader
    list(APPEND HAVEL_LANG_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/host/module/ModuleLoader.cpp
    )
    add_library(havel_lang STATIC ${HAVEL_LANG_LIB_SOURCES})
    
    target_include_directories(havel_lang PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${X11_INCLUDE_DIR}
        ${XI_INCLUDE_DIR}
        ${DBUS_INCLUDE_DIRS}
        ${PULSEAUDIO_INCLUDE_DIRS}
        ${PIPEWIRE_INCLUDE_DIRS}
        ${ALSA_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
        ${WAYLAND_INCLUDE_DIRS}
        ${GENERATED_PROTO_DIR}
        $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
        $<$<BOOL:${HAVE_LIBFFI}>:${LIBFFI_INCLUDE_DIRS}>
        # Note: LLVM_INCLUDE_DIRS scoped to BytecodeOrcJIT.cpp only (see below)
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtCore>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtWidgets>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtGui>
    )
    
    target_link_libraries(havel_lang PRIVATE
        ${COMMON_LIBS}
        spdlog::spdlog
        $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
        $<$<BOOL:${HAVE_LIBFFI}>:ffi>
        dl
    )

    # Disable LTO for debug builds to avoid R_X86_64_PC32 relocation overflow
    # in PIE test binaries. Release builds keep LTO for performance.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(havel_lang PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
        )
        target_compile_options(havel_lang PRIVATE -fno-lto)
    endif()
    
    target_compile_definitions(havel_lang PRIVATE
        $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
        $<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:HAVE_QT_EXTENSION>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_CORE_LIB>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_GUI_LIB>
        $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_WIDGETS_LIB>
        $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
    )

    # -----------------------------------------------------------------------
    # Note: LLVM_INCLUDE_DIRS handling is applied globally at source assembly
    # stage to all targets that use HAVEL_LANG_SOURCES (including tests).
    # This prevents LLVM 23's bare `namespace __detail` from polluting
    # std::__detail across every translation unit in havel_lang.
    # -----------------------------------------------------------------------

endif()

# ==============================================================================
# TEST EXECUTABLES (CONDITIONAL)
# ==============================================================================
if(ENABLE_TESTS AND TEST_SOURCES AND GTest_FOUND)
    foreach(test_file ${TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)

        add_executable(${test_name}
            ${test_file}
            ${CORE_SOURCES}
            ${GUI_SOURCES}
            ${HOST_MODULES_SOURCES}
            $<$<BOOL:${ENABLE_HAVEL_LANG}>:${HAVEL_LANG_SOURCES}>
        )
        
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/include
            ${X11_INCLUDE_DIR}
            ${XI_INCLUDE_DIR}
            ${DBUS_INCLUDE_DIRS}
            ${PULSEAUDIO_INCLUDE_DIRS}
            ${PIPEWIRE_INCLUDE_DIRS}
            ${ALSA_INCLUDE_DIRS}
            ${LUA_INCLUDE_DIR}
            ${WAYLAND_INCLUDE_DIRS}
            ${GENERATED_PROTO_DIR}
            $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
            $<$<BOOL:${ENABLE_LLVM}>:${LLVM_INCLUDE_DIRS}>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtCore>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtWidgets>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:/usr/include/qt6/QtGui>
        )
        
        target_link_libraries(${test_name} PRIVATE 
            ${COMMON_LIBS}
            spdlog::spdlog
            GTest::gtest 
            GTest::gtest_main 
            Qt6::Test
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:Qt6::Widgets>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:Qt6::Gui>
            havel_extension_runtime
            $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
        )
        
        target_compile_definitions(${test_name} PRIVATE
            RUN_TESTS
            $<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
            $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
            $<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
            $<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
            $<$<BOOL:${PULSEAUDIO_FOUND}>:HAVE_PULSEAUDIO>
            $<$<BOOL:${ALSA_FOUND}>:HAVE_ALSA>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:HAVE_QT_EXTENSION>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_CORE_LIB>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_GUI_LIB>
            $<$<BOOL:${QT_BACKEND_AVAILABLE}>:QT_WIDGETS_LIB>
        )
        
 # Add to CTest
 add_test(NAME ${test_name} COMMAND ${test_name})

 if(ENABLE_COVERAGE)
 target_compile_options(${test_name} PRIVATE --coverage)
 target_link_options(${test_name} PRIVATE --coverage)
 endif()
 endforeach()
    
    # Special GUI test if it exists
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/TestGUI.cpp")
        add_executable(test_gui tests/TestGUI.cpp)
        
        target_link_libraries(test_gui PRIVATE
            spdlog::spdlog
            Qt6::Widgets 
            Qt6::Test
            GTest::gtest 
            GTest::gtest_main
        )
        
        target_compile_definitions(test_gui PRIVATE RUN_TESTS)
        add_test(NAME test_gui COMMAND test_gui)
    endif()
endif()

# ==============================================================================
# INSTALLATION RULES
# ==============================================================================
install(TARGETS havel
  RUNTIME DESTINATION bin
  COMPONENT Runtime
)

if(TARGET havel-lsp)
install(TARGETS havel-lsp
  RUNTIME DESTINATION bin
  COMPONENT Runtime
)
endif()

if(TARGET hvtest)
install(TARGETS hvtest
  RUNTIME DESTINATION bin
  COMPONENT Runtime
)
endif()

if(TARGET havel_lang)
    install(TARGETS havel_lang
        ARCHIVE DESTINATION lib
        COMPONENT Development
    )
endif()

# Install protocol files for development
install(DIRECTORY ${CMAKE_SOURCE_DIR}/protocols/
    DESTINATION share/havel/protocols
    COMPONENT Development
)

# ==============================================================================
# BUILD SUMMARY
# ==============================================================================
message(STATUS "")
message(STATUS "=== HAVEL BUILD CONFIGURATION SUMMARY ===")
message(STATUS "Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler:        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard:    C++${CMAKE_CXX_STANDARD}")

# Feature status
message(STATUS "")
message(STATUS "=== FEATURES ===")
if(QT_BACKEND_AVAILABLE)
    message(STATUS "GUI (Qt6):       ✓ DYNAMIC EXTENSION (${Qt6_VERSION})")
else()
    message(STATUS "GUI (Qt6):       ✗ Not available")
endif()
if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire:        ✓ ENABLED")
endif()
if(PULSEAUDIO_FOUND)
    message(STATUS "PulseAudio:      ✓ ENABLED")
endif()
if(ALSA_FOUND)
    message(STATUS "ALSA:            ✓ ENABLED")
endif()
message(STATUS "Wayland:         ✓ ENABLED")
message(STATUS "X11:             ✓ ENABLED")

if(ENABLE_HAVEL_LANG)
    message(STATUS "Havel Language:  ✓ ENABLED")
else()
    message(STATUS "Havel Language:  ✗ DISABLED")
endif()

if(ENABLE_LLVM)
    message(STATUS "LLVM JIT:        ✓ ENABLED (${LLVM_PACKAGE_VERSION})")
else()
    message(STATUS "LLVM JIT:        ✗ DISABLED")
endif()

 if(ENABLE_TESTS AND GTest_FOUND)
 message(STATUS "Tests: ✓ ENABLED")
 elseif(ENABLE_TESTS)
 message(STATUS "Tests: ⚠ REQUESTED BUT GTEST MISSING")
 else()
 message(STATUS "Tests: ✗ DISABLED")
 endif()

 if(ENABLE_COVERAGE)
 message(STATUS "Coverage: ✓ ENABLED (--coverage)")
 else()
 message(STATUS "Coverage: ✗ DISABLED")
 endif()

# Dependency versions
message(STATUS "")
message(STATUS "=== DEPENDENCIES ===")
message(STATUS "Lua:             ${LUA_VERSION_STRING}")
message(STATUS "Qt6:             ${Qt6_VERSION}")
if(ENABLE_LLVM)
    message(STATUS "LLVM:            ${LLVM_PACKAGE_VERSION}")
endif()
if(GTest_FOUND)
    message(STATUS "GTest:           Found")
endif()

# Build targets
message(STATUS "")
message(STATUS "=== BUILD TARGETS ===")
message(STATUS "Main executable: havel")
if(TARGET havel-lsp)
message(STATUS "LSP server: havel-lsp")
endif()
if(TARGET hvtest)
message(STATUS "Test runner: hvtest")
endif()
if(TARGET havel_lang)
    message(STATUS "Language lib:    libhavel_lang.a")
endif()

if(ENABLE_TESTS AND GTest_FOUND AND TEST_SOURCES)
    list(LENGTH TEST_SOURCES test_count)
    message(STATUS "Test executables: ${test_count} tests")
endif()

message(STATUS "")
message(STATUS "Installation prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=== END CONFIGURATION ===")
message(STATUS "")

# ==============================================================================
# DEVELOPMENT HELPERS
# ==============================================================================
# Add custom target for cleaning generated files
add_custom_target(clean-generated
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${GENERATED_PROTO_DIR}
    COMMENT "Cleaning generated protocol files"
)

# Add custom target for regenerating protocols only
add_custom_target(regen-protocols
    DEPENDS ${GAMMA_HEADERS} ${XDG_HEADERS}
    COMMENT "Regenerating Wayland protocol files"
)
if(PULSEAUDIO_FOUND)
    target_compile_definitions(havel PRIVATE HAVE_PULSEAUDIO)
endif()
if(ALSA_FOUND)
    target_compile_definitions(havel PRIVATE HAVE_ALSA)
endif()
if(TARGET havel_lang)
    target_include_directories(havel_lang PRIVATE
        ${CMAKE_SOURCE_DIR}/src/havel-lang
    )
endif()
