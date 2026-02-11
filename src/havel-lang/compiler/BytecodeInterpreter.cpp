#include "Bytecode.h"
#include "../runtime/Interpreter.hpp"
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>

namespace havel::compiler {

// Bytecode interpreter implementation
class HavelBytecodeInterpreter : public BytecodeInterpreter {
private:
    std::stack<BytecodeValue> stack;
    std::vector<BytecodeValue> locals;
    std::unordered_map<std::string, BytecodeValue> globals;
    const BytecodeChunk* current_chunk;
    const BytecodeFunction* current_function;
    size_t instruction_pointer;
    bool debug_mode = false;
    
    // Helper functions
    template<typename T>
    T getValue(const BytecodeValue& value) {
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return std::get<std::nullptr_t>(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            return std::get<bool>(value);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::get<int64_t>(value);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::get<double>(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::get<std::string>(value);
        }
        throw std::runtime_error("Invalid type conversion");
    }
    
    BytecodeValue getConstant(uint32_t index) {
        return current_function->constants[index];
    }
    
    void push(const BytecodeValue& value) {
        stack.push(value);
        if (debug_mode) {
            std::cout << "PUSH: " << toString(value) << std::endl;
        }
    }
    
    BytecodeValue pop() {
        if (stack.empty()) {
            throw std::runtime_error("Stack underflow");
        }
        BytecodeValue value = stack.top();
        stack.pop();
        if (debug_mode) {
            std::cout << "POP: " << toString(value) << std::endl;
        }
        return value;
    }
    
    std::string toString(const BytecodeValue& value) {
        if (std::holds_alternative<std::nullptr_t>(value)) return "null";
        if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
        if (std::holds_alternative<int64_t>(value)) return std::to_string(std::get<int64_t>(value));
        if (std::holds_alternative<double>(value)) return std::to_string(std::get<double>(value));
        if (std::holds_alternative<std::string>(value)) return "\"" + std::get<std::string>(value) + "\"";
        if (std::holds_alternative<uint32_t>(value)) return "const[" + std::to_string(std::get<uint32_t>(value)) + "]";
        return "unknown";
    }
    
    BytecodeValue performBinaryOp(OpCode op, const BytecodeValue& left, const BytecodeValue& right) {
        // Handle numeric operations
        if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
            int64_t l = std::get<int64_t>(left);
            int64_t r = std::get<int64_t>(right);
            
            switch (op) {
                case OpCode::ADD: return l + r;
                case OpCode::SUB: return l - r;
                case OpCode::MUL: return l * r;
                case OpCode::DIV: return r != 0 ? l / r : throw std::runtime_error("Division by zero");
                case OpCode::MOD: return r != 0 ? l % r : throw std::runtime_error("Modulo by zero");
                case OpCode::EQ: return l == r;
                case OpCode::NEQ: return l != r;
                case OpCode::LT: return l < r;
                case OpCode::LTE: return l <= r;
                case OpCode::GT: return l > r;
                case OpCode::GTE: return l >= r;
                default: throw std::runtime_error("Invalid integer operation");
            }
        }
        
        // Handle double operations
        if ((std::holds_alternative<int64_t>(left) || std::holds_alternative<double>(left)) &&
            (std::holds_alternative<int64_t>(right) || std::holds_alternative<double>(right))) {
            double l = std::holds_alternative<int64_t>(left) ? std::get<int64_t>(left) : std::get<double>(left);
            double r = std::holds_alternative<int64_t>(right) ? std::get<int64_t>(right) : std::get<double>(right);
            
            switch (op) {
                case OpCode::ADD: return l + r;
                case OpCode::SUB: return l - r;
                case OpCode::MUL: return l * r;
                case OpCode::DIV: return r != 0.0 ? l / r : throw std::runtime_error("Division by zero");
                case OpCode::MOD: return r != 0.0 ? fmod(l, r) : throw std::runtime_error("Modulo by zero");
                case OpCode::POW: return pow(l, r);
                case OpCode::EQ: return l == r;
                case OpCode::NEQ: return l != r;
                case OpCode::LT: return l < r;
                case OpCode::LTE: return l <= r;
                case OpCode::GT: return l > r;
                case OpCode::GTE: return l >= r;
                default: throw std::runtime_error("Invalid float operation");
            }
        }
        
        // Handle string operations
        if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
            std::string l = std::get<std::string>(left);
            std::string r = std::get<std::string>(right);
            
            switch (op) {
                case OpCode::ADD: return l + r;
                case OpCode::EQ: return l == r;
                case OpCode::NEQ: return l != r;
                default: throw std::runtime_error("Invalid string operation");
            }
        }
        
        throw std::runtime_error("Type mismatch in binary operation");
    }
    
public:
    HavelBytecodeInterpreter() : current_chunk(nullptr), current_function(nullptr), instruction_pointer(0) {}
    
    void setDebugMode(bool enabled) override {
        debug_mode = enabled;
    }
    
