#pragma once

#include "havel-lang/compiler/vm/VM.hpp"

namespace havel::compiler::prototypes {

// Register prototype methods for each built-in type.
// These functions live in havel::compiler::prototypes because they need
// direct access to VM internals (heap_, current_chunk, toBool, etc.).
// They are NOT part of the public stdlib API.

void registerStringPrototype(VM& vm);
void registerArrayPrototype(VM& vm);
void registerNumberPrototype(VM& vm);
void registerBoolPrototype(VM& vm);
void registerObjectPrototype(VM& vm);

} // namespace havel::compiler::prototypes
