cmake_minimum_required(VERSION 3.16)
project(Havel VERSION 1.0.0 LANGUAGES C CXX)

# Options - declare these FIRST
option(USE_CLANG "Force use of Clang compiler" ON)
option(ENABLE_LLVM "Enable LLVM JIT compilation" OFF)
option(ENABLE_TESTS "Enable building tests" ON)
option(DISABLE_HAVEL_LANG "Disable Havel language compilation" OFF)

# Compiler selection warning
if(USE_CLANG AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "USE_CLANG set but not using Clang. Set CC=clang CXX=clang++ before cmake.")
endif()

# Standards
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto=thin -DNDEBUG -ffast-math")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto=thin -fuse-ld=lld")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG -ffast-math")
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)

# Lua
find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    pkg_search_module(LUA REQUIRED lua5.4 lua-5.4 lua54)
endif()
message(STATUS "LUA_LIBRARIES: ${LUA_LIBRARIES}")
message(STATUS "LUA_INCLUDE_DIR: ${LUA_INCLUDE_DIR}")

# X11 libraries
find_library(XTEST_LIB Xtst REQUIRED)
find_library(XRANDR_LIB Xrandr REQUIRED)
message(STATUS "XTEST_LIB: ${XTEST_LIB}")

# Qt6 is now REQUIRED (GUI always enabled)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts)
message(STATUS "Qt6 found: ${Qt6_FOUND}")

# GTest (only if tests enabled)
if(ENABLE_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        message(STATUS "GTest found: ${GTest_FOUND}")
    else()
        message(STATUS "GTest not found - some tests will be skipped")
    endif()
endif()

# LLVM (only if enabled and Havel lang enabled)
if(ENABLE_LLVM AND NOT DISABLE_HAVEL_LANG)
    find_package(LLVM REQUIRED CONFIG)
    
    execute_process(
        COMMAND llvm-config --libs --system-libs --ldflags
        OUTPUT_VARIABLE LLVM_LDFLAGS_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE LLVM_CONFIG_RESULT
    )
    
    if(NOT LLVM_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "llvm-config failed")
    endif()

    # Clean up LLVM flags
    string(REGEX REPLACE "[\r\n\t ]+" " " LLVM_LDFLAGS "${LLVM_LDFLAGS_RAW}")
    string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS)
    separate_arguments(LLVM_LIBS UNIX_COMMAND "${LLVM_LDFLAGS}")
    
    message(STATUS "LLVM_LIBS: ${LLVM_LIBS}")

    add_definitions(${LLVM_DEFINITIONS})
    add_definitions(-DHAVEL_ENABLE_LLVM)
elseif(ENABLE_LLVM AND DISABLE_HAVEL_LANG)
    message(WARNING "LLVM enabled but Havel language disabled - LLVM will be ignored")
    set(ENABLE_LLVM OFF)
endif()

# Source files
file(GLOB_RECURSE CORE_SOURCES 
    "src/core/*.cpp" 
    "src/window/*.cpp" 
    "src/utils/*.cpp" 
    "src/media/*.cpp" 
    "src/process/*.cpp" 
    "src/fs/*.cpp"
    "src/core/util/*.cpp"
)

# GUI sources (always included now)
file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
# Remove standalone main.cpp files
list(FILTER GUI_SOURCES EXCLUDE REGEX "main\\.cpp$")

# Havel language sources
set(HAVEL_LANG_SOURCES "")
if(NOT DISABLE_HAVEL_LANG)
    file(GLOB_RECURSE HAVEL_LANG_SOURCES "src/havel-lang/*.cpp")
endif()

# Test sources (only if tests enabled)
set(TEST_SOURCES "")
if(ENABLE_TESTS)
    file(GLOB_RECURSE TEST_SOURCES "src/tests/*.cpp")
endif()

# Common libraries (Qt6 always included)
set(COMMON_LIBS
    ${X11_LIBRARIES}
    ${XTEST_LIB}
    ${XRANDR_LIB}
    Threads::Threads
    ${DBUS_LIBRARIES}
    ${LUA_LIBRARIES}
    Qt6::Core
    Qt6::Widgets
    Qt6::Charts
    dl
    crypto
    zip
    ssl
    pthread
    m
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
)

