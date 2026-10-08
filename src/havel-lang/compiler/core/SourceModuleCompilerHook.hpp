#pragma once

namespace havel::compiler {

// Installs the parse + ByteCompiler pipeline as the process-wide
// ModuleCompilerHook. SDK hosts (havel, hvtest, hvdb, havel-dap, embed
// tests) call this at startup before running any script that imports
// modules or calls eval. Idempotent: returns false when a hook that can
// compile is already installed. Runtime-only builds never call this and
// never link the TU that defines it.
bool registerSourceModuleCompilerHook();

} // namespace havel::compiler
