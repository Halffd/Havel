/*
 * SystemModule.cpp
 *
 * System information module for Havel language.
 * Provides CPU, memory, OS, and temperature information.
 */
#include "SystemModule.hpp"
#include "core/system/CpuInfo.hpp"
#include "core/system/MemoryInfo.hpp"
#include "core/system/OSInfo.hpp"
#include "core/system/Temperature.hpp"

namespace havel::modules {

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
