/*
 * StandardLibraryModule.cpp
 * 
 * Standard library module for Havel language.
 */
#include "StandardLibraryModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::modules {

void registerStandardLibraryModule(Environment& env, HostContext&) {
    // This module contains core standard library functions
    // that don't require host context
    
    // Note: Most standard library functions have been extracted to:
    // - stdlib/MathModule.cpp
    // - stdlib/StringModule.cpp
    // - stdlib/ArrayModule.cpp
    // - stdlib/TypeModule.cpp
    // - stdlib/RegexModule.cpp
    // - stdlib/FileModule.cpp
    // - stdlib/ProcessModule.cpp
    // - stdlib/PhysicsModule.cpp
    //
    // This module is a placeholder for any remaining core functions
    // that should be part of the standard library.
}

} // namespace havel::modules
