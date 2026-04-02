/*
 * RegexModule.hpp - Regular expression stdlib for VM (VMApi)
 * Pure VM implementation using VMApi
 */
#pragma once
#include "havel-lang/compiler/vm/VMApi.hpp"

#include <regex>

namespace havel {
class Environment;
}

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerRegexModule(Environment &env);

// NEW: Register regex module with VMApi (stable API layer)
void registerRegexModule(compiler::VMApi &api);

// Implementation of old registerRegexModule (placeholder)
inline void registerRegexModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
