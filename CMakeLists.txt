cmake_minimum_required(VERSION 3.16)
project(Havel VERSION 1.0.0 LANGUAGES C CXX)

# ==============================================================================
# BUILD OPTIONS - Define these FIRST before any logic
# ==============================================================================
option(USE_CLANG "Force use of Clang compiler" ON)
option(ENABLE_LLVM "Enable LLVM JIT compilation" OFF)
option(ENABLE_TESTS "Enable building tests" ON)
option(ENABLE_HAVEL_LANG "Enable Havel language compilation" ON)

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
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto=thin -DNDEBUG -ffast-math")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto=thin -fuse-ld=lld")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG -ffast-math")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")
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

# D-Bus
pkg_check_modules(DBUS REQUIRED dbus-1)

# Lua (try 5.4 first, fallback to pkg-config)
find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    pkg_search_module(LUA REQUIRED lua5.4 lua-5.4 lua54 lua)
    if(NOT LUA_FOUND)
        message(FATAL_ERROR "Lua 5.4 not found. Install lua5.4-dev or lua-devel")
    endif()
endif()

# X11 extensions
find_library(XTEST_LIB Xtst REQUIRED)
find_library(XRANDR_LIB Xrandr REQUIRED)  
find_library(XINERAMA_LIB Xinerama REQUIRED)

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
# QT6 GUI FRAMEWORK (REQUIRED)
# ==============================================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) 
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Test)

