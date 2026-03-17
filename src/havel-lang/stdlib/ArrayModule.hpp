/*
 * ArrayModule.hpp
 *
 * Array manipulation functions for Havel standard library.
 */
#pragma once

#include "../runtime/Environment.hpp"

namespace havel {

class Interpreter;

namespace stdlib {

// Module registration function (auto-registered via STD_MODULE_DESC macro)
void registerArrayModule(Environment &env, Interpreter *interpreter = nullptr);

} // namespace stdlib
} // namespace havel
