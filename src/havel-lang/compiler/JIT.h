// src/havel-lang/compiler/JIT.hpp
#pragma once
#include "Compiler.h"
#include "../ast/AST.h"
#include <unordered_map>

namespace havel::compiler {

    class JIT {
    private:
        Compiler compiler;
        std::unordered_map<std::string, Compiler::HotkeyActionFunc> compiledHotkeys;

    public:
        JIT();

        // Compile and register hotkey with NATIVE SPEED! ⚡
        void CompileHotkey(const std::string& combination, const ast::Expression& action);

        // Execute compiled hotkey (pure machine code!)
        void ExecuteHotkey(const std::string& combination);

        // Compile entire Havel script
        void CompileScript(const ast::Program& program);
    };

} // namespace havel::compiler