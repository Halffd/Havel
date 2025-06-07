cmake_minimum_required(VERSION 3.16)  # Bumped for better glob support
project(HvC VERSION 1.0.0 LANGUAGES C CXX)  # Enable both C and C++

# Enable C++23 standard with GNU extensions for better compatibility
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable C11 standard for C code
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()


# Compiler detection and selection
option(USE_CLANG "Force use of Clang compiler" ON)

if(USE_CLANG)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    message(STATUS "Forcing Clang compiler usage")
endif()

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Compiler-specific optimizations
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Using Clang with AGGRESSIVE optimizations!")

    # Debug Profile
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG -Wall -Wextra -Wno-unused-parameter")

    # Release Profile
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto=thin \
        -DNDEBUG -ffast-math -funroll-loops -fvectorize -fslp-vectorize \
        -fomit-frame-pointer -finline-functions -fprefetch-loop-arrays \
        -fuse-ld=lld -Wno-unused-command-line-argument")

    # Clang-specific LTO and linker optimizations
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto=thin -fuse-ld=lld -Wl,--icf=all")

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Using GCC with optimizations")

    # Debug Profile
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG -Wall -Wextra")

    # Release Profile - GCC optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto \
        -DNDEBUG -ffast-math -funroll-loops -finline-functions \
        -fprefetch-loop-arrays -fomit-frame-pointer -ftree-vectorize \
        -mavx2 -mfma -mf16c")

else()
    message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# LLVM Configuration
if(ENABLE_LLVM)
    message(STATUS " Enabling LLVM JIT compilation for Havel language!")

    # Find LLVM installation
    set(LLVM_DIR "/usr/lib/cmake/llvm")
    find_package(LLVM REQUIRED CONFIG)
    
    # Set LLVM components we need
    set(LLVM_COMPONENTS
        Core
        ExecutionEngine
        Interpreter
        MCJIT
        OrcJIT
        RuntimeDyld
        X86CodeGen
        X86AsmPrinter
        X86Desc
        X86Info
        X86Utils
        Support
        Target
        TransformUtils
        Analysis
        Object
        BitWriter
        IRReader
        Linker
        Passes
        ScalarOpts
        Vectorize
        InstCombine
        Instrumentation
        MC
        MCParser
        MCDisassembler
        DebugInfoDWARF
        DebugInfoMSF
        DebugInfoCodeView
        DebugInfoPDB
        Symbolize
        Demangle
    )
    
    # Find LLVM components
    llvm_map_components_to_libnames(LLVM_LIBS ${LLVM_COMPONENTS})

    if(LLVM_FOUND)
        message(STATUS " Found LLVM ${LLVM_PACKAGE_VERSION}")
        message(STATUS " Using LLVMConfig.cmake in: ${LLVM_DIR}")
        message(STATUS " LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
        message(STATUS " LLVM library dirs: ${LLVM_LIBRARY_DIRS}")
        message(STATUS " LLVM definitions: ${LLVM_DEFINITIONS}")
        message(STATUS "🔗 LLVM library dirs: ${LLVM_LIBRARY_DIRS}")
        message(STATUS "🏗️  LLVM definitions: ${LLVM_DEFINITIONS}")

        # Add definitions to handle X11/LLVM macro conflicts
        add_definitions(${LLVM_DEFINITIONS})
        add_definitions(-DLLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING=1)
        
        # Ensure we compile with C++17 or later
        set(CMAKE_CXX_STANDARD 17)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)
        
        # Add warning flags
        if(MSVC)
            add_compile_options(/W4 /WX)
        else()
            add_compile_options(-Wall -Wextra -Wpedantic -Werror)
        endif()
        
        # Create a custom target for LLVM includes to ensure proper ordering
        add_library(llvm_includes INTERFACE)
        target_include_directories(llvm_includes SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
        
        # Add LLVM includes with SYSTEM flag to prevent macro conflicts
        target_include_directories(havel_lang PRIVATE $<TARGET_PROPERTY:llvm_includes,INTERFACE_INCLUDE_DIRECTORIES>)
        
        # Add LLVM libraries
        target_link_libraries(havel_lang PRIVATE ${LLVM_LIBS})
        
        # Add LLVM definitions to the target
        target_compile_definitions(havel_lang PRIVATE ${LLVM_DEFINITIONS})

        # Separate LLVM definitions based on build type
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_definitions(-DLLVM_DEBUG_BUILD)
        endif()

        # Map LLVM components to library names
        llvm_map_components_to_libnames(LLVM_LIBS
                # Core LLVM libraries
                support                 # Basic LLVM support
                core                   # LLVM Core (Module, Function, etc.)
                irreader              # LLVM IR reader

                # Execution engines
                executionengine       # Base execution engine
                interpreter           # LLVM interpreter (fallback)
                mcjit                 # MC-based JIT compiler (FAST!)
                orcjit                # On-Request-Compilation JIT (ULTRA FAST!)

                # Code generation - NATIVE MACHINE CODE! 💪
                native                # Native target support
                nativecodegen         # Native code generation

                # Target support
                x86                   # x86 target support
                x86asmprinter         # x86 assembly printing
                x86codegen            # x86 code generation
                x86desc               # x86 target description
                x86info               # x86 target info

                # Optimization passes
                instcombine           # Instruction combining
                scalaropts            # Scalar optimizations
                ipo                   # Inter-procedural optimizations
                vectorize             # Auto-vectorization

                # Analysis passes
                analysis              # Analysis utilities
                transformutils        # Transformation utilities

                # Object file support
                object                # Object file reading/writing
                mcparser              # MC assembly parser
                mcdisassembler        # MC disassembler
        )

        message(STATUS "🎯 LLVM libraries: ${LLVM_LIBS}")

        # Add compiler definition to enable LLVM features
        add_definitions(-DHAVEL_ENABLE_LLVM)

        # LLVM requires C++17 minimum✨
        if(LLVM_CXX_STANDARD GREATER 23)
            message(WARNING "LLVM requires C++${LLVM_CXX_STANDARD}, but using C++23")
        endif()

    else()
        message(FATAL_ERROR "❌ LLVM not found! Install with: sudo pacman -S llvm llvm-libs")
    endif()
else()
    message(STATUS "⚠️  LLVM JIT compilation disabled")
    set(LLVM_LIBS "")
endif()

# Find X11 and related libraries
# First find the base X11 package
find_package(X11 REQUIRED)
message(STATUS "X11_INCLUDE_DIR: ${X11_INCLUDE_DIR}")
message(STATUS "X11_LIBRARIES: ${X11_LIBRARIES}")

# Find X11 extensions
find_package(X11 COMPONENTS X11 Xext Xrandr Xtst)

# Manually find other X11 components if needed
find_library(X11_X11_LIB X11)
find_library(X11_Xext_LIB Xext)
find_library(X11_Xrandr_LIB Xrandr)
find_library(X11_Xtst_LIB Xtst)
find_library(X11_Xinerama_LIB Xinerama)
find_library(X11_Xcursor_LIB Xcursor)
find_library(X11_Xfixes_LIB Xfixes)
find_library(X11_Xrender_LIB Xrender)

# Set X11 variables if not found by find_package
if(NOT X11_X11_LIB)
    set(X11_X11_LIB ${X11_LIBRARIES})
endif()

# Find additional system libraries
find_library(DL_LIBRARY dl)
find_library(RT_LIBRARY rt)

# Find Lua
find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_search_module(LUA lua5.4 lua-5.4 lua54 lua5.3 lua-5.3 lua53 lua)
    if(LUA_FOUND)
        set(LUA_INCLUDE_DIR ${LUA_INCLUDE_DIRS})
        set(LUA_LIBRARIES ${LUA_LIBRARIES})
    else()
        message(FATAL_ERROR "Lua 5.4 not found")
    endif()
endif()

# Find Cairo
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO QUIET cairo)
if(NOT CAIRO_FOUND)
    message(FATAL_ERROR "Cairo not found")
