/*
 * RuntimeModule.cpp
 *
 * Runtime utilities module for Havel language.
 * Provides app control, debug utilities, and runOnce functionality.
 */
#include "RuntimeModule.hpp"
#include "process/Launcher.hpp"
#include "stdlib/TypeModule.hpp"

namespace havel::modules {

void registerRuntimeModule(Environment &env, Interpreter *interpreter) {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
