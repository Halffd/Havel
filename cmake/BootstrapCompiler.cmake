# Bootstrap compiler - frozen minimal compiler for self-hosted modules
# Compiles lexer.hv, pratt.hv, emitter.hv to .hvc bytecode
# This is a Stage 0 compiler - no new features should be added here

cmake_minimum_required(VERSION 3.16)
project(HavelBootstrap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Use the same compiler as main build
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto=auto -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden -fabi-version=14 -fno-gnu-unique")
endif()

# Bootstrap compiler target
add_executable(havel-bootstrap
    src/bootstrap/main.cpp
    src/bootstrap/lexer/Lexer.cpp
    src/bootstrap/parser/Parser.cpp
    src/bootstrap/ast/AST.cpp
    src/bootstrap/errors/ErrorSystem.cpp
    src/bootstrap/compiler/core/ByteCompiler.cpp
    src/bootstrap/compiler/core/CompilerUtils.cpp
    src/bootstrap/compiler/core/Pipeline.cpp
    src/bootstrap/compiler/runtime/RuntimeSupport.cpp
    src/bootstrap/utils/Logger.cpp
    src/bootstrap/utils/Util.cpp
    src/bootstrap/utils/Timer.cpp
)

target_include_directories(havel-bootstrap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/bootstrap
    ${CMAKE_SOURCE_DIR}/src/havel-lang
)

target_link_libraries(havel-bootstrap PRIVATE
    fmt::fmt
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Find self-hosted modules to build
set(SELF_HOSTED_MODULES
    modules/lang/lexer.hv
    modules/lang/pratt.hv
    modules/lang/emitter.hv
)

# Custom commands to build .hvc files via bootstrap compiler
foreach(module ${SELF_HOSTED_MODULES})
    get_filename_component(module_name ${module} NAME_WE)
    set(hvc_output "${CMAKE_BINARY_DIR}/${module}.hvc")
    
    add_custom_command(
        OUTPUT ${hvc_output}
        COMMAND havel-bootstrap ${CMAKE_SOURCE_DIR}/${module} ${hvc_output}
        DEPENDS havel-bootstrap ${CMAKE_SOURCE_DIR}/${module}
        COMMENT "Building ${module} -> ${module}.hvc"
        VERBATIM
    )
    
    list(APPEND HVC_OUTPUTS ${hvc_output})
endforeach()

# Custom target to build all self-hosted modules
add_custom_target(build-self-hosted-modules DEPENDS ${HVC_OUTPUTS})

# Make main havel target depend on bootstrap target if HAVEL_LANG enabled
if(ENABLE_HAVEL_LANG)
    add_dependencies(havel build-self-hosted-modules)
endif()