# Handle X11/Qt keyword conflicts in MOC
set(CMAKE_AUTOMOC_MOC_OPTIONS 
    "-DQT_NO_KEYWORDS"
    "-DMOC_PARSING"
    "-DNone=0L"
    "-DTrue=1"
    "-DFalse=0"
    "-DStatus=int"
    "-DBool=int" 
    "-DSuccess=0"
)

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
        
        # Get LLVM libraries using llvm-config
        execute_process(
            COMMAND llvm-config --libs core support mcjit native
            OUTPUT_VARIABLE LLVM_LIBS_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        
        execute_process(
            COMMAND llvm-config --system-libs
            OUTPUT_VARIABLE LLVM_SYSTEM_LIBS_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        
        # Clean and convert to lists
        string(STRIP "${LLVM_LIBS_OUTPUT}" LLVM_LIBS_CLEAN)
        string(STRIP "${LLVM_SYSTEM_LIBS_OUTPUT}" LLVM_SYSTEM_LIBS_CLEAN)
        separate_arguments(LLVM_LIBS_LIST UNIX_COMMAND "${LLVM_LIBS_CLEAN}")
        separate_arguments(LLVM_SYSTEM_LIBS_LIST UNIX_COMMAND "${LLVM_SYSTEM_LIBS_CLEAN}")
        
        set(LLVM_LIBS ${LLVM_LIBS_LIST} ${LLVM_SYSTEM_LIBS_LIST})
        
        message(STATUS "  LLVM libraries: ${LLVM_LIBS_LIST}")
        message(STATUS "  System libraries: ${LLVM_SYSTEM_LIBS_LIST}")
        
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
# Core sources (always required)
file(GLOB_RECURSE CORE_SOURCES 
    "src/core/*.cpp"
    "src/window/*.cpp" 
    "src/utils/*.cpp"
    "src/fs/*.cpp"
    "src/process/*.cpp"
    "src/media/*.cpp"
    "src/io/*.cpp"
)

# GUI sources (always included)
file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
list(FILTER GUI_SOURCES EXCLUDE REGEX "main\\.cpp$")

# Havel language sources (conditional)
set(HAVEL_LANG_SOURCES "")
if(ENABLE_HAVEL_LANG)
    file(GLOB_RECURSE HAVEL_LANG_SOURCES "src/havel-lang/*.cpp")
endif()

# Test sources (conditional)
set(TEST_SOURCES "")
if(ENABLE_TESTS)
    file(GLOB_RECURSE TEST_SOURCES "src/tests/*.cpp")
endif()

# ==============================================================================
# LIBRARY CONFIGURATION
# ==============================================================================
pkg_check_modules(XI REQUIRED xi)

# PulseAudio
pkg_check_modules(PULSEAUDIO libpulse)

# ALSA
pkg_check_modules(ALSA alsa)
set(COMMON_LIBS
    # System libraries
    ${X11_LIBRARIES}
    ${XI_LIBRARIES}
    ${XTEST_LIB}
    ${XRANDR_LIB} 
    ${XINERAMA_LIB}
    ${PULSEAUDIO_LIBRARIES}
    ${ALSA_LIBRARIES}
    Threads::Threads
    ${DBUS_LIBRARIES}
    ${LUA_LIBRARIES}
    # Qt6 libraries
    Qt6::Core
    Qt6::Widgets
    Qt6::Charts
    # Wayland support
    wayland-protos
    ${WAYLAND_LIBRARIES}
    # System libraries
    dl crypto zip ssl pthread m
)

# ==============================================================================
# MAIN EXECUTABLE
# ==============================================================================
add_executable(havel 
    src/main.cpp
    ${CORE_SOURCES}
    ${GUI_SOURCES}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${HAVEL_LANG_SOURCES}>
)

# Set target properties
set_target_properties(havel PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories
target_include_directories(havel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
    ${XI_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
    ${WAYLAND_INCLUDE_DIRS}
    ${GENERATED_PROTO_DIR}
    $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
    $<$<BOOL:${ENABLE_LLVM}>:${LLVM_INCLUDE_DIRS}>
)

# Link libraries
target_link_libraries(havel PRIVATE 
    ${COMMON_LIBS}
    $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
)

# Compile definitions
target_compile_definitions(havel PRIVATE
    $<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
    $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
)

# ==============================================================================
# HAVEL LANGUAGE LIBRARY (CONDITIONAL)
# ==============================================================================
if(ENABLE_HAVEL_LANG)
    add_library(havel_lang STATIC ${HAVEL_LANG_SOURCES})
    
    target_include_directories(havel_lang PRIVATE
        ${CMAKE_SOURCE_DIR}/src/havel-lang
        $<$<BOOL:${ENABLE_LLVM}>:${LLVM_INCLUDE_DIRS}>
    )
    
    target_link_libraries(havel_lang PRIVATE 
        ${COMMON_LIBS}
        $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
    )
    
    target_compile_definitions(havel_lang PRIVATE
        $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
    )
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
            $<$<BOOL:${ENABLE_HAVEL_LANG}>:${HAVEL_LANG_SOURCES}>
        )
        
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/include
            ${X11_INCLUDE_DIR}
            ${DBUS_INCLUDE_DIRS}
            ${LUA_INCLUDE_DIR}
            ${WAYLAND_INCLUDE_DIRS}
            ${GENERATED_PROTO_DIR}
            $<$<BOOL:${ENABLE_HAVEL_LANG}>:${CMAKE_SOURCE_DIR}/src/havel-lang>
            $<$<BOOL:${ENABLE_LLVM}>:${LLVM_INCLUDE_DIRS}>
        )
        
        target_link_libraries(${test_name} PRIVATE 
            ${COMMON_LIBS}
            GTest::gtest 
            GTest::gtest_main 
            Qt6::Test
            $<$<BOOL:${ENABLE_LLVM}>:${LLVM_LIBS}>
        )
        
        target_compile_definitions(${test_name} PRIVATE
            RUN_TESTS
            $<$<NOT:$<BOOL:${ENABLE_HAVEL_LANG}>>:DISABLE_HAVEL_LANG>
            $<$<BOOL:${ENABLE_LLVM}>:HAVEL_ENABLE_LLVM>
        )
        
        # Add to CTest
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
    
    # Special GUI test if it exists
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/TestGUI.cpp")
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
message(STATUS "GUI (Qt6):       ✓ ENABLED (${Qt6_VERSION})")
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
    message(STATUS "Tests:           ✓ ENABLED")
elseif(ENABLE_TESTS)
    message(STATUS "Tests:           ⚠ REQUESTED BUT GTEST MISSING")
else()
    message(STATUS "Tests:           ✗ DISABLED")
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
    