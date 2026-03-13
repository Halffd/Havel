/*
 * ArrayModule.hpp
 *
 * Array manipulation functions for Havel standard library.
 */
#pragma once

#include "../runtime/Environment.hpp"

namespace havel::stdlib {

// Module registration function (auto-registered via STD_MODULE_DESC macro)
void registerArrayModule(Environment& env);

} // namespace havel::stdlib
