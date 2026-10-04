#pragma once

/*
 * HavelAPI.hpp - C++ side of the extension C ABI (HavelCAPI.h).
 *
 * api_register_function (HavelCAPI implementations) stores extension
 * functions in a registry; the engine drains that registry here once the
 * VM exists so the functions land in the normal host-function table and
 * buildNamespaceGlobals() materializes their namespace objects (qt.*, ...).
 *
 * Before this drain existed the registry was a function-local static with
 * no reader — every C-ABI extension function ever registered was silently
 * discarded, which is how the qt.* and gtk.* namespaces died when those
 * extensions moved into the toolkit plugins.
 */

#include "havel-lang/compiler/vm/VM.hpp"

#include <string>
#include <unordered_map>

namespace havel::compiler {

/// Move out all extension functions registered via the C API since the
/// last call and record `vm` as the target used for value conversion
/// (string interning) inside extension wrappers.
///
/// @param vm VM that will execute the drained functions; may be null for
///        a pure presence check.
/// @return name -> host function map ("qt.init", ...) — may be empty.
std::unordered_map<std::string, BytecodeHostFunction>
takeRegisteredExtensionFunctions(VM *vm);

} // namespace havel::compiler
