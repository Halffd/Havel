#pragma once

// Seam between the Havel runtime (VM) and the compiler SDK.
//
// The VM must never include Parser/ByteCompiler directly: a runtime-only
// build (no SDK) has to link and run. Every dynamic compile the VM needs
// — module `use` loads, loadScript, eval, runInContext — goes through
// this process-wide hook. SDK hosts register SourceModuleCompilerHook at
// startup (registerSourceModuleCompilerHook); runtime-only hosts register
// a bytecode loader or nothing, in which case source imports fail with a
// clean NoCompiler diagnostic instead of a link error.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "havel-lang/compiler/core/BytecodeIR.hpp"

namespace havel::compiler {

// Plain-data declaration info the compiler SDK extracts from the AST.
// The VM must not see AST types, so protocols/traits/impls cross the
// seam as descriptors and the caller registers them.
struct ModuleProtocolDecl {
  std::string name; // protocol or trait name
  std::vector<std::string> methods;
};

struct ModuleImplDecl {
  std::string traitName;
  std::string typeName;
};

enum class SourceCompileStatus {
  Ok,          // chunk ready
  LexError,    // lexer threw
  ParseError,  // parser threw
  ParseFailed, // parser returned errors / null program
  CompileError, // ByteCompiler threw
  NullChunk,   // ByteCompiler returned null
  NoCompiler,  // no hook registered (runtime-only build without SDK)
};

// How much frontend work the request needs.
//
// Module    bare Parser + ByteCompiler. What every module load, loadScript,
//           runInContext and the module-level eval have always used.
// FullPipeline
//           the whole SDK pipeline (use-statement module loading, type
//           check, name resolution) behind compileToBytecodeChunk. Only the
//           eval host function ever needed it. Asking for it in a runtime
//           without an SDK is a NoCompiler error, exactly like Module.
enum class SourceCompileMode { Module, FullPipeline };

struct SourceCompileResult {
  SourceCompileStatus status = SourceCompileStatus::NoCompiler;
  std::string error; // raw diagnostic; call site adds its own prefix
  std::unique_ptr<BytecodeChunk> chunk;
  std::vector<ModuleProtocolDecl> protocols; // protocols + traits
  std::vector<ModuleImplDecl> impls;         // trait -> type impls
};

class ModuleCompilerHook {
public:
  virtual ~ModuleCompilerHook() = default;

  virtual bool canCompile() const = 0;
  virtual SourceCompileResult compileSource(const std::string &source,
                                            SourceCompileMode mode =
                                                SourceCompileMode::Module) = 0;

  // Process-wide hook. Never null: when nothing is registered an internal
  // null-object is returned (canCompile() == false, NoCompiler).
  static ModuleCompilerHook &instance();
  // Last registration wins.
  static void registerHook(std::unique_ptr<ModuleCompilerHook> hook);
};

} // namespace havel::compiler
