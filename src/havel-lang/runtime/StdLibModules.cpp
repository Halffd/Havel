// StdLibModules.cpp - STUBBED
// Stdlib modules will be implemented properly later

#include "../compiler/bytecode/HostBridge.hpp"

namespace havel {

/**
 * Register stdlib modules with VM (VM-native, no Environment)
 * Currently stubbed - stdlib needs proper VM integration
 */
void registerStdLibWithVM(compiler::HostBridge&) {
    // STUBBED - stdlib modules need proper VM integration
    // Basic functions (print, sleep, type conversions) are registered by VM itself
}

} // namespace havel
