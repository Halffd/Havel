#include "JIT.h"


// JIT Hotkey Manager - ULTIMATE PERFORMANCE! ⚡
void JIT::RegisterHotkey(const std::string& combination, const ast::Expression& action) {
    // 1. Generate unique function name
    std::string funcName = "hotkey_" + combination;
    std::replace(funcName.begin(), funcName.end(), '+', '_');
    std::replace(funcName.begin(), funcName.end(), ' ', '_');

    // 2. Compile the action to LLVM IR
    llvm::Function* func = compiler.CompileHotkeyAction(action);
    func->setName(funcName);

    // 3. JIT compile to native machine code
    auto compiledFunc = compiler.GetCompiledFunction(funcName);

    // 4. Store compiled function
    compiledHotkeys[combination] = compiledFunc;

    // 5. Register with IO system using NATIVE CALLBACK
    io->AddHotkey(combination, [this, combination]() {
        ExecuteHotkey(combination);  // Direct machine code execution!
    });

    std::cout << "🔥 Compiled hotkey '" << combination << "' to native code!" << std::endl;
}

void JIT::ExecuteHotkey(const std::string& combination) {
    auto it = compiledHotkeys.find(combination);
    if (it != compiledHotkeys.end()) {
        // Execute pure machine code - BLAZING FAST! ⚡
        it->second();
    }
}