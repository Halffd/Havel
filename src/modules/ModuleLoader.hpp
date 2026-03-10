/*
 * ModuleLoader.hpp
 *
 * Loads all host modules into the Havel environment.
 * This is the bridge between the pure language runtime and host system.
 * 
 * Uses map-based module registration for cleaner architecture.
 */
#pragma once

#include "havel-lang/runtime/HostModuleRegistry.hpp"

namespace havel {

class Environment;
class Interpreter;

namespace modules {

/**
 * Load all host modules into the environment
 * 
 * @param env The language environment to register functions in
 * @param interpreter The interpreter instance (provides access to managers)
 */
void loadHostModules(Environment& env, Interpreter* interpreter);

/**
 * Register all modules with the registry
 * Called once at startup
 */
void registerAllModules();

} // namespace modules
} // namespace havel
