cmake_minimum_required(VERSION 3.16)  # Bumped for better glob support
project(HvC VERSION 1.0.0 LANGUAGES CXX)

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


if(ENABLE_LLVM)
    message(STATUS "🚀 Enabling LLVM JIT compilation for Havel language!")

    # Find LLVM installation
    find_package(LLVM REQUIRED CONFIG)

    if(LLVM_FOUND)
        message(STATUS "✅ Found LLVM ${LLVM_PACKAGE_VERSION}")
        message(STATUS "📁 Using LLVMConfig.cmake in: ${LLVM_DIR}")
        message(STATUS "🎯 LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
        message(STATUS "🔗 LLVM library dirs: ${LLVM_LIBRARY_DIRS}")
        message(STATUS "🏗️  LLVM definitions: ${LLVM_DEFINITIONS}")

        # Add LLVM include directories and definitions
        include_directories(${LLVM_INCLUDE_DIRS})
        add_definitions(${LLVM_DEFINITIONS})

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
# Find required packages
find_package(X11 REQUIRED)
find_package(Lua REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)

# Check for XRandR extension for multi-monitor support
find_package(X11 COMPONENTS Xrandr Xtst)
if(X11_Xrandr_FOUND)
    add_compile_definitions(HAVE_XRANDR)
    message(STATUS "XRandR found, enabling multi-monitor support")
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

# Havel Language Test
add_executable(test_havel
        src/havel-lang/tests/test_havel.cpp
        ${HAVEL_LANG_SOURCES}
        ${HAVEL_LANG_HEADERS}
        ${CORE_SOURCES}
        ${CORE_HEADERS}
)

target_link_libraries(test_havel ${COMMON_LIBS})
target_compile_definitions(test_havel PRIVATE
        $<$<CONFIG:Debug>:DEBUG_BUILD>
        $<$<CONFIG:Release>:RELEASE_BUILD>
)

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