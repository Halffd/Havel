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
option(ENABLE_QT_UI_BACKEND "Build in-process Qt6 UI backend (fallback to dynamic extension)" ON)
option(ENABLE_HEADLESS "Build without any GUI backend (server/headless mode)" OFF)
option(ENABLE_MODULE_PLUGINS "Build stdlib/host modules as .so plugins" OFF)

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
find_package(OpenGL REQUIRED)
find_package(fmt CONFIG REQUIRED)
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

# libcurl (HTTP client)
pkg_check_modules(CURL REQUIRED libcurl)

# PipeWire
pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire:        ✓ ENABLED (${PIPEWIRE_VERSION})")
else()
    message(STATUS "PipeWire:        ✗ NOT FOUND (Falling back to PulseAudio/ALSA)")
endif()

# libffi (for FFI - Foreign Function Interface)
pkg_check_modules(LIBFFI QUIET libffi)
if(LIBFFI_FOUND)
    message(STATUS "libffi:       ✓ FOUND (${LIBFFI_VERSION})")
    set(HAVE_LIBFFI 1)
else()
    message(STATUS "libffi:       ✗ NOT FOUND (FFI will be limited)")
    set(HAVE_LIBFFI 0)
endif()

# X11 extensions
find_library(XTEST_LIB Xtst REQUIRED)
find_library(XRANDR_LIB Xrandr REQUIRED)  
find_library(XINERAMA_LIB Xinerama REQUIRED)
pkg_check_modules(XCOMPOSITE REQUIRED xcomposite)
pkg_check_modules(XDAMAGE REQUIRED xdamage)

if(NOT XTEST_LIB OR NOT XRANDR_LIB OR NOT XINERAMA_LIB)
    message(FATAL_ERROR "Missing X11 extension libraries. Install libxtst-dev libxrandr-dev libxinerama-dev")
endif()

# ==============================================================================
# WAYLAND SUPPORT
# ==============================================================================
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-protocols)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Protocol generation setup
set(PROTOCOLS_DIR /usr/share/wayland-protocols)
set(GENERATED_PROTO_DIR ${CMAKE_BINARY_DIR}/generated/wayland)
file(MAKE_DIRECTORY ${GENERATED_PROTO_DIR})

# Helper function to generate Wayland protocol headers
function(generate_wayland_protocol PROTO_NAME PROTO_XML)
  set(PROTO_HEADER ${GENERATED_PROTO_DIR}/${PROTO_NAME}-client-protocol.h)
  set(PROTO_CODE ${GENERATED_PROTO_DIR}/${PROTO_NAME}-protocol.c)
  add_custom_command(
    OUTPUT ${PROTO_HEADER} ${PROTO_CODE}
    COMMAND ${WAYLAND_SCANNER} client-header ${PROTO_XML} ${PROTO_HEADER}
    COMMAND ${WAYLAND_SCANNER} private-code ${PROTO_XML} ${PROTO_CODE}
    DEPENDS ${PROTO_XML}
    COMMENT "Generating ${PROTO_NAME} protocol headers"
  )
  set(PROTO_SOURCES ${PROTO_HEADER} ${PROTO_CODE} PARENT_SCOPE)
endfunction()

# Gamma control protocol (in-tree, wlroots)
set(GAMMA_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-gamma-control-unstable-v1.xml)
generate_wayland_protocol(wlr-gamma-control-unstable-v1 ${GAMMA_XML})