endif()

# Print found libraries
message(STATUS "✅ Found X11: ${X11_LIBRARIES}")
message(STATUS "✅ Found X11_X11_LIB: ${X11_X11_LIB}")
message(STATUS "✅ Found X11_Xext_LIB: ${X11_Xext_LIB}")
message(STATUS "✅ Found X11_Xrandr_LIB: ${X11_Xrandr_LIB}")
message(STATUS "✅ Found X11_Xtst_LIB: ${X11_Xtst_LIB}")
message(STATUS "✅ Found Lua: ${LUA_LIBRARIES}")
message(STATUS "✅ Found Cairo: ${CAIRO_LIBRARIES}")

# Set include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/havel-lang  # For havel-lang includes
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
    ${LUA_INCLUDE_DIR}
    ${CAIRO_INCLUDE_DIRS}
)

# Set link libraries
set(COMMON_LIBS
    ${X11_LIBRARIES}
    ${X11_Xrandr_LIB}
    ${X11_Xtst_LIB}
    ${X11_Xinerama_LIB}
    ${X11_Xcursor_LIB}
    ${X11_Xfixes_LIB}
    ${X11_Xrender_LIB}
    ${X11_Xext_LIB}
    ${X11_X11_LIB}
    ${DL_LIBRARY}
    ${RT_LIBRARY}
    ${LUA_LIBRARIES}
    ${CAIRO_LIBRARIES}
    pthread
    dl
    lua5.4
)

message(STATUS "✅ Found X11: ${X11_INCLUDE_DIR}")
message(STATUS "✅ Found XRandR: ${X11_Xrandr_LIB}")
message(STATUS "✅ Found Xtst: ${X11_Xtst_LIB}")
message(STATUS "✅ Found Lua: ${LUA_INCLUDE_DIR}")
message(STATUS "✅ Found Cairo: ${CAIRO_INCLUDE_DIRS}")

if(X11_Xrandr_FOUND)
    message(STATUS "✅ XRandR found, enabling multi-monitor support")
    add_definitions(-DHAVE_XRANDR)
endif()

# Add include directories
include_directories(
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/include
        ${LUA_INCLUDE_DIR}
        ${CAIRO_INCLUDE_DIRS}
)

