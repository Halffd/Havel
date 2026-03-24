/*
 * ArrayModule.hpp - Array stdlib for VM (no host/service)
 * Pure VM implementation using BytecodeValue
 */
#pragma once
#include "../compiler/bytecode/VMApi.hpp"

#include <algorithm>
#include <cmath>

namespace havel {
class Environment;
class Interpreter;
} // namespace havel

namespace havel::stdlib {

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerArrayModule(Environment &env, Interpreter *interpreter);

// NEW: Register array module with VMApi (stable API layer)
void registerArrayModule(havel::compiler::VMApi &api);

// Implementation in ArrayModule.cpp

// Implementation of old registerArrayModule (placeholder)
inline void registerArrayModule(Environment &env, Interpreter *interpreter) {
  (void)env;
  (void)interpreter;
}

} // namespace havel::stdlib
