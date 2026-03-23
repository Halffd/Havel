/*
 * MapManagerModule.cpp
 *
 * Map Manager module for Havel language.
 * Host binding - connects language to MapManager.
 */
#include "MapManagerModule.hpp"
#include "core/IO.hpp"
#include "core/io/Device.hpp"
#include "core/io/MapManager.hpp"
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

namespace havel::modules {

// Static instance - matches the pattern in Interpreter.cpp
static std::unique_ptr<MapManager> coreMapManager;

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