# XDG output protocol (system)
set(XDG_XML ${PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml)
generate_wayland_protocol(xdg-output-unstable-v1 ${XDG_XML})

# Virtual keyboard protocol (in-tree, wlroots)
set(VKB_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/virtual-keyboard-unstable-v1.xml)
generate_wayland_protocol(virtual-keyboard-unstable-v1 ${VKB_XML})

# Virtual pointer protocol (in-tree, wlroots)
set(VPTR_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-virtual-pointer-unstable-v1.xml)
generate_wayland_protocol(wlr-virtual-pointer-unstable-v1 ${VPTR_XML})

# Foreign toplevel management protocol (in-tree, wlroots)
set(FTL_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-foreign-toplevel-management-unstable-v1.xml)
generate_wayland_protocol(wlr-foreign-toplevel-management-unstable-v1 ${FTL_XML})

# WLR data control protocol (in-tree, wlroots)
set(WDC_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-data-control-unstable-v1.xml)
generate_wayland_protocol(wlr-data-control-unstable-v1 ${WDC_XML})

# ext-data-control protocol (system, staging standard)
set(EDC_XML ${PROTOCOLS_DIR}/staging/ext-data-control/ext-data-control-v1.xml)
if(EXISTS ${EDC_XML})
  generate_wayland_protocol(ext-data-control-v1 ${EDC_XML})
  set(HAS_EXT_DATA_CONTROL TRUE)
else()
  set(HAS_EXT_DATA_CONTROL FALSE)
  message(STATUS "ext-data-control-v1 not found in system protocols, using wlr-data-control fallback")
endif()

# Create wayland protocols library
set(ALL_PROTO_SOURCES
  ${PROTO_SOURCES}
  ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/wlr-gamma-control-unstable-v1-protocol.c
  ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/xdg-output-unstable-v1-protocol.c
  ${GENERATED_PROTO_DIR}/virtual-keyboard-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/virtual-keyboard-unstable-v1-protocol.c
  ${GENERATED_PROTO_DIR}/wlr-virtual-pointer-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/wlr-virtual-pointer-unstable-v1-protocol.c
  ${GENERATED_PROTO_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
  ${GENERATED_PROTO_DIR}/wlr-data-control-unstable-v1-client-protocol.h
  ${GENERATED_PROTO_DIR}/wlr-data-control-unstable-v1-protocol.c
)
if(HAS_EXT_DATA_CONTROL)
  list(APPEND ALL_PROTO_SOURCES
    ${GENERATED_PROTO_DIR}/ext-data-control-v1-client-protocol.h
    ${GENERATED_PROTO_DIR}/ext-data-control-v1-protocol.c
  )
endif()

add_library(wayland-protos STATIC ${ALL_PROTO_SOURCES})
target_include_directories(wayland-protos PUBLIC ${GENERATED_PROTO_DIR})
target_link_libraries(wayland-protos PUBLIC ${WAYLAND_LIBRARIES} ${XKBCOMMON_LIBRARIES})
target_include_directories(wayland-protos PUBLIC ${XKBCOMMON_INCLUDE_DIRS})

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
        string(STRIP "${LLVM_LIBS}" LLVM_LIBS)
        separate_arguments(LLVM_LIBS) # Convert space-separated string to CMake list
        string(JOIN " " HAVEL_LLVM_LINK_FLAGS ${LLVM_LIBS})
        
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
    "src/c/*.c"
 "src/c/*.cpp"
 "src/dl/*.cpp"
    "src/host/async/*.cpp"
    "src/host/audio/*.cpp"
    "src/host/brightness/*.cpp"
    "src/host/browser/*.cpp"
    "src/host/chunker/*.cpp"
    "src/host/filesystem/*.cpp"
    "src/host/hotkey/*.cpp"
    "src/host/io/*.cpp"
    "src/host/media/*.cpp"
    "src/host/mouse/*.cpp"
    "src/host/mode/*.cpp"
    "src/host/network/*.cpp"
    "src/host/process/*.cpp"
    "src/host/image/*.cpp"
    "src/host/timer/*.cpp"
    "src/window/*.cpp"
    "src/window/backends/*.cpp"
    "src/window/strategies/*.cpp"
"src/utils/*.cpp"
"src/utils/*.c"
"src/media/*.cpp"
"src/ai/ocr/*.cpp"
)
# Non-Qt host sources (always built)
file(GLOB_RECURSE _HOST_CORE_SOURCES
  "src/host/app/*.cpp"
  "src/host/automation/AutomationService.cpp"
)
list(APPEND CORE_SOURCES ${_HOST_CORE_SOURCES})

# Non-Qt host files in Qt-dependency directories (always built)
list(APPEND CORE_SOURCES
"src/host/module/ModuleLoader.cpp"
"src/host/module/ModularHostBridges.cpp"
"src/host/ui/GtkBackend.cpp"
"src/host/ui/ImGuiBackend.cpp"
"src/host/ui/UIManager.cpp"
"src/host/window/WindowService.cpp"
"src/Havel.cpp"
 "src/c/Config.c"
"src/modules/HostModules.cpp"
)

# Qt-dependent core sources (only built when in-process Qt backend is enabled)
if(ENABLE_QT_UI_BACKEND)
  set(_QT_CORE_SOURCES
    "src/host/automation/PixelAutomationService.cpp"
    "src/host/clipboard/Clipboard.cpp"
    "src/host/clipboard/ClipboardInfo.cpp"
    "src/host/clipboard/ClipboardService.cpp"
    "src/host/clipboard/HistoryClipboard.cpp"
    "src/host/clipboard/MonitoringClipboard.cpp"
    "src/host/screenshot/ScreenshotService.cpp"
    "src/host/ui/QtBackend.cpp"
    "src/host/ui/UIService.cpp"
    "src/host/window/AltTabService.cpp"
  )
  list(APPEND CORE_SOURCES ${_QT_CORE_SOURCES})
endif()



# Input backend adapters - re-enabled after API fixes

# GUI sources - only build when Qt UI backend is enabled
# (all gui/* files unconditionally include qt.hpp)
if(ENABLE_QT_UI_BACKEND)
  file(GLOB_RECURSE GUI_SOURCES "src/extensions/gui/*/*.cpp")
else()
  set(GUI_SOURCES "")
endif()

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
    ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/Compiler.cpp
        # Note: Disabled C API files (not implemented)
        # ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/module/HavelAPI.cpp
        # ${CMAKE_SOURCE_DIR}/src/extensions/HavelValue.cpp
)

# Exclude stdlib modules when building as plugins — they become havel_mod_*.so
# Exception: HotkeyModule stays in main binary (host bridges use its C++ class directly)
# Exception: StringModule is called from VMHostFunctions — must stay in static lib
if(ENABLE_MODULE_PLUGINS)
file(GLOB_RECURSE _STDLIB_MODULE_SOURCES "src/havel-lang/stdlib/*Module.cpp")
  list(REMOVE_ITEM _STDLIB_MODULE_SOURCES
      "${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/HotkeyModule.cpp"
      "${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/StringModule.cpp"
      "${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/FsModule.cpp"
      "${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/BytecodeBuilderModule.cpp"
  )
list(REMOVE_ITEM HAVEL_LANG_SOURCES ${_STDLIB_MODULE_SOURCES})
endif()

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
list(REMOVE_ITEM HOST_MODULES_SOURCES "${CMAKE_SOURCE_DIR}/src/modules/HostModules.cpp")
# Remove Qt-dependent modules when in-process Qt UI backend is disabled
if(NOT ENABLE_QT_UI_BACKEND)
  file(GLOB_RECURSE _QT_MODULES
    "src/modules/clipboardmgr/*.cpp"
    "src/modules/ui/*.cpp"
    "src/modules/screenshot/*.cpp"
    "src/modules/alttab/*.cpp"
    "src/modules/clipboard/*.cpp"
    "src/modules/automation/PixelModule.cpp"
  )
  list(REMOVE_ITEM HOST_MODULES_SOURCES ${_QT_MODULES})
endif()

# Explicitly add new modules (GLOB might not catch new files)

list(APPEND HOST_MODULES_SOURCES "src/modules/window/WindowMonitorModule.cpp")
list(APPEND HOST_MODULES_SOURCES "src/modules/mouse/MouseModule.cpp")
list(APPEND HOST_MODULES_SOURCES "src/modules/automation/AutomationModule.cpp")
if(ENABLE_QT_UI_BACKEND)
  list(APPEND HOST_MODULES_SOURCES "src/modules/ui/UIModule.cpp")
endif()

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
pkg_check_modules(TESSERACT tesseract)
pkg_check_modules(LEPTONICA lept)

# OpenCV with workaround for FFmpeg symbol version mismatch
# Some systems have libavfilter/libavutil version conflicts
# We ignore unresolved symbols in OpenCV's FFmpeg dependencies since we don't use video
find_package(OpenCV COMPONENTS core imgproc imgcodecs)

