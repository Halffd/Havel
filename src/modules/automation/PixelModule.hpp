/*
 * PixelModule.hpp
 *
 * Pixel and image automation module for Havel bytecode VM.
 * Exposes: pixel, findImage, waitImage, readText, etc.
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerPixelModule(const compiler::VMApi& api);

} // namespace havel::modules
