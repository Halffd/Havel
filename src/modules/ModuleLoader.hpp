/*
 * ModuleLoader.hpp
 * 
 * Loads all host modules into the Havel environment.
 * This is the bridge between the pure language runtime and host system.
 */
#pragma once

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

} // namespace modules
} // namespace havel
