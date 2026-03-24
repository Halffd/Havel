/*
 * WindowModule.hpp
 *
 * Window module for Havel VM
 * Provides window operations using the new VMApi system.
 */
#pragma once

#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {

/**
 * Register window module functions with VApi
 */
void registerWindowModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
