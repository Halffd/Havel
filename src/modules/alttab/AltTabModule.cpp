/*
 * AltTabModule.cpp
 *
 * Alt-Tab window switcher module for Havel language.
 * Host binding - connects language to AltTabWindow.
 */
#include "AltTabModule.hpp"
#include "gui/AltTab.hpp"

namespace havel::modules {

// Static instance - matches the pattern in Interpreter.cpp
static std::unique_ptr<AltTabWindow> altTabWindow;

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
