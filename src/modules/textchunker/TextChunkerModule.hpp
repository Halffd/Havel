/*
 * TextChunkerModule.hpp
 * 
 * Text chunking/splitting module for Havel language.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerTextChunkerModule(compiler::VMApi &api);

} // namespace havel::modules
