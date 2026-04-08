#pragma once

#include "havel-lang/compiler/vm/VM.hpp"

namespace havel::stdlib {

void registerStringPrototype(compiler::VM* vm);
void registerListPrototype(compiler::VM* vm);
void registerNumberPrototype(compiler::VM* vm);
void registerBoolPrototype(compiler::VM* vm);
void registerObjectPrototype(compiler::VM* vm);

} // namespace havel::stdlib
