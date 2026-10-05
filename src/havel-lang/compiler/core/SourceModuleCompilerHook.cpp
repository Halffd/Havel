#include "havel-lang/compiler/core/SourceModuleCompilerHook.hpp"

#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/ModuleCompilerHook.hpp"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"

namespace havel::compiler {
namespace {

// SDK-side hook: the compile pipeline the VM is not allowed to reach.
// Module mode reproduces exactly what the VM used to inline at every
// dynamic compile site (loadModule / loadScript / runInContext / module
// eval): a default Parser (DebugOptions{}), default-constructed
// ByteCompiler (strict mode off, no optimizer), and the same exception
// surfaces. FullPipeline mode is what the eval host function used:
// compileToBytecodeChunk, which additionally loads use-statement modules,
// type checks and resolves names.
class SourceModuleCompilerHook final : public ModuleCompilerHook {
public:
  bool canCompile() const override { return true; }

  SourceCompileResult compileSource(const std::string &source,
                                    SourceCompileMode mode) override {
    SourceCompileResult result;

    if (mode == SourceCompileMode::FullPipeline)
      return compileFullPipeline(source);

    parser::Parser parser;
    std::unique_ptr<ast::Program> program;
    try {
      program = parser.produceAST(source);
    } catch (const ::havel::LexError &e) {
      result.status = SourceCompileStatus::LexError;
      result.error = e.what();
      return result;
    } catch (const ::havel::parser::ParseError &e) {
      result.status = SourceCompileStatus::ParseError;
      result.error = e.what();
      return result;
    }
    if (!program || parser.hasErrors()) {
      result.status = SourceCompileStatus::ParseFailed;
      if (parser.hasErrors()) {
        for (const auto &err : parser.getErrors())
          result.error += err.message + "\n";
      }
      return result;
    }

    collectDeclarations(*program, result);

    ByteCompiler byteCompiler;
    try {
      result.chunk = byteCompiler.compile(*program);
    } catch (const std::exception &e) {
      result.status = SourceCompileStatus::CompileError;
      result.error = e.what();
      return result;
    }
    if (!result.chunk) {
      result.status = SourceCompileStatus::NullChunk;
      return result;
    }

    result.status = SourceCompileStatus::Ok;
    return result;
  }

private:
  // The eval host function's pipeline. The unit name is "<eval>" and the
  // chunk entry "__main__" to match what eval used to pass. max_instructions
  // is deliberately not forwarded: compileToBytecodeChunk never reads it
  // (only runPipeline calls vm->setMaxInstructions), and the VM enforces its
  // own limit in the dispatch loop, so passing a value from the runtime side
  // would be dead data anyway.
  static SourceCompileResult compileFullPipeline(const std::string &source) {
    SourceCompileResult result;

    PipelineOptions options;
    options.compile_unit_name = "<eval>";
    options.debugBytecode = false;

    try {
      result.chunk = compileToBytecodeChunk(source, "__main__", options);
    } catch (const std::exception &e) {
      result.status = SourceCompileStatus::CompileError;
      result.error = e.what();
      return result;
    }
    if (!result.chunk) {
      result.status = SourceCompileStatus::NullChunk;
      return result;
    }

    result.status = SourceCompileStatus::Ok;
    return result;
  }

  // Protocol/trait/impl extraction previously lived inline in
  // VM::loadScript's AST walk. Runtime code must not see AST types, so
  // the hook translates top-level declarations into plain descriptors
  // and the caller registers them on the VM.
  static void collectDeclarations(const ast::Program &program,
                                   SourceCompileResult &out) {
    for (const auto &stmt : program.body) {
      if (!stmt)
        continue;
      if (stmt->kind == ast::NodeType::ProtocolDeclaration) {
        const auto &decl =
            static_cast<const ast::ProtocolDeclaration &>(*stmt);
        if (!decl.name)
          continue;
        ModuleProtocolDecl protocol;
        protocol.name = decl.name->symbol;
        for (const auto &method : decl.methods) {
          if (method && method->name)
            protocol.methods.push_back(method->name->symbol);
        }
        out.protocols.push_back(std::move(protocol));
      } else if (stmt->kind == ast::NodeType::TraitDeclaration) {
        const auto &decl = static_cast<const ast::TraitDeclaration &>(*stmt);
        if (!decl.name)
          continue;
        ModuleProtocolDecl trait;
        trait.name = decl.name->symbol;
        for (const auto &method : decl.methods) {
          if (method && method->name)
            trait.methods.push_back(method->name->symbol);
        }
        out.protocols.push_back(std::move(trait));
      } else if (stmt->kind == ast::NodeType::ImplDeclaration) {
        const auto &decl = static_cast<const ast::ImplDeclaration &>(*stmt);
        std::string traitName = decl.traitName ? decl.traitName->symbol : "";
        std::string typeName = decl.typeName ? decl.typeName->symbol : "";
        if (!traitName.empty() && !typeName.empty())
          out.impls.push_back({std::move(traitName), std::move(typeName)});
      }
    }
  }
};

// Self-registration: fires in every binary that compiles this TU
// directly into the executable (the gtest loop compiles all lang
// sources into each test binary, so every one of them gets the hook
// with no anchor needed). Archive-linked binaries (havel, hvtest, hvdb,
// havel-dap, embed tests) pull this TU through an explicit call to
// registerSourceModuleCompilerHook() from their entrypoint, which makes
// this initializer run too. Idempotent either way.
const bool selfRegistered = registerSourceModuleCompilerHook();

} // namespace

bool registerSourceModuleCompilerHook() {
  if (ModuleCompilerHook::instance().canCompile())
    return false;
  ModuleCompilerHook::registerHook(
      std::make_unique<SourceModuleCompilerHook>());
  return true;
}

} // namespace havel::compiler
