#pragma once

#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/common/Export.hpp"

namespace havel::compiler::prototypes {

// Register prototype methods for each built-in type.
// These functions live in havel::compiler::prototypes because they need
// direct access to VM internals (heap_, current_chunk, toBool, etc.).
// They are NOT part of the public stdlib API.
//
// HAVEL_EXPORT: stdlib module plugins call these and resolve them from the
// main executable at dlopen time; release builds use -fvisibility=hidden, so
// without the explicit attribute the plugin fails to load on undefined symbols.
HAVEL_EXPORT void registerStringPrototype(VM& vm);
void registerArrayPrototype(VM& vm);
void registerNumberPrototype(VM& vm);
void registerBoolPrototype(VM& vm);
void registerObjectPrototype(VM& vm);
void registerSetPrototype(VM& vm);
void registerRangePrototype(VM& vm);

} // namespace havel::compiler::prototypes
