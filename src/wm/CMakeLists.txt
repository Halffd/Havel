cmake_minimum_required(VERSION 3.16)
project(wayland-compositor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find Wayland and wlroots
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WLROOTS REQUIRED wlroots-0.19)
pkg_check_modules(XKB REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)

# Get wayland-protocols datadir for protocol files
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Generate protocol headers
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_HEADERS_DIR ${CMAKE_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PROTOCOL_HEADERS_DIR})

# Generate xdg-shell protocol
execute_process(
    COMMAND ${WAYLAND_SCANNER} server-header 
    ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.h
)

execute_process(
    COMMAND ${WAYLAND_SCANNER} private-code
    ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
)

# Add the generated protocol source
set(PROTOCOL_SOURCES
    ${PROTOCOL_HEADERS_DIR}/xdg-shell-protocol.c
)

# Add executable
add_executable(${PROJECT_NAME} 
    compositor.cpp
    ${PROTOCOL_SOURCES}
)

# Set compile definitions for unstable API
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WLR_USE_UNSTABLE
)

# Set include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROTOCOL_HEADERS_DIR}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${WLROOTS_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${WAYLAND_SERVER_LIBRARIES}
    ${WLROOTS_LIBRARIES}
    ${XKB_LIBRARIES}
    ${PIXMAN_LIBRARIES}
)

# Set library directories
target_link_directories(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${WLROOTS_LIBRARY_DIRS}
    ${XKB_LIBRARY_DIRS}
    ${PIXMAN_LIBRARY_DIRS}
)

# Compiler flags - Use C++17 to handle C99 array syntax
target_compile_options(${PROJECT_NAME} PRIVATE
    ${WAYLAND_SERVER_CFLAGS_OTHER}
    ${WLROOTS_CFLAGS_OTHER}
    ${XKB_CFLAGS_OTHER}
    ${PIXMAN_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-c99-extensions
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
