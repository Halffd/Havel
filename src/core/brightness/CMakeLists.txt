cmake_minimum_required(VERSION 3.20)

project(havel_gamma_ramp LANGUAGES C)

# Build shared library
add_library(gamma_ramp SHARED gamma_ramp.c)

target_include_directories(gamma_ramp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gamma_ramp PRIVATE -O3 -fPIC -Wall -Wextra -std=c11 -lm)

# Install to build directory
set_target_properties(gamma_ramp PROPERTIES
    OUTPUT_NAME "gamma_ramp"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

install(TARGETS gamma_ramp LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})