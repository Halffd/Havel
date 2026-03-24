/*
 * HotkeyModule.hpp
 *
 * Hotkey module for Havel VM
 * Provides hotkey operations using the new VApi system.
 */
#pragma once

#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {

/**
 * Register hotkey module functions with VApi
 */
void registerHotkeyModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