# Recursively glob all source files
file(GLOB_RECURSE CORE_SOURCES
        "src/*.cpp"
)

file(GLOB_RECURSE CORE_HEADERS
        "src/*.h"
)

# Havel Language Sources (recursive glob)
file(GLOB_RECURSE HAVEL_LANG_SOURCES
        "src/havel-lang/*.cpp"
)

file(GLOB_RECURSE HAVEL_LANG_HEADERS
        "src/havel-lang/*.hpp"
        "src/havel-lang/*.h"
)

# Test sources
file(GLOB_RECURSE TEST_SOURCES
        "src/tests/*.cpp"
)

# Common libraries to link
set(COMMON_LIBS
        ${X11_LIBRARIES}
        ${LUA_LIBRARIES}
        ${CAIRO_LIBRARIES}
        pthread
)

# Add XTest library if found
if(X11_Xtst_FOUND)
    list(APPEND COMMON_LIBS ${X11_Xtst_LIB})
endif()

# Add XRandR library if found
if(X11_Xrandr_FOUND)
    list(APPEND COMMON_LIBS ${X11_Xrandr_LIB})
endif()

# Main executable (HvC)
add_executable(hvc
        src/main.cpp
        ${CORE_SOURCES}
        ${CORE_HEADERS}
)

target_link_libraries(hvc ${COMMON_LIBS})
target_compile_definitions(hvc PRIVATE
        $<$<CONFIG:Debug>:DEBUG_BUILD>
        $<$<CONFIG:Release>:RELEASE_BUILD>
)

# Hotkey test executable
if(TEST_SOURCES)
    add_executable(hotkey_test
            ${TEST_SOURCES}
            ${CORE_SOURCES}
            ${CORE_HEADERS}
    )

    target_compile_definitions(hotkey_test PRIVATE
            RUN_TESTS
            $<$<CONFIG:Debug>:DEBUG_BUILD>
    )

    target_link_libraries(hotkey_test ${COMMON_LIBS})
endif()

# Havel Language Test - Include all necessary source files
add_executable(test_havel
        # Core Havel Language
        src/havel-lang/ast/AST.cpp
        src/havel-lang/compiler/Compiler.cpp
        src/havel-lang/compiler/JIT.cpp
        src/havel-lang/lexer/Lexer.cpp
        src/havel-lang/parser/Parser.cpp
        src/havel-lang/runtime/Clipboard.cpp
        src/havel-lang/runtime/Engine.cpp
        src/havel-lang/runtime/Interpreter.cpp
        src/havel-lang/tests/test_havel.cpp
        
        # Core functionality
        src/core/DisplayManager.cpp
        src/core/IO.cpp
        src/core/ScriptEngine.cpp
        src/core/SocketServer.cpp
        
        # Window management
        src/window/Window.cpp
        src/window/WindowManager.cpp
        src/window/WindowManagerDetector.cpp
        src/window/WindowMonitor.cpp
        src/window/CompizManager.cpp
        
        # Utils
        src/utils/Logger.cpp
        src/utils/Printer.cpp
        src/utils/Utils.cpp
        src/utils/Util.cpp
        
        # Media
        src/media/MPVController.cpp
        src/media/MediaControl.cpp
)

# Add include directories
target_include_directories(test_havel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
)

# Add definition to disable GUI components
target_compile_definitions(test_havel PRIVATE 
    DISABLE_GUI
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:RELEASE_BUILD>
)

# Link required libraries
target_link_libraries(test_havel
    ${X11_LIBRARIES}
    ${X11_Xtst_LIB}
    ${X11_Xrandr_LIB}
    ${X11_Xinerama_LIB}
    ${X11_Xcursor_LIB}
    ${X11_Xfixes_LIB}
    ${X11_Xrender_LIB}
    ${X11_Xext_LIB}
    ${X11_X11_LIB}
    ${CMAKE_DL_LIBS}
    pthread
    dl
    lua5.4
)

# Add LLVM components if enabled
if(ENABLE_LLVM)
    target_include_directories(test_havel PRIVATE ${LLVM_INCLUDE_DIRS})
    target_link_libraries(test_havel ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS})
    target_compile_definitions(test_havel PRIVATE HAVEL_ENABLE_LLVM)
endif()

target_link_libraries(test_havel ${COMMON_LIBS})

# Havel Language Library (optional - for modular builds)
add_library(havel_lang STATIC
        ${HAVEL_LANG_SOURCES}
        ${HAVEL_LANG_HEADERS}
)

target_link_libraries(havel_lang ${COMMON_LIBS})

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Install targets
install(TARGETS hvc test_havel
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

# Install hotkey_test if it exists
if(TARGET hotkey_test)
    install(TARGETS hotkey_test RUNTIME DESTINATION bin)
endif()

# Optional: Create a custom target for cleaning build artifacts
add_custom_target(clean-all
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
        COMMENT "Removing all build artifacts"
)

# Print found files (for debugging)
message(STATUS "Found ${CMAKE_CURRENT_LIST_DIR}")
message(STATUS "Core sources: ${CORE_SOURCES}")
message(STATUS "Havel sources: ${HAVEL_LANG_SOURCES}")