if(NOT DISABLE_HAVEL_LANG)
    include_directories(${CMAKE_SOURCE_DIR}/src/havel-lang)
endif()

if(ENABLE_LLVM)
    include_directories(${LLVM_INCLUDE_DIRS})
endif()

# Main executable (renamed from hvc to havel)
add_executable(havel 
    src/main.cpp 
    ${CORE_SOURCES}
    ${GUI_SOURCES}
    ${HAVEL_LANG_SOURCES}
)

target_link_libraries(havel ${COMMON_LIBS})

# Compile definitions for main executable
if(NOT DISABLE_GUI)
    # Set MOC options to handle X11 conflicts
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
endif()
if(DISABLE_HAVEL_LANG)
    target_compile_definitions(havel PRIVATE DISABLE_HAVEL_LANG)
endif()

if(ENABLE_LLVM)
    target_link_libraries(havel ${LLVM_LIBS})
endif()


# Havel Language Library (if enabled)
if(NOT DISABLE_HAVEL_LANG)
    add_library(havel_lang STATIC ${HAVEL_LANG_SOURCES})
    target_link_libraries(havel_lang ${COMMON_LIBS})
    
    if(ENABLE_LLVM)
        target_link_libraries(havel_lang ${LLVM_LIBS})
        target_compile_definitions(havel_lang PRIVATE HAVEL_ENABLE_LLVM)
    endif()
    
    # Test executable for Havel language
    if(ENABLE_TESTS)
        add_executable(test_havel
            src/havel-lang/tests/test_havel.cpp
            ${CORE_SOURCES}
        )
        target_link_libraries(test_havel havel_lang ${COMMON_LIBS})
        
        if(ENABLE_LLVM)
            target_link_libraries(test_havel ${LLVM_LIBS})
        endif()
        
        add_test(NAME test_havel COMMAND test_havel)
    endif()
endif()

# General tests (if enabled)
if(ENABLE_TESTS AND TEST_SOURCES)
    foreach(test_file ${TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} 
            ${test_file} 
            ${CORE_SOURCES}
            ${GUI_SOURCES}
        )
        target_link_libraries(${test_name} ${COMMON_LIBS})
        target_compile_definitions(${test_name} PRIVATE RUN_TESTS)
        
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${test_name} PRIVATE -Wall -Wextra)
        endif()
        
        if(DISABLE_HAVEL_LANG)
            target_compile_definitions(${test_name} PRIVATE DISABLE_HAVEL_LANG)
        endif()
        
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# GUI tests (if GTest found and tests enabled)
if(ENABLE_TESTS AND GTest_FOUND)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/TestGUI.cpp")
        add_executable(havel_gui_test tests/TestGUI.cpp)
        target_link_libraries(havel_gui_test 
            Qt6::Widgets 
            GTest::GTest 
            GTest::Main
        )
        add_test(NAME havel_gui_test COMMAND havel_gui_test)
    endif()
endif()

# GUI example
if(EXISTS "${CMAKE_SOURCE_DIR}/examples/simple_gui_example.cpp")
    add_executable(havel_gui_example examples/simple_gui_example.cpp)
    target_link_libraries(havel_gui_example Qt6::Widgets ${X11_LIBRARIES})
endif()

# Install targets
set(INSTALL_TARGETS havel)

if(NOT DISABLE_HAVEL_LANG)
    list(APPEND INSTALL_TARGETS havel_lang)
    if(ENABLE_TESTS)
        list(APPEND INSTALL_TARGETS test_havel)
    endif()
endif()

if(TARGET havel_gui)
    list(APPEND INSTALL_TARGETS havel_gui)
endif()

install(TARGETS ${INSTALL_TARGETS} 
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Status messages
message(STATUS "=== Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Main executable: havel (GUI always enabled)")

if(DISABLE_HAVEL_LANG)
    message(STATUS "Havel Language: DISABLED")
else()
    message(STATUS "Havel Language: ENABLED")
endif()

if(ENABLE_LLVM)
    message(STATUS "LLVM: ENABLED")
else()
    message(STATUS "LLVM: DISABLED")
endif()

if(ENABLE_TESTS)
    message(STATUS "Tests: ENABLED")
else()
    message(STATUS "Tests: DISABLED")
endif()

message(STATUS "Qt6 version: ${Qt6_VERSION}")