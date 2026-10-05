#include "havel-lang/compiler/vm/ModuleCompilerHook.hpp"

namespace havel::compiler {
namespace {

// Degrade target when no SDK is linked: source compilation reports
// NoCompiler instead of crashing or silently succeeding.
class NullCompilerHook final : public ModuleCompilerHook {
public:
  bool canCompile() const override { return false; }

  SourceCompileResult compileSource(const std::string &,
                                   SourceCompileMode) override {
    SourceCompileResult result;
    result.status = SourceCompileStatus::NoCompiler;
    result.error = "no compiler available in this runtime";
    return result;
  }
};

std::unique_ptr<ModuleCompilerHook> &hookStorage() {
  static std::unique_ptr<ModuleCompilerHook> hook;
  return hook;
}

} // namespace

ModuleCompilerHook &ModuleCompilerHook::instance() {
  static NullCompilerHook nullHook;
  auto &stored = hookStorage();
  return stored ? *stored : nullHook;
}

void ModuleCompilerHook::registerHook(std::unique_ptr<ModuleCompilerHook> hook) {
  hookStorage() = std::move(hook);
}

} // namespace havel::compiler
