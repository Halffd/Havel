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

#include <cstdint>
#include <string>
#include <unordered_map>

/* Opaque C-ABI value type (extensions/HavelValue.h). */
struct HavelValue;

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

/* ==========================================================================
 * Bidirectional value conversion: VM Value <-> C-ABI HavelValue
 * ==========================================================================
 *
 * Handles round-trip through a registry: a HAVEL_HANDLE becomes a VM host
 * object carrying the "__capi_handle" id field; converting such an object
 * back yields the ORIGINAL HavelValue (pointer identity). Ints, bools,
 * floats, nulls and strings convert by value; arrays and objects convert
 * recursively with a depth limit (cycles degrade to null instead of
 * hanging). Both directions need the VM for interning/allocation and
 * degrade to primitives-only when it is null.
 */

/// VM Value -> new HavelValue* (caller owns the returned reference).
/// Returns nullptr for the null VM + non-primitive combination.
HavelValue *valueToHavelValue(VM *vm, const Value &v, int depth = 0);

/// HavelValue -> VM Value. Borrows `hv` (does not consume a reference);
/// handle results are additionally registered (registry owns +1 ref).
Value havelValueToValue(VM *vm, HavelValue *hv, int depth = 0);

} // namespace havel::compiler
