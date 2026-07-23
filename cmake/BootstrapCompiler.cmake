# Bootstrap compiler - frozen minimal compiler for self-hosted modules
# Compiles modules/lang/*.hv to .hvc bytecode
# This is a Stage 0 compiler - no new features should be added here
# Sources: bootstrap main + bootstrap lexer/parser + havel-lang ByteCompiler

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Use the same compiler flags as main build
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto=auto -DNDEBUG -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden -fabi-version=14 -fno-gnu-unique")
endif()

# Bootstrap compiler sources - frozen, minimal subset
# src/bootstrap/parser/Parser.cpp has the same code path as havel-lang parser
# because they share the same AST/Bytecode compilation through havel-lang includes.
# The bootstrap parser was kept separate so bootstrap releases don't depend on
# the main C++ parser source layout. See line 1899 lookahead>0 fix for [::x].
set(BOOTSTRAP_SOURCES
    src/bootstrap/main.cpp
    src/bootstrap/lexer/Lexer.cpp
    src/bootstrap/parser/Parser.cpp
    src/havel-lang/ast/AST.cpp
    src/havel-lang/compiler/core/ByteCompiler.cpp
    src/havel-lang/compiler/core/BytecodeIR.hpp
    src/havel-lang/compiler/runtime/RuntimeSupport.cpp
    src/havel-lang/compiler/semantic/LexicalResolver.cpp
    src/havel-lang/compiler/semantic/TypeChecker.cpp
    src/havel-lang/compiler/semantic/ModuleResolver.cpp
    src/havel-lang/errors/ErrorSystem.h
    src/havel-lang/utils/ErrorPrinter.cpp
    src/utils/Timer.cpp
    src/c/LoggerC.c
    src/c/Config.c
)

add_executable(havel-bootstrap ${BOOTSTRAP_SOURCES})
set_target_properties(havel-bootstrap PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)

# Use large code model to avoid R_X86_64_PC32 relocation overflow
target_compile_options(havel-bootstrap PRIVATE -mcmodel=large)
target_link_options(havel-bootstrap PRIVATE -mcmodel=large)

target_include_directories(havel-bootstrap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/havel-lang
    ${CMAKE_SOURCE_DIR}/src/bootstrap
)

target_link_libraries(havel-bootstrap PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads
    fmt::fmt
)

# Add bootstrap as dependency of havel (havel needs .hvc files)
add_dependencies(havel havel-bootstrap)

# Function to add bootstrap compilation target
function(add_bootstrap_module module_name)
    set(hv_src "${CMAKE_SOURCE_DIR}/modules/lang/${module_name}.hv")
    set(hvc_out "${CMAKE_BINARY_DIR}/out/modules/lang/${module_name}.hvc")
    set(hv_reldir "modules/lang")
    set(target_name "build_${module_name}_hvc")

    add_custom_command(
        OUTPUT ${hvc_out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/out/modules/lang
        COMMAND havel-bootstrap ${hv_src} ${hvc_out}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${hvc_out} ${CMAKE_SOURCE_DIR}/${hv_reldir}/${module_name}.hvc
        DEPENDS havel-bootstrap ${hv_src}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Compiling ${module_name}.hv via bootstrap compiler and syncing to source tree"
        VERBATIM
    )

    # Custom target that depends on the file output
    add_custom_target(${target_name} DEPENDS ${hvc_out})
    add_dependencies(havel ${target_name})
endfunction()

# Compile the core self-hosted compiler modules
add_bootstrap_module(lexer)
add_bootstrap_module(pratt)
add_bootstrap_module(emitter)
add_bootstrap_module(loader)
add_bootstrap_module(scope)
add_bootstrap_module(ast)
add_bootstrap_module(types)
add_bootstrap_module(debug)
add_bootstrap_module(error)
add_bootstrap_module(fmt)
