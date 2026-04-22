#pragma once
// Backward compatibility: compiler::ModuleLoader -> havel::ModuleLoader
// All module loading is now consolidated in havel::ModuleLoader
#include "../../runtime/ModuleLoader.hpp"

namespace havel::compiler {
    // Use :: to reference global havel namespace
    using ModuleLoader = ::havel::ModuleLoader;
    // Old struct, no longer used, but keep for ABI compat
    struct LoadedModule {};
}