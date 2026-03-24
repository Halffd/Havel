/*
 * NewClipboardModule.hpp
 *
 * Clipboard module for Havel VM
 * Provides clipboard operations using the new VMApi system.
 */
#pragma once

#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {

/**
 * Register clipboard module functions with VMApi
 */
void registerNewClipboardModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
