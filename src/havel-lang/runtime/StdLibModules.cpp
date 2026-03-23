// StdLibModules.cpp - STUBBED (lambda mangling issue)
// Stdlib modules moved to separate compilation units to avoid GCC lambda mangling

#include "../compiler/bytecode/HostBridge.hpp"

namespace havel {

/**
 * Register stdlib modules with VM (VM-native, no Environment)
 * STUBBED - lambda mangling requires separate compilation units
 */
void registerStdLibWithVM(compiler::HostBridge&) {
    // STUBBED - stdlib modules need to be compiled as separate translation units
    // to avoid GCC lambda mangling conflicts
}

} // namespace havel