if(OpenCV_FOUND)
    set(OPENCV_ESSENTIAL_LIBS
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
    )

    if(OpenCV_LIBS MATCHES "avfilter")
        message(STATUS "OpenCV found with FFmpeg - applying linker workaround for libavutil symbols")
        set(OPENCV_FFMPEG_WORKAROUND "-Wl,--unresolved-symbols=ignore-in-shared-libs")
    endif()
else()
    message(STATUS "OpenCV not found - image module will be stubbed")
    set(OPENCV_ESSENTIAL_LIBS "")
    set(OPENCV_FFMPEG_WORKAROUND "")
endif()

if(TESSERACT_FOUND)
    message(STATUS "Tesseract found - OCR will be available")
else()
    message(STATUS "Tesseract not found - OCR will be stubbed")
endif()

# ==============================================================================
# UI BACKEND LIBRARIES (DYNAMIC - Qt linked to main for core, extensions for additional UI)
# ==============================================================================
set(UI_BACKEND_DEFS "")
set(UI_BACKEND_INCLUDES "")

if(UI_BACKEND STREQUAL "QT" AND QT_BACKEND_AVAILABLE AND ENABLE_QT_UI_BACKEND)
    # Qt6 include directories - use standard paths
    list(APPEND UI_BACKEND_INCLUDES 
        /usr/include/qt6 
        /usr/include/qt6/QtCore
        /usr/include/qt6/QtWidgets
        /usr/include/qt6/QtGui
    )
    list(APPEND BACKEND_LIBS Qt6::Core Qt6::Widgets Qt6::Gui)
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
# Check for GTK4 dev libs even when not selected as primary backend,
# so toolkit plugin .so can still be built for dynamic loading.
pkg_check_modules(GTK4_TOOLKIT gtk4)
set(GTK4_TOOLKIT_AVAILABLE ${GTK4_TOOLKIT_FOUND})
if(GTK4_TOOLKIT_AVAILABLE)
pkg_check_modules(CAIRO_TOOLKIT cairo)
if(CAIRO_TOOLKIT_FOUND)
list(APPEND GTK4_TOOLKIT_INCLUDE_DIRS ${CAIRO_TOOLKIT_INCLUDE_DIRS})
endif()
endif()
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
if(ENABLE_HEADLESS)
  set(UI_BACKEND "HEADLESS")
  set(ENABLE_QT OFF)
  set(ENABLE_QT_UI_BACKEND OFF)
  message(STATUS "UI backend: HEADLESS (no GUI)")
elseif(UI_BACKEND STREQUAL "AUTO")
  if(QT_BACKEND_AVAILABLE)
    set(UI_BACKEND "QT")
  elseif(GTK_BACKEND_AVAILABLE)
    set(UI_BACKEND "GTK")
  elseif(IMGUI_BACKEND_AVAILABLE)
    set(UI_BACKEND "IMGUI")
  else()
    set(UI_BACKEND "HEADLESS")
    set(ENABLE_QT OFF)
    set(ENABLE_QT_UI_BACKEND OFF)
    message(WARNING "No UI backend available - falling back to HEADLESS mode")
  endif()
  if(NOT UI_BACKEND STREQUAL "HEADLESS")
    message(STATUS "Auto-selected UI backend: ${UI_BACKEND}")
  endif()
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
    ${XDAMAGE_LIBRARIES}
    OpenGL::GL
    Xfixes
${WAYLAND_LIBRARIES}
${PULSEAUDIO_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${ALSA_LIBRARIES}
    Threads::Threads
    ncurses
  ${DBUS_LIBRARIES}
# OCR libraries (only linked if found, see image_extension/ocr_extension targets)
$<$<BOOL:${TESSERACT_FOUND}>:${TESSERACT_LIBRARIES}>
$<$<BOOL:${LEPTONICA_FOUND}>:${LEPTONICA_LIBRARIES}>
$<$<BOOL:${OpenCV_FOUND}>:${OPENCV_ESSENTIAL_LIBS}>
    # Optional Readline
    ${READLINE_LIB}
    dl crypto ssl pthread m fmt::fmt
 ${CURL_LIBRARIES}
    # ZIP libraries (use minizip if available, otherwise fall back to generic zip)
    $<$<BOOL:${MINIZIP_FOUND}>:${MINIZIP_LIBRARIES}>
    $<$<NOT:$<BOOL:${MINIZIP_FOUND}>>:zip>
    # Note: Qt6 is loaded dynamically as an extension, not linked here
)

