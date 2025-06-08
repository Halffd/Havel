cmake_minimum_required(VERSION 3.16)
project(HvC VERSION 1.0.0 LANGUAGES C CXX)

# Options - declare these FIRST
option(USE_CLANG "Force use of Clang compiler" ON)
option(ENABLE_LLVM "Enable LLVM JIT compilation" OFF)
option(DISABLE_GUI "Disable GUI components" OFF)
option(DISABLE_HAVEL_LANG "Disable Havel language compilation" OFF)

# Compiler selection (must be before project() ideally, but whatever)
if(USE_CLANG AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "USE_CLANG set but not using Clang. Set CC/CXX env vars instead.")
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

# Find packages
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)

find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_search_module(LUA REQUIRED lua5.4 lua-5.4 lua54)
endif()
message(STATUS "LUA_LIBRARIES: ${LUA_LIBRARIES}")
message(STATUS "LUA_INCLUDE_DIR: ${LUA_INCLUDE_DIR}")

find_library(XTEST_LIB Xtst)
message(STATUS "XTEST_LIB: ${XTEST_LIB}")

# GUI dependencies (only if not disabled)
if(NOT DISABLE_GUI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
    find_package(GTest QUIET)
endif()

# LLVM (only if enabled and Havel lang enabled)
if(ENABLE_LLVM AND NOT DISABLE_HAVEL_LANG)
    find_package(LLVM REQUIRED CONFIG)
    llvm_map_components_to_libnames(LLVM_LIBS
        Core ExecutionEngine MCJIT OrcJIT Support Target
        X86CodeGen X86Desc X86Info Analysis TransformUtils
    )
    add_definitions(${LLVM_DEFINITIONS})
    add_definitions(-DHAVEL_ENABLE_LLVM)
elseif(ENABLE_LLVM AND DISABLE_HAVEL_LANG)
    message(WARNING "LLVM enabled but Havel language disabled - LLVM will be ignored")
    set(ENABLE_LLVM OFF)
endif()

# Source files - conditional compilation
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp" "src/window/*.cpp" "src/utils/*.cpp" "src/media/*.cpp")

if(NOT DISABLE_GUI)
    file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
    list(APPEND CORE_SOURCES ${GUI_SOURCES})
endif()

if(NOT DISABLE_HAVEL_LANG)
    file(GLOB_RECURSE HAVEL_LANG_SOURCES "src/havel-lang/*.cpp")
endif()

file(GLOB_RECURSE TEST_SOURCES "src/tests/*.cpp")

# Common libraries
set(COMMON_LIBS
    ${X11_LIBRARIES}
    Threads::Threads
    dl
    ${DBUS_LIBRARIES}
    ${XTEST_LIB}
    Xrandr
    pthread
    m
)

list(APPEND COMMON_LIBS ${LUA_LIBRARIES})

# Add GUI libs if not disabled
if(NOT DISABLE_GUI)
    list(APPEND COMMON_LIBS ${CAIRO_LIBRARIES} ${GTKMM_LIBRARIES})
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${X11_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIRS}
)

if(NOT DISABLE_HAVEL_LANG)
    include_directories(${LUA_INCLUDE_DIR})
endif()

if(NOT DISABLE_GUI)
    include_directories(${CAIRO_INCLUDE_DIRS} ${GTKMM_INCLUDE_DIRS})
endif()

if(ENABLE_LLVM)
    include_directories(${LLVM_INCLUDE_DIRS})
endif()

# Main executable
add_executable(hvc src/main.cpp ${CORE_SOURCES})
target_link_libraries(hvc ${COMMON_LIBS})

# Compile definitions for main executable
if(DISABLE_GUI)
    target_compile_definitions(hvc PRIVATE DISABLE_GUI)
endif()

if(DISABLE_HAVEL_LANG)
    target_compile_definitions(hvc PRIVATE DISABLE_HAVEL_LANG)
endif()

# Test executable (only if Havel lang enabled)
if(NOT DISABLE_HAVEL_LANG)
    add_executable(test_havel
        src/havel-lang/tests/test_havel.cpp
        ${HAVEL_LANG_SOURCES}
        ${CORE_SOURCES}
    )
    target_link_libraries(test_havel ${COMMON_LIBS})
    target_compile_definitions(test_havel PRIVATE DISABLE_GUI)
    
    # Havel Language Library
    add_library(havel_lang STATIC ${HAVEL_LANG_SOURCES})
    target_link_libraries(havel_lang ${COMMON_LIBS})
    
    if(ENABLE_LLVM)
        target_link_libraries(test_havel ${LLVM_LIBS})
        target_link_libraries(havel_lang ${LLVM_LIBS})
        target_compile_definitions(havel_lang PRIVATE HAVEL_ENABLE_LLVM)
    endif()
endif()

# LLVM linking for main executable
if(ENABLE_LLVM)
    target_link_libraries(hvc ${LLVM_LIBS})
endif()

# Hotkey test (if test sources exist)
if(TEST_SOURCES)
    # Create a separate test executable for each test file
    foreach(test_file ${TEST_SOURCES})
        get_filename_component(test_name ${test_file} NAME_WE)
        add_executable(${test_name} ${test_file} ${CORE_SOURCES})
        target_link_libraries(${test_name} ${COMMON_LIBS})
        target_compile_definitions(${test_name} PRIVATE RUN_TESTS)
        
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror)
        endif()
        
        # Add a test target for each test executable
        add_test(NAME ${test_name}
                 COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${test_name}
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endforeach()
    
    if(DISABLE_GUI)
        target_compile_definitions(hotkey_test PRIVATE DISABLE_GUI)
    endif()
    
    if(DISABLE_HAVEL_LANG)
        target_compile_definitions(hotkey_test PRIVATE DISABLE_HAVEL_LANG)
    endif()
endif()

# GUI targets (only if GUI enabled)
if(NOT DISABLE_GUI AND GTest_FOUND)
    add_executable(havel_gui_test tests/TestGUI.cpp)
    target_link_libraries(havel_gui_test ${GTKMM_LIBRARIES} GTest::GTest GTest::Main)
    
    add_executable(havel_gui_example examples/simple_gui_example.cpp)
    target_link_libraries(havel_gui_example ${GTKMM_LIBRARIES} ${X11_LIBRARIES})
endif()

# Install
set(INSTALL_TARGETS hvc)
if(NOT DISABLE_HAVEL_LANG)
    list(APPEND INSTALL_TARGETS test_havel)
endif()

install(TARGETS ${INSTALL_TARGETS} RUNTIME DESTINATION bin)

# Status messages
message(STATUS "Build configuration:")
if(DISABLE_GUI)
    message(STATUS "  GUI: DISABLED")
else()
    message(STATUS "  GUI: ENABLED")
endif()

if(DISABLE_HAVEL_LANG)
    message(STATUS "  Havel Language: DISABLED")
else()
    message(STATUS "  Havel Language: ENABLED")
endif()

if(ENABLE_LLVM)
    message(STATUS "  LLVM: ENABLED")
else()
    message(STATUS "  LLVM: DISABLED")
endif()