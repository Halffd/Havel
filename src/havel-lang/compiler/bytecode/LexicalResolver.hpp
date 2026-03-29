#pragma once

#include "BytecodeIR.hpp"
#include "../../ast/AST.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace havel::compiler {

enum class ResolvedBindingKind {
  Local,
  Upvalue,
  GlobalFunction,
  HostGlobal,
  Builtin
};

struct ResolvedBinding {
  ResolvedBindingKind kind = ResolvedBindingKind::Local;
  uint32_t slot = 0;
  uint32_t scope_distance = 0;
  std::string name;
  bool is_const = false;
};

struct LexicalResolutionResult {
  std::unordered_map<const ast::Identifier *, ResolvedBinding> identifier_bindings;
  std::unordered_map<const ast::Identifier *, uint32_t> declaration_slots;
  std::unordered_map<const ast::FunctionDeclaration *, std::vector<UpvalueDescriptor>>
      function_upvalues;
  std::unordered_map<const ast::LambdaExpression *, std::vector<UpvalueDescriptor>>
      lambda_upvalues;
  std::unordered_set<std::string> global_variables;  // Top-level let declarations
};

class LexicalResolver {
public:
  LexicalResolver() = default;
  explicit LexicalResolver(std::unordered_set<std::string> builtins,
                           std::unordered_set<std::string> host_globals = {})
      : builtins_(std::move(builtins)), host_globals_(std::move(host_globals)) {}

  LexicalResolutionResult resolve(const ast::Program &program);
  const std::vector<std::string> &errors() const { return errors_; }

private:
  struct FunctionContext {
    struct LocalSymbol {
      uint32_t slot = 0;
      bool is_const = false;
    };
    const ast::ASTNode *owner = nullptr;
    std::vector<std::unordered_map<std::string, LocalSymbol>> scopes;
    std::unordered_map<std::string, uint32_t> upvalue_slots;
    std::vector<UpvalueDescriptor> upvalues;
    uint32_t next_slot = 0;
  };

  LexicalResolutionResult result_;
  std::vector<std::string> errors_;
  std::unordered_set<std::string> top_level_functions_;
  std::unordered_set<std::string> top_level_structs_;
  std::unordered_set<std::string> global_variables_;  // Top-level let declarations
  std::vector<FunctionContext> function_stack_;
  std::unordered_set<std::string> builtins_{
      "print",      "sleep_ms",        "clock_ms",
      "system.gc",  "system.gcStats",  "system_gc",
      "system_gcStats",
      // Pipeline functions (standalone aliases for string methods)
      "upper", "lower", "trim", "replace",
      // Process aliases
      "run", "runDetached",
      // Media aliases
      "play",
      // Mouse module functions
      "mouse.click", "mouse.down", "mouse.up", "mouse.move", "mouse.moveRel",
      "mouse.scroll", "mouse.pos", "mouse.setSpeed", "mouse.setAccel", "mouse.setDPI",
      // Mouse global aliases
      "click", "move", "moveRel", "scroll",
      // System detection
      "system.detect", "system.hardware",
      // Display functions
      "display.getMonitors", "display.getPrimary", "display.getCount", "display.getMonitorsArea",
      // Extension modules (loaded via extension.load())
      "image", "ocr", "pixel", "join", "gui", "audio",
      // MPV media player control
      "mpv",
      // Extension module methods (common patterns)
      "image.load", "ocr.read", "pixel.region", "pixel.get", "pixel.match",
      "audio.increaseActiveAppVolume", "audio.decreaseActiveAppVolume", "audio.getActiveAppVolume",
      // MPV methods
      "mpv.volumeUp", "mpv.volumeDown", "mpv.toggleMute", "mpv.stop", "mpv.next", "mpv.previous",
      "mpv.seek", "mpv.subSeek", "mpv.addSpeed", "mpv.addSubScale", "mpv.addSubDelay",
      "mpv.cycle", "mpv.copySubtitle", "mpv.ipcSet", "mpv.ipcReset", "mpv.screenshot", "mpv.cmd",
      // Utility functions
      "tap",
      // Array functions
      "flat", "smooth", "squeeze", "flatMap"};
  std::unordered_set<std::string> host_globals_{
      "print", "sleep", "sleep_ms", "clock_ms", "time.now", "fmt",
      "window", "mouse", "io", "system", "hotkey", "mode", "process", "async",
      "thread", "interval", "timeout", "struct", "extension", "image", "ocr", "pixel", "gui", "audio", "mpv"};

  void collectTopLevelFunctions(const ast::Program &program);
  void collectTopLevelStructs(const ast::Program &program);

  void beginFunction(const ast::ASTNode *function);
  void endFunction();
  void beginScope();
  void endScope();
  uint32_t declareLocal(const std::string &name,
                        const ast::Identifier *declaration = nullptr,
                        bool is_const = false);

  void resolveStatement(const ast::Statement &statement);
  void resolveExpression(const ast::Expression &expression);
  void resolveFunctionDeclaration(const ast::FunctionDeclaration &function);
  void resolveLambdaExpression(const ast::LambdaExpression &lambda);
  void collectPatternIdentifiers(const ast::Expression &pattern);

  std::optional<ResolvedBinding> resolveIdentifier(const std::string &name);
  std::optional<ResolvedBinding> resolveIdentifierInFunction(
      const std::string &name, size_t function_index);
  uint32_t addUpvalue(size_t function_index, const std::string &name,
                      uint32_t source_index, bool captures_local);
  void noteIdentifierBinding(const ast::Identifier &identifier,
                             const ResolvedBinding &binding);
};

} // namespace havel::compiler