# Append backend libs to common libs
list(APPEND COMMON_LIBS ${BACKEND_LIBS})

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
set_target_properties(havel_extension_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(havel_extension_runtime PRIVATE -fno-lto)
target_link_options(havel_extension_runtime PRIVATE -fno-lto)

# Image extension (OpenCV-based)
if(OpenCV_FOUND)
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
target_compile_definitions(image_extension PRIVATE HAVE_OPENCV)
endif()

# OCR extension (Tesseract-based)
if(TESSERACT_FOUND)
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
endif()

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
if(IMGUI_BACKEND_AVAILABLE)
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
endif()

# Qt6 extension merged into havel_toolkit_qt.so
# (qt_extension.cpp is now compiled into the toolkit plugin)

# ==============================================================================
# TOOLKIT PLUGINS (per-toolkit .so providing all backends)
# ==============================================================================
if(QT_BACKEND_AVAILABLE)
add_library(havel_toolkit_qt MODULE
src/extensions/qt/havel_toolkit_qt.cpp
src/extensions/qt/qt_extension.cpp
src/extensions/qt/QtScreenshotBackend.hpp
src/extensions/qt/QtAltTabBackend.hpp
src/extensions/qt/QtClipboardBackend.hpp
src/extensions/qt/QtClipboardManagerBackend.hpp
)
target_include_directories(havel_toolkit_qt PRIVATE
${CMAKE_SOURCE_DIR}/src
${CMAKE_SOURCE_DIR}/include
${CMAKE_SOURCE_DIR}/src/extensions
${CMAKE_SOURCE_DIR}/src/havel-lang
${UI_BACKEND_INCLUDES}
)
target_link_libraries(havel_toolkit_qt PRIVATE
Qt6::Core Qt6::Widgets Qt6::Gui
fmt::fmt
havel_extension_runtime
dl
)
target_compile_definitions(havel_toolkit_qt PRIVATE
HAVE_QT_EXTENSION
HAVEL_MODULE_PLUGIN
)
set_target_properties(havel_toolkit_qt PROPERTIES
PREFIX ""
OUTPUT_NAME "havel_toolkit_qt"
LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/toolkits"
CXX_STANDARD 23
CXX_VISIBILITY_PRESET default
VISIBILITY_INLINES_HIDDEN OFF
INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(havel_toolkit_qt PRIVATE -fno-lto)
target_link_options(havel_toolkit_qt PRIVATE -fno-lto -Wl,--allow-shlib-undefined)
endif() # QT_BACKEND_AVAILABLE


# GTK4 toolkit plugin
if(GTK4_TOOLKIT_AVAILABLE OR GTK_BACKEND_AVAILABLE)
add_library(havel_toolkit_gtk MODULE
    src/extensions/gtk/havel_toolkit_gtk.cpp
)
set(_GTK_TK_INCLUDES ${GTK4_TOOLKIT_INCLUDE_DIRS})
if(GTK_BACKEND_AVAILABLE)
    set(_GTK_TK_INCLUDES ${GTK4_INCLUDE_DIRS})
endif()
list(APPEND _GTK_TK_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    /usr/include/cairo
    /usr/include/freetype2
    /usr/include/libpng16
    /usr/include/pixman-1
)
target_include_directories(havel_toolkit_gtk PRIVATE ${_GTK_TK_INCLUDES})
set(_GTK_TK_LIBS ${GTK4_TOOLKIT_LIBRARIES})
if(GTK_BACKEND_AVAILABLE)
    set(_GTK_TK_LIBS ${GTK4_LIBRARIES})
endif()
target_link_libraries(havel_toolkit_gtk PRIVATE ${_GTK_TK_LIBS} dl)
target_compile_definitions(havel_toolkit_gtk PRIVATE
    HAVE_GTK_BACKEND
    HAVEL_MODULE_PLUGIN
)
set_target_properties(havel_toolkit_gtk PROPERTIES
    PREFIX ""
    OUTPUT_NAME "havel_toolkit_gtk"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/toolkits"
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(havel_toolkit_gtk PRIVATE -fno-lto)
target_link_options(havel_toolkit_gtk PRIVATE -fno-lto -Wl,--allow-shlib-undefined)
endif()

# ImGui toolkit plugin
if(IMGUI_BACKEND_AVAILABLE)
add_library(havel_toolkit_imgui MODULE
    src/extensions/imgui/havel_toolkit_imgui.cpp
)
target_include_directories(havel_toolkit_imgui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${GLFW3_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIR}
)
target_link_libraries(havel_toolkit_imgui PRIVATE
    ${GLFW3_LIBRARIES}
    OpenGL::GL
    dl
)
target_compile_definitions(havel_toolkit_imgui PRIVATE
    HAVE_IMGUI_BACKEND
    HAVEL_MODULE_PLUGIN
)
set_target_properties(havel_toolkit_imgui PROPERTIES
    PREFIX ""
    OUTPUT_NAME "havel_toolkit_imgui"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/toolkits"
    CXX_STANDARD 23
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(havel_toolkit_imgui PRIVATE -fno-lto)
target_link_options(havel_toolkit_imgui PRIVATE -fno-lto -Wl,--allow-shlib-undefined)
endif() # IMGUI_BACKEND_AVAILABLE

# ==============================================================================
# UI SHIM (C ABI wrapper around UIBackend virtual methods for FFI)
# ==============================================================================
add_library(havel_ui_shim MODULE
	src/extensions/shim/ui_shim.cpp
)
target_include_directories(havel_ui_shim PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(havel_ui_shim PRIVATE
	havel_extension_runtime
	dl
)
target_compile_definitions(havel_ui_shim PRIVATE
	HAVEL_MODULE_PLUGIN
)
set_target_properties(havel_ui_shim PROPERTIES
	PREFIX ""
	OUTPUT_NAME "havel_ui_shim"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shims"
	CXX_STANDARD 23
	CXX_VISIBILITY_PRESET default
	VISIBILITY_INLINES_HIDDEN OFF
	INTERPROCEDURAL_OPTIMIZATION OFF
)
target_compile_options(havel_ui_shim PRIVATE -fno-lto)
target_link_options(havel_ui_shim PRIVATE -fno-lto -Wl,--allow-shlib-undefined)

# ==============================================================================
# MAIN EXECUTABLE
# ==============================================================================
add_library(havel_core STATIC ${CORE_SOURCES})
set_target_properties(havel_core PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)
if(ENABLE_MODULE_PLUGINS)
    set_target_properties(havel_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
set_source_files_properties(
    src/core/hotkey/HotkeyActionContext.cpp
    PROPERTIES COMPILE_FLAGS "-ftls-model=initial-exec"
)
target_compile_definitions(havel_core PRIVATE
$<$<BOOL:${ENABLE_LLVM}>:HAVEL_DEFAULT_LLVM_LINK_FLAGS=\"${HAVEL_LLVM_LINK_FLAGS}\">
$<$<BOOL:${ENABLE_MODULE_PLUGINS}>:ENABLE_MODULE_PLUGINS>
)
if(NOT ENABLE_MODULE_PLUGINS)
add_library(havel_modules STATIC ${HOST_MODULES_SOURCES})
else()
add_library(havel_modules INTERFACE)
endif()
target_link_libraries(havel_core PRIVATE fmt::fmt wayland-protos)
if(ENABLE_QT_UI_BACKEND)
  add_library(havel_gui STATIC ${GUI_SOURCES})
endif()

# Shared configuration for the libraries
set(_LIBS havel_core)
if(NOT ENABLE_MODULE_PLUGINS)
list(APPEND _LIBS havel_modules)
endif()
if(ENABLE_QT_UI_BACKEND)
list(APPEND _LIBS havel_gui)
endif()
foreach(lib ${_LIBS})
target_include_directories(${lib} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/extensions
    ${X11_INCLUDE_DIR}
    ${XI_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
    ${GENERATED_PROTO_DIR}
    $<$<BOOL:${TESSERACT_FOUND}>:${TESSERACT_INCLUDE_DIRS}>
    $<$<BOOL:${LEPTONICA_FOUND}>:${LEPTONICA_INCLUDE_DIRS}>
    $<$<BOOL:${OpenCV_FOUND}>:${OpenCV_INCLUDE_DIRS}>
    ${UI_BACKEND_INCLUDES}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang/compiler>
)
    target_compile_definitions(${lib} PRIVATE
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:ENABLE_HAVEL_LANG>
    $<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
    $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
    $<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
    $<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
    $<$<BOOL:${PULSEAUDIO_FOUND}>:HAVE_PULSEAUDIO>
    $<$<BOOL:${ALSA_FOUND}>:HAVE_ALSA>
    $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
    $<$<BOOL:${OpenCV_FOUND}>:HAVE_OPENCV>
    $<$<BOOL:${TESSERACT_FOUND}>:HAVE_TESSERACT>
    ${UI_BACKEND_DEFS}
    UI_BACKEND=\"${UI_BACKEND}\"
)
endforeach()

add_executable(havel
    src/main.cpp
)

target_link_libraries(havel PRIVATE
-Wl,--start-group
$<$<BOOL:${ENABLE_HAVEL_LANG}>:havel_lang>
havel_modules
$<$<BOOL:${ENABLE_QT_UI_BACKEND}>:havel_gui>
havel_core
-Wl,--end-group
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
 src/c/LoggerC.c
 src/c/Config.c
 src/dl/Loader.c
 )

add_executable(havel-lsp ${LSP_SOURCES})

set_target_properties(havel-lsp PROPERTIES
CXX_STANDARD 23
CXX_STANDARD_REQUIRED ON
CXX_EXTENSIONS OFF
C_STANDARD 17
C_STANDARD_REQUIRED ON
)

target_compile_options(havel-lsp PRIVATE -fno-lto)
target_link_options(havel-lsp PRIVATE -fno-lto)

target_include_directories(havel-lsp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/havel-lang
)

target_link_libraries(havel-lsp PRIVATE
nlohmann_json::nlohmann_json
Threads::Threads
fmt::fmt
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
 src/hvtest/test_scheduler_rig.cpp
 src/c/LoggerC.c
 src/c/Config.c
 )

add_executable(hvtest ${HVTEST_SOURCES})
set_target_properties(hvtest PROPERTIES
CXX_STANDARD 23
CXX_STANDARD_REQUIRED ON
CXX_EXTENSIONS OFF
C_STANDARD 17
C_STANDARD_REQUIRED ON
)

target_compile_options(hvtest PRIVATE -fno-lto)
target_link_options(hvtest PRIVATE -fno-lto)

target_include_directories(hvtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/havel-lang
)

target_compile_definitions(hvtest PRIVATE
$<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
$<$<BOOL:${ENABLE_MODULE_PLUGINS}>:ENABLE_MODULE_PLUGINS>
)

target_link_libraries(hvtest PRIVATE
-Wl,--start-group havel_lang havel_core havel_modules $<$<BOOL:${ENABLE_QT_UI_BACKEND}>:havel_gui> -Wl,--end-group
${COMMON_LIBS}
$<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
)

if(ENABLE_MODULE_PLUGINS)
set_target_properties(hvtest PROPERTIES ENABLE_EXPORTS ON)
target_link_options(hvtest PRIVATE -Wl,--export-dynamic)
endif()

add_executable(havel_api_test tests/test_embed_api.cpp)
set_target_properties(havel_api_test PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_compile_options(havel_api_test PRIVATE -fno-lto)
target_link_options(havel_api_test PRIVATE -fno-lto)
target_include_directories(havel_api_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/havel-lang
)
target_link_libraries(havel_api_test PRIVATE
-Wl,--start-group havel_lang havel_core havel_modules $<$<BOOL:${ENABLE_QT_UI_BACKEND}>:havel_gui> -Wl,--end-group
${COMMON_LIBS}
)

add_executable(brightness_test tests/test_brightness.cpp)
set_target_properties(brightness_test PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_compile_options(brightness_test PRIVATE -fno-lto)
target_link_options(brightness_test PRIVATE -fno-lto)
target_include_directories(brightness_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/havel-lang
)
target_link_libraries(brightness_test PRIVATE
-Wl,--start-group havel_lang havel_core havel_modules $<$<BOOL:${ENABLE_QT_UI_BACKEND}>:havel_gui> -Wl,--end-group
${COMMON_LIBS}
)

if(ENABLE_MODULE_PLUGINS)
set_target_properties(havel_api_test PROPERTIES ENABLE_EXPORTS ON)
target_link_options(havel_api_test PRIVATE -Wl,--export-dynamic)
set_target_properties(brightness_test PROPERTIES ENABLE_EXPORTS ON)
target_link_options(brightness_test PRIVATE -Wl,--export-dynamic)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  enable_testing()
  add_test(NAME hvtest-smoke COMMAND hvtest --smoke)
  if(ENABLE_COVERAGE)
    target_compile_options(hvtest PRIVATE --coverage)
    target_link_options(hvtest PRIVATE --coverage)
  endif()
endif()
endif()

# ==============================================================================
# PACKAGE MANAGER
# ==============================================================================
if(ENABLE_HAVEL_LANG)
add_executable(hpkg src/hpkg/main.cpp)
set_target_properties(hpkg PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_options(hpkg PRIVATE -fno-lto)
target_link_options(hpkg PRIVATE -fno-lto)
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
${CURL_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
    ${GENERATED_PROTO_DIR}
    $<$<BOOL:${TESSERACT_FOUND}>:${TESSERACT_INCLUDE_DIRS}>
    $<$<BOOL:${LEPTONICA_FOUND}>:${LEPTONICA_INCLUDE_DIRS}>
    $<$<BOOL:${OpenCV_FOUND}>:${OpenCV_INCLUDE_DIRS}>
    ${UI_BACKEND_INCLUDES}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
)

# Link libraries
target_link_libraries(havel PRIVATE
    ${COMMON_LIBS}
    $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:Qt6::Gui>
    $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:Qt6::Widgets>
    $<$<BOOL:${OpenCV_FOUND}>:${OPENCV_FFMPEG_WORKAROUND}>
    havel_extension_runtime
    $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
    $<$<BOOL:${HAVE_LIBFFI}>:ffi>
)

if(ENABLE_HAVEL_LANG)
    add_dependencies(havel havel_aot_core_shim)
endif()

# Compile definitions
target_compile_definitions(havel PRIVATE
$<$<BOOL:${ENABLE_HAVEL_LANG}>:ENABLE_HAVEL_LANG>
$<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
$<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
$<$<BOOL:${ENABLE_MODULE_PLUGINS}>:ENABLE_MODULE_PLUGINS>
$<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
    $<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
    $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
    $<$<BOOL:${OpenCV_FOUND}>:HAVE_OPENCV>
    $<$<BOOL:${TESSERACT_FOUND}>:HAVE_TESSERACT>
    ${UI_BACKEND_DEFS}
    UI_BACKEND=\"${UI_BACKEND}\"
)

# ==============================================================================
# HAVEL LANGUAGE LIBRARY (CONDITIONAL)
# ==============================================================================
if(ENABLE_HAVEL_LANG)
    set(HAVEL_LANG_LIB_SOURCES ${HAVEL_LANG_SOURCES})
    list(REMOVE_ITEM HAVEL_LANG_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/havel-lang/lsp/main.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/lsp/LanguageServer.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/tools/BytecodeSmokeMain.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/runtime/HostBridgeNew.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/Compiler.cpp
        ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/JIT.cpp
    )
# HostBridge.hpp inlines loadModule / isModuleAvailable → HostModuleLoader
    list(APPEND HAVEL_LANG_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/host/module/ModuleLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/havel-lang/runtime/PureStdLib.cpp
    ${CMAKE_SOURCE_DIR}/src/dl/Loader.c
    ${CMAKE_SOURCE_DIR}/src/c/LoggerC.c
    ${CMAKE_SOURCE_DIR}/src/c/Config.c
    ${CMAKE_SOURCE_DIR}/src/modules/brightness/BrightnessModule.cpp
)
    add_library(havel_lang STATIC ${HAVEL_LANG_LIB_SOURCES})

    set(HAVEL_LANG_CORE_SOURCES ${HAVEL_LANG_LIB_SOURCES})
list(REMOVE_ITEM HAVEL_LANG_CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/host/module/ModularHostBridges.cpp
  ${CMAKE_SOURCE_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp
  ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/LogModule.cpp
)
    add_library(havel_lang_core STATIC ${HAVEL_LANG_CORE_SOURCES})
    
    target_include_directories(havel_lang PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${X11_INCLUDE_DIR}
        ${XI_INCLUDE_DIR}
        ${DBUS_INCLUDE_DIRS}
        ${PULSEAUDIO_INCLUDE_DIRS}
        ${PIPEWIRE_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${WAYLAND_INCLUDE_DIRS}
        ${GENERATED_PROTO_DIR}
        $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
        $<$<BOOL:${HAVE_LIBFFI}>:${LIBFFI_INCLUDE_DIRS}>
        # Note: LLVM_INCLUDE_DIRS intentionally omitted here - Qt headers cause issues
        # to prevent LLVM 23 namespace __detail from polluting std::__detail in all TUs.
    )
    
target_link_libraries(havel_lang PRIVATE
${COMMON_LIBS}
$<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
$<$<BOOL:${HAVE_LIBFFI}>:ffi>
dl
havel_core
)

    target_link_libraries(havel_lang_core PRIVATE
${COMMON_LIBS}
$<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
$<$<BOOL:${HAVE_LIBFFI}>:ffi>
dl
havel_core
)

    target_include_directories(havel_lang_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${X11_INCLUDE_DIR}
        ${XI_INCLUDE_DIR}
 ${DBUS_INCLUDE_DIRS}
 ${PULSEAUDIO_INCLUDE_DIRS}
 ${PIPEWIRE_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${WAYLAND_INCLUDE_DIRS}
        ${GENERATED_PROTO_DIR}
        $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
        $<$<BOOL:${HAVE_LIBFFI}>:${LIBFFI_INCLUDE_DIRS}>
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
$<$<BOOL:${ENABLE_MODULE_PLUGINS}>:ENABLE_MODULE_PLUGINS>
$<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
$<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:HAVE_QT_EXTENSION>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_CORE_LIB>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_GUI_LIB>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_WIDGETS_LIB>
$<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
)

    target_compile_definitions(havel_lang_core PRIVATE
$<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
$<$<BOOL:${ENABLE_MODULE_PLUGINS}>:ENABLE_MODULE_PLUGINS>
$<$<BOOL:${READLINE_LIB}>:HAVE_READLINE>
$<$<BOOL:${PIPEWIRE_FOUND}>:HAVE_PIPEWIRE>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:HAVE_QT_EXTENSION>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_CORE_LIB>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_GUI_LIB>
$<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_WIDGETS_LIB>
$<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>
HAVEL_CORE_PROFILE
)

    # -----------------------------------------------------------------------
    # Note: LLVM_INCLUDE_DIRS handling is applied globally at source assembly
    # stage to all targets that use HAVEL_LANG_SOURCES (including tests).
    # This prevents LLVM 23's bare `namespace __detail` from polluting
    # std::__detail across every translation unit in havel_lang.
    # -----------------------------------------------------------------------

endif()

# ==============================================================================
# MODULE PLUGINS (conditional .so modules loaded via dlopen)
# ==============================================================================
if(ENABLE_MODULE_PLUGINS AND ENABLE_HAVEL_LANG)
set(MODULE_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/modules")
file(MAKE_DIRECTORY ${MODULE_PLUGIN_OUTPUT_DIR})

# Helper function to define a module plugin .so target
# Usage: add_havel_module_plugin(name source [EXTRA_DEFS def1 def2 ...])
function(add_havel_module_plugin MODULE_NAME MODULE_SOURCE)
  set(MODULE_TARGET "havel_mod_${MODULE_NAME}")
  add_library(${MODULE_TARGET} MODULE ${MODULE_SOURCE})
  set_target_properties(${MODULE_TARGET} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${MODULE_PLUGIN_OUTPUT_DIR}"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION OFF
  )
  target_compile_options(${MODULE_TARGET} PRIVATE -fno-lto)
  target_link_options(${MODULE_TARGET} PRIVATE -fno-lto)
  target_compile_definitions(${MODULE_TARGET} PRIVATE
    HAVEL_MODULE_PLUGIN
    ENABLE_HAVEL_LANG
  )
  # Parse optional EXTRA_DEFS keyword
  set(_extra_defs "")
  if(ARGC GREATER 2)
    set(_parsing_defs FALSE)
    foreach(_arg ${ARGN})
      if(_arg STREQUAL "EXTRA_DEFS")
        set(_parsing_defs TRUE)
      elseif(_parsing_defs)
        list(APPEND _extra_defs ${_arg})
      endif()
    endforeach()
  endif()
  foreach(_def ${_extra_defs})
    target_compile_definitions(${MODULE_TARGET} PRIVATE ${_def})
  endforeach()
  target_include_directories(${MODULE_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/havel-lang
    ${CMAKE_SOURCE_DIR}/src/extensions
  )
  # Module .so resolves symbols from the main executable at runtime
  # via --export-dynamic. Do NOT link static libs -- they duplicate globals.
  target_link_libraries(${MODULE_TARGET} PRIVATE
    fmt::fmt
  )
  # Allow unresolved symbols at link time (resolved from main exe at runtime)
  target_link_options(${MODULE_TARGET} PRIVATE
    -Wl,--allow-shlib-undefined
  )
  install(TARGETS ${MODULE_TARGET}
    LIBRARY DESTINATION lib/havel/modules
    COMPONENT Runtime
  )
endfunction()

# -- stdlib module plugins -------------------------------------------------
add_havel_module_plugin(log          ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/LogModule.cpp)
add_havel_module_plugin(array        ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/ArrayModule.cpp)
add_havel_module_plugin(bit          ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/BitModule.cpp)
add_havel_module_plugin(browser      ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/BrowserModule.cpp)
add_havel_module_plugin(bytecodebuilder ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/BytecodeBuilderModule.cpp)
add_havel_module_plugin(fmt          ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/FormatModule.cpp)
add_havel_module_plugin(fs           ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/FsModule.cpp)
add_havel_module_plugin(http ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/HttpModule.cpp)
# HotkeyModule stays in main binary — host bridges use its C++ class directly
add_havel_module_plugin(math ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/MathModule.cpp)
add_havel_module_plugin(object       ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/ObjectModule.cpp)
add_havel_module_plugin(option       ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/OptionModule.cpp)
add_havel_module_plugin(pack         ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/PackModule.cpp)
add_havel_module_plugin(ptr          ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/PointerModule.cpp)
add_havel_module_plugin(random       ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/RandomModule.cpp)
add_havel_module_plugin(regex        ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/RegexModule.cpp)
add_havel_module_plugin(shell        ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/ShellModule.cpp)
add_havel_module_plugin(string       ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/StringModule.cpp)
add_havel_module_plugin(sys          ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/SysModule.cpp)
add_havel_module_plugin(time         ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/TimeModule.cpp)
add_havel_module_plugin(timer        ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/TimerModule.cpp)
add_havel_module_plugin(type         ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/TypeModule.cpp)

# -- host module plugins ---------------------------------------------------
add_havel_module_plugin(alttab       ${CMAKE_SOURCE_DIR}/src/modules/alttab/AltTabModule.cpp)
add_havel_module_plugin(app          ${CMAKE_SOURCE_DIR}/src/modules/app/AppModule.cpp)
add_havel_module_plugin(audio        ${CMAKE_SOURCE_DIR}/src/modules/audio/AudioModule.cpp)
add_havel_module_plugin(automation   ${CMAKE_SOURCE_DIR}/src/modules/automation/AutomationModule.cpp)
add_havel_module_plugin(pixel        ${CMAKE_SOURCE_DIR}/src/modules/automation/PixelModule.cpp)
add_havel_module_plugin(brightness   ${CMAKE_SOURCE_DIR}/src/modules/brightness/BrightnessModule.cpp)
add_havel_module_plugin(clipboard    ${CMAKE_SOURCE_DIR}/src/modules/clipboard/ClipboardModule.cpp)
add_havel_module_plugin(historyclipboard ${CMAKE_SOURCE_DIR}/src/modules/clipboard/HistoryClipboardModule.cpp)
add_havel_module_plugin(monitoringclipboard ${CMAKE_SOURCE_DIR}/src/modules/clipboard/MonitoringClipboardModule.cpp)
add_havel_module_plugin(config       ${CMAKE_SOURCE_DIR}/src/modules/config/ConfigModule.cpp)
add_havel_module_plugin(display      ${CMAKE_SOURCE_DIR}/src/modules/display/DisplayModule.cpp)
add_havel_module_plugin(ffi          ${CMAKE_SOURCE_DIR}/src/modules/ffi/FFIModule.cpp
  EXTRA_DEFS $<$<BOOL:${HAVE_LIBFFI}>:HAVE_LIBFFI>)
target_sources(havel_mod_ffi PRIVATE
  ${CMAKE_SOURCE_DIR}/src/havel-lang/ffi/FFIMemory.cpp
  ${CMAKE_SOURCE_DIR}/src/havel-lang/ffi/FFICall.cpp
  ${CMAKE_SOURCE_DIR}/src/havel-lang/ffi/FFITypes.cpp
)
target_link_libraries(havel_mod_ffi PRIVATE ${LIBFFI_LIBRARIES})
add_havel_module_plugin(filemanager  ${CMAKE_SOURCE_DIR}/src/modules/filesystem/FileManagerModule.cpp)
add_havel_module_plugin(help         ${CMAKE_SOURCE_DIR}/src/modules/help/HelpModule.cpp)
add_havel_module_plugin(image        ${CMAKE_SOURCE_DIR}/src/modules/image/ImageModule.cpp)
add_havel_module_plugin(io           ${CMAKE_SOURCE_DIR}/src/modules/io/IOModule.cpp)
add_havel_module_plugin(mapmanager   ${CMAKE_SOURCE_DIR}/src/modules/mapmanager/MapManagerModule.cpp)
add_havel_module_plugin(media        ${CMAKE_SOURCE_DIR}/src/modules/media/MediaModule.cpp)
add_havel_module_plugin(mode         ${CMAKE_SOURCE_DIR}/src/modules/mode/ModeModule.cpp)
add_havel_module_plugin(mouse        ${CMAKE_SOURCE_DIR}/src/modules/mouse/MouseModule.cpp)
add_havel_module_plugin(textchunker  ${CMAKE_SOURCE_DIR}/src/modules/textchunker/TextChunkerModule.cpp)
add_havel_module_plugin(ui           ${CMAKE_SOURCE_DIR}/src/modules/ui/UIModule.cpp)
add_havel_module_plugin(window ${CMAKE_SOURCE_DIR}/src/modules/window/WindowMonitorModule.cpp)

add_havel_module_plugin(wayland ${CMAKE_SOURCE_DIR}/src/havel-lang/stdlib/WaylandModule.cpp)
target_include_directories(havel_mod_wayland PRIVATE
    ${CMAKE_BINARY_DIR}/generated/wayland
)
target_compile_definitions(havel_mod_wayland PRIVATE
    $<$<BOOL:${HAS_EXT_DATA_CONTROL}>:HAS_EXT_DATA_CONTROL>
)

# Qt-dependent modules
if(ENABLE_QT)
  add_havel_module_plugin(clipboardmgr ${CMAKE_SOURCE_DIR}/src/modules/clipboardmgr/ClipboardMgrModule.cpp)
  get_target_property(_qt_core_inc Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(_qt_gui_inc Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(_qt_widgets_inc Qt6::Widgets INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(havel_mod_clipboardmgr PRIVATE ${_qt_core_inc} ${_qt_gui_inc} ${_qt_widgets_inc})
  # screenshot module has const auto& lambda incompatibility with std::function — skip as .so for now
  # add_havel_module_plugin(screenshot ${CMAKE_SOURCE_DIR}/src/modules/screenshot/ScreenshotModule.cpp)
endif()

# Export symbols from main executable so plugins can resolve them
set_target_properties(havel PROPERTIES
  ENABLE_EXPORTS ON
)
target_link_options(havel PRIVATE -Wl,--export-dynamic)
# -fvisibility=hidden + --gc-sections strips VM symbols that .so plugins need.
# Remove gc-sections so dlopen(RTLD_NOW) can resolve them at runtime.
target_link_options(havel PRIVATE -Wl,--no-gc-sections)
endif()

# Dedicated AOT core runtime shim (lean entrypoint surface for --profile core).
if(ENABLE_HAVEL_LANG)
    set_target_properties(havel_lang PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION OFF
    )

    add_library(havel_aot_core_shim STATIC
        src/havel-lang/runtime/AotCoreShim.cpp
        src/havel-lang/runtime/CoreRuntimeExports.cpp
    )
    target_include_directories(havel_aot_core_shim PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/havel-lang
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(havel_aot_core_shim PRIVATE
        havel_lang_core
    )
    set_target_properties(havel_aot_core_shim PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF
    )
    target_compile_options(havel_aot_core_shim PRIVATE -fno-lto)
endif()

# ==============================================================================
# TEST EXECUTABLES (CONDITIONAL)
# ==============================================================================
if(ENABLE_TESTS AND GTest_FOUND AND TEST_SOURCES)
    foreach(test_file ${TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)

        add_executable(${test_name}
                ${test_file}
                ${CORE_SOURCES}
                ${GUI_SOURCES}
                $<$<NOT:$<BOOL:${ENABLE_MODULE_PLUGINS}>>:${HOST_MODULES_SOURCES}>
                $<$<BOOL:${ENABLE_HAVEL_LANG}>:${HAVEL_LANG_SOURCES}>
        )
        
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/include
            ${X11_INCLUDE_DIR}
            ${XI_INCLUDE_DIR}
 ${DBUS_INCLUDE_DIRS}
 ${CURL_INCLUDE_DIRS}
            ${PULSEAUDIO_INCLUDE_DIRS}
            ${PIPEWIRE_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
  ${WAYLAND_INCLUDE_DIRS}
  ${GENERATED_PROTO_DIR}
            $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
            $<$<BOOL:${ENABLE_LLVM}>:${LLVM_INCLUDE_DIRS}>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:/usr/include/qt6>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:/usr/include/qt6/QtCore>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:/usr/include/qt6/QtWidgets>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:/usr/include/qt6/QtGui>
        )
        
target_link_libraries(${test_name} PRIVATE
${COMMON_LIBS}
GTest::gtest
            GTest::gtest_main 
            Qt6::Test
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:Qt6::Widgets>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:Qt6::Gui>
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
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:HAVE_QT_EXTENSION>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_CORE_LIB>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_GUI_LIB>
            $<$<AND:$<BOOL:${QT_BACKEND_AVAILABLE}>,$<BOOL:${ENABLE_QT_UI_BACKEND}>>:QT_WIDGETS_LIB>
        )
        
 # Add to CTest
 add_test(NAME ${test_name} COMMAND ${test_name})

 if(ENABLE_COVERAGE)
 target_compile_options(${test_name} PRIVATE --coverage)
 target_link_options(${test_name} PRIVATE --coverage)
 endif()
 endforeach()
    
    # Special GUI test if it exists
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/TestGUI.cpp" AND ENABLE_QT_UI_BACKEND)
        add_executable(test_gui tests/TestGUI.cpp)
        
target_link_libraries(test_gui PRIVATE
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

if(TARGET hpkg)
install(TARGETS hpkg
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
message(STATUS "LLVM JIT: ✓ ENABLED (${LLVM_PACKAGE_VERSION})")
else()
message(STATUS "LLVM JIT: ✗ DISABLED")
endif()

if(ENABLE_MODULE_PLUGINS)
message(STATUS "Module Plugins: ✓ ENABLED")
else()
message(STATUS "Module Plugins: ✗ DISABLED")
endif()

 if(ENABLE_TESTS AND GTest_FOUND AND TEST_SOURCES)
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
message(STATUS "Qt6: ${Qt6_VERSION}")
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
if(TARGET hpkg)
message(STATUS "Package manager: hpkg")
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
  if(PIPEWIRE_FOUND)
    target_compile_definitions(havel PRIVATE HAVE_PIPEWIRE)
  endif()
if(TARGET havel_lang)
    target_include_directories(havel_lang PRIVATE
        ${CMAKE_SOURCE_DIR}/src/havel-lang
    )
endif()