    BytecodeValue execute(const BytecodeChunk& chunk, const std::string& entry_point) override {
        current_chunk = &chunk;
        current_function = chunk.getFunction(entry_point);
        
        if (!current_function) {
            throw std::runtime_error("Function not found: " + entry_point);
        }
        
        // Initialize locals
        locals.resize(current_function->local_count);
        instruction_pointer = 0;
        
        // Clear stack
        while (!stack.empty()) stack.pop();
        
        if (debug_mode) {
            std::cout << "=== Executing function: " << entry_point << " ===" << std::endl;
        }
        
        // Main execution loop
        while (instruction_pointer < current_function->instructions.size()) {
            const auto& instruction = current_function->instructions[instruction_pointer];
            
            if (debug_mode) {
                std::cout << "IP: " << instruction_pointer << " OP: " << static_cast<int>(instruction.opcode) << std::endl;
            }
            
            executeInstruction(instruction);
            instruction_pointer++;
        }
        
        if (stack.empty()) {
            return nullptr;
        }
        
        return pop();
    }
    
private:
    void executeInstruction(const Instruction& instruction) {
        switch (instruction.opcode) {
            case OpCode::LOAD_CONST: {
                uint32_t const_index = std::get<uint32_t>(instruction.operands[0]);
                push(getConstant(const_index));
                break;
            }
            
            case OpCode::LOAD_VAR: {
                uint32_t var_index = std::get<uint32_t>(instruction.operands[0]);
                if (var_index < locals.size()) {
                    push(locals[var_index]);
                } else {
                    throw std::runtime_error("Variable index out of bounds");
                }
                break;
            }
            
            case OpCode::STORE_VAR: {
                uint32_t var_index = std::get<uint32_t>(instruction.operands[0]);
                if (var_index < locals.size()) {
                    locals[var_index] = pop();
                } else {
                    throw std::runtime_error("Variable index out of bounds");
                }
                break;
            }
            
            case OpCode::POP: {
                pop();
                break;
            }
            
            case OpCode::DUP: {
                BytecodeValue value = pop();
                push(value);
                push(value);
                break;
            }
            
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::MOD:
            case OpCode::POW:
            case OpCode::EQ:
            case OpCode::NEQ:
            case OpCode::LT:
            case OpCode::LTE:
            case OpCode::GT:
            case OpCode::GTE: {
                BytecodeValue right = pop();
                BytecodeValue left = pop();
                push(performBinaryOp(instruction.opcode, left, right));
                break;
            }
            
            case OpCode::AND: {
                BytecodeValue right = pop();
                BytecodeValue left = pop();
                bool result = getValue<bool>(left) && getValue<bool>(right);
                push(result);
                break;
            }
            
            case OpCode::OR: {
                BytecodeValue right = pop();
                BytecodeValue left = pop();
                bool result = getValue<bool>(left) || getValue<bool>(right);
                push(result);
                break;
            }
            
            case OpCode::NOT: {
                BytecodeValue value = pop();
                bool result = !getValue<bool>(value);
                push(result);
                break;
            }
            
            case OpCode::JUMP: {
                uint32_t target = std::get<uint32_t>(instruction.operands[0]);
                instruction_pointer = target - 1; // -1 because loop will increment
                break;
            }
            
            case OpCode::JUMP_IF_FALSE: {
                uint32_t target = std::get<uint32_t>(instruction.operands[0]);
                BytecodeValue condition = pop();
                if (!getValue<bool>(condition)) {
                    instruction_pointer = target - 1; // -1 because loop will increment
                }
                break;
            }
            
            case OpCode::JUMP_IF_TRUE: {
                uint32_t target = std::get<uint32_t>(instruction.operands[0]);
                BytecodeValue condition = pop();
                if (getValue<bool>(condition)) {
                    instruction_pointer = target - 1; // -1 because loop will increment
                }
                break;
            }
            
            case OpCode::CALL: {
                uint32_t arg_count = std::get<uint32_t>(instruction.operands[0]);
                // For now, just pop arguments and return null
                for (uint32_t i = 0; i < arg_count; i++) {
                    pop();
                }
                push(nullptr); // Placeholder return value
                break;
            }
            
            case OpCode::RETURN: {
                // Return value is already on stack
                // Exit execution loop
                instruction_pointer = current_function->instructions.size();
                break;
            }
            
            case OpCode::ARRAY_NEW: {
                uint32_t size = std::get<uint32_t>(instruction.operands[0]);
                // For now, just return null placeholder
                push(nullptr);
                break;
            }
            
            case OpCode::OBJECT_NEW: {
                uint32_t size = std::get<uint32_t>(instruction.operands[0]);
                // For now, just return null placeholder
                push(nullptr);
                break;
            }
            
            case OpCode::PRINT: {
                BytecodeValue value = pop();
                std::cout << toString(value) << std::endl;
                break;
            }
            
            case OpCode::DEBUG: {
                std::cout << "DEBUG: Stack size: " << stack.size() << std::endl;
                std::cout << "DEBUG: Locals size: " << locals.size() << std::endl;
                break;
            }
            
            case OpCode::NOP:
                break;
                
            default:
                throw std::runtime_error("Unknown opcode: " + std::to_string(static_cast<int>(instruction.opcode)));
        }
    }
};

// Factory function
std::unique_ptr<BytecodeInterpreter> createBytecodeInterpreter() {
    return std::make_unique<HavelBytecodeInterpreter>();
}

} // namespace havel::compiler
