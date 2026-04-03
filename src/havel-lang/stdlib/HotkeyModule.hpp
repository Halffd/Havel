#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"
#include <string>

namespace havel {
class Environment;
}

namespace havel::stdlib {

class HotkeyModule {
public:
  static void install(havel::compiler::VMApi &api);

  // Factory function to create hotkey context objects
  static havel::compiler::Value
  createHotkeyContext(havel::compiler::VM *vm, const std::string &hotkeyId,
                      const std::string &alias, const std::string &key,
                      const std::string &condition, const std::string &info,
                      havel::compiler::CallbackId callback);
};

// OLD: Register with Environment (for AST interpreter) - DECLARATION ONLY
void registerHotkeyModule(Environment &env);

// NEW: Register hotkey module with VMApi (stable API layer)
void registerHotkeyModule(havel::compiler::VMApi &api);

// Implementation in HotkeyModule.cpp

// Implementation of old registerHotkeyModule (placeholder)
inline void registerHotkeyModule(Environment &env) { (void)env; }

} // namespace havel::stdlib
