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
#include "havel-lang/core/PipelineOptions.hpp"

namespace havel::compiler {

// Only ever used as a pointer in SourceExecuteOptions; keeping the forward
// declaration here means the runtime-facing header does not have to pull in
// the whole VM.
class VM;

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
  Ok,          // chunk ready, or compile-and-execute finished
  LexError,    // lexer threw
  ParseError,  // parser threw
  ParseFailed, // parser returned errors / null program
  CompileError, // ByteCompiler threw
  ExecuteFailed, // compile-and-execute threw (compile or run, no distinction)
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

// Options for the compile-and-execute path. Deliberately much smaller than
// the SDK PipelineOptions: the runtime side must not be able to reach the
// whole pipeline, only the knobs a caller genuinely owns.
struct SourceExecuteOptions {
  std::string compile_unit_name = "unit";
  // Execute in this VM instead of a fresh one. havel_loadstring needs it:
  // the C API keeps the returned state's VM and merges its globals after
  // the call, so the script has to run in that exact VM.
  VM *vm_override = nullptr;
};

struct SourceExecuteResult {
  SourceCompileStatus status = SourceCompileStatus::NoCompiler;
  std::string error; // raw diagnostic; call site adds its own prefix
  Value return_value = nullptr;
};

class ModuleCompilerHook {
public:
  virtual ~ModuleCompilerHook() = default;

  virtual bool canCompile() const = 0;
  // options is honoured by FullPipeline mode only, and is the caller's own
  // PipelineOptions when a caller has one (HavelEngine builds a fully
  // populated record: unit name, strict semantics, host functions, debug
  // flags, instruction limit). Module mode has no pipeline to configure, so
  // it ignores it. Passing null means "use the hook's defaults".
  virtual SourceCompileResult compileSource(const std::string &source,
                                            SourceCompileMode mode =
                                                SourceCompileMode::Module,
                                            const PipelineOptions *options =
                                                nullptr) = 0;

  // Compile and run in one step. Separate from compileSource because the
  // caller wants the side effects of execution, not a chunk it then has to
  // drive itself - and because it may need to run in a VM it owns.
  // Reports NoCompiler in a runtime without an SDK, like compileSource.
  virtual SourceExecuteResult
  compileAndExecute(const std::string &source, const std::string &entry_function,
                    const SourceExecuteOptions &options) = 0;

  // Process-wide hook. Never null: when nothing is registered an internal
  // null-object is returned (canCompile() == false, NoCompiler).
  static ModuleCompilerHook &instance();
  // Last registration wins.
  static void registerHook(std::unique_ptr<ModuleCompilerHook> hook);
};

} // namespace havel::compiler
