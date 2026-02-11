#include "Bytecode.h"
#include <iostream>
#include <unordered_map>

namespace havel::compiler {

// Simple JIT compiler (placeholder implementation)
class HavelJITCompiler : public JITCompiler {
private:
    std::unordered_map<std::string, bool> compiled_functions;
    
public:
    void compileFunction(const BytecodeFunction& func) override {
        // Placeholder: just mark as compiled
        compiled_functions[func.name] = true;
        std::cout << "JIT: Compiled function '" << func.name << "'" << std::endl;
    }
    
    BytecodeValue executeCompiled(const std::string& func_name, const std::vector<BytecodeValue>& args) override {
        // Placeholder: just return null
        std::cout << "JIT: Executing compiled function '" << func_name << "'" << std::endl;
        return nullptr;
    }
    
    bool isCompiled(const std::string& func_name) const override {
        auto it = compiled_functions.find(func_name);
        return it != compiled_functions.end() && it->second;
    }
};

// Hybrid engine implementation
HybridEngine::HybridEngine(
    std::unique_ptr<BytecodeCompiler> comp,
    std::unique_ptr<BytecodeInterpreter> interp,
    std::unique_ptr<JITCompiler> jcomp
) : compiler(std::move(comp)), interpreter(std::move(interp)), jit(std::move(jcomp)) {}

bool HybridEngine::compile(const ast::Program& program) {
    try {
        current_chunk = compiler->compile(program);
        return current_chunk != nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        return false;
    }
}

BytecodeValue HybridEngine::execute(const std::string& function_name, const std::vector<BytecodeValue>& args) {
    if (!current_chunk) {
        throw std::runtime_error("No compiled program available");
    }
    
    // Update execution count
    execution_counts[function_name]++;
    
    // Check if we should JIT compile this function
    if (jit_enabled && jit && !jit->isCompiled(function_name)) {
        if (execution_counts[function_name] >= jit_threshold) {
            const auto* func = current_chunk->getFunction(function_name);
            if (func) {
                jit->compileFunction(*func);
            }
        }
    }
    
    // Execute with JIT if available and compiled
    if (jit && jit->isCompiled(function_name)) {
        return jit->executeCompiled(function_name, args);
    }
    
    // Fall back to interpreter
    return interpreter->execute(*current_chunk, function_name);
}

// Factory function
std::unique_ptr<HybridEngine> createHybridEngine() {
    auto compiler = std::make_unique<HavelBytecodeCompiler>();
    auto interpreter = std::make_unique<HavelBytecodeInterpreter>();
    auto jit = std::make_unique<HavelJITCompiler>();
    
    return std::make_unique<HybridEngine>(
        std::move(compiler),
        std::move(interpreter), 
        std::move(jit)
    );
}

} // namespace havel::compiler
