#include "Bytecode.h"
#include "../ast/AST.h"
#include "../runtime/Interpreter.hpp"
#include <iostream>
#include <sstream>

namespace havel::compiler {

// Bytecode compiler implementation
class HavelBytecodeCompiler : public BytecodeCompiler {
private:
    std::unique_ptr<BytecodeChunk> chunk;
    BytecodeFunction* current_function;
    std::unordered_map<std::string, uint32_t> variable_indices;
    uint32_t next_var_index = 0;
    
    uint32_t addConstant(const BytecodeValue& value) {
        current_function->constants.push_back(value);
        return current_function->constants.size() - 1;
    }
    
    uint32_t getVariableIndex(const std::string& name) {
        auto it = variable_indices.find(name);
        if (it != variable_indices.end()) {
            return it->second;
        }
        uint32_t index = next_var_index++;
        variable_indices[name] = index;
        return index;
    }
    
    void emit(OpCode opcode, std::vector<BytecodeValue> operands = {}) {
        current_function->instructions.emplace_back(opcode, std::move(operands));
    }
    
public:
    HavelBytecodeCompiler() : chunk(std::make_unique<BytecodeChunk>()) {}
    
    std::unique_ptr<BytecodeChunk> compile(const ast::Program& program) override {
        // Create main function
        current_function = new BytecodeFunction("main", 0, 0);
        variable_indices.clear();
        next_var_index = 0;
        
        // Compile program statements
        for (const auto& stmt : program.statements) {
            compileStatement(*stmt);
        }
        
        // Add return statement if none exists
        emit(OpCode::RETURN, {0}); // Return null
        
        chunk->addFunction(*current_function);
        delete current_function;
        
        return std::move(chunk);
    }
    
private:
    void compileStatement(const ast::Statement& stmt) {
        if (auto expr_stmt = dynamic_cast<const ast::ExpressionStatement*>(&stmt)) {
            compileExpression(*expr_stmt->expression);
            emit(OpCode::POP); // Discard result
        }
        else if (auto var_decl = dynamic_cast<const ast::VariableDeclaration*>(&stmt)) {
            compileExpression(*var_decl->initializer);
            uint32_t var_index = getVariableIndex(var_decl->name);
            emit(OpCode::STORE_VAR, {var_index});
        }
        else if (auto func_decl = dynamic_cast<const ast::FunctionDeclaration*>(&stmt)) {
            compileFunction(*func_decl);
        }
        else if (auto if_stmt = dynamic_cast<const ast::IfStatement*>(&stmt)) {
            compileIfStatement(*if_stmt);
        }
        else if (auto while_stmt = dynamic_cast<const ast::WhileStatement*>(&stmt)) {
            compileWhileStatement(*while_stmt);
        }
        else if (auto ret_stmt = dynamic_cast<const ast::ReturnStatement*>(&stmt)) {
            if (ret_stmt->value) {
                compileExpression(*ret_stmt->value);
            } else {
                emit(OpCode::LOAD_CONST, {addConstant(nullptr)});
            }
            emit(OpCode::RETURN);
        }
    }
    
    void compileExpression(const ast::Expression& expr) {
        if (auto literal = dynamic_cast<const ast::NumberLiteral*>(&expr)) {
            emit(OpCode::LOAD_CONST, {addConstant(literal->value)});
        }
        else if (auto string_literal = dynamic_cast<const ast::StringLiteral*>(&expr)) {
            emit(OpCode::LOAD_CONST, {addConstant(string_literal->value)});
        }
        else if (auto bool_literal = dynamic_cast<const ast::BooleanLiteral*>(&expr)) {
            emit(OpCode::LOAD_CONST, {addConstant(bool_literal->value)});
        }
        else if (auto null_literal = dynamic_cast<const ast::NullLiteral*>(&expr)) {
            emit(OpCode::LOAD_CONST, {addConstant(nullptr)});
        }
        else if (auto var_ref = dynamic_cast<const ast::VariableReference*>(&expr)) {
            uint32_t var_index = getVariableIndex(var_ref->name);
            emit(OpCode::LOAD_VAR, {var_index});
        }
        else if (auto binary = dynamic_cast<const ast::BinaryExpression*>(&expr)) {
            compileBinaryExpression(*binary);
        }
        else if (auto call = dynamic_cast<const ast::CallExpression*>(&expr)) {
            compileCallExpression(*call);
        }
        else if (auto array_lit = dynamic_cast<const ast::ArrayLiteral*>(&expr)) {
            compileArrayLiteral(*array_lit);
        }
        else if (auto obj_lit = dynamic_cast<const ast::ObjectLiteral*>(&expr)) {
            compileObjectLiteral(*obj_lit);
        }
    }
    
    void compileBinaryExpression(const ast::BinaryExpression& expr) {
        compileExpression(*expr.left);
        compileExpression(*expr.right);
        
        switch (expr.op) {
            case ast::BinaryOperator::Add: emit(OpCode::ADD); break;
            case ast::BinaryOperator::Subtract: emit(OpCode::SUB); break;
            case ast::BinaryOperator::Multiply: emit(OpCode::MUL); break;
            case ast::BinaryOperator::Divide: emit(OpCode::DIV); break;
            case ast::BinaryOperator::Modulo: emit(OpCode::MOD); break;
            case ast::BinaryOperator::Power: emit(OpCode::POW); break;
            case ast::BinaryOperator::Equal: emit(OpCode::EQ); break;
            case ast::BinaryOperator::NotEqual: emit(OpCode::NEQ); break;
            case ast::BinaryOperator::LessThan: emit(OpCode::LT); break;
            case ast::BinaryOperator::LessEqual: emit(OpCode::LTE); break;
            case ast::BinaryOperator::GreaterThan: emit(OpCode::GT); break;
            case ast::BinaryOperator::GreaterEqual: emit(OpCode::GTE); break;
            case ast::BinaryOperator::And: emit(OpCode::AND); break;
            case ast::BinaryOperator::Or: emit(OpCode::OR); break;
        }
    }
    
    void compileCallExpression(const ast::CallExpression& expr) {
        // Compile arguments
        for (const auto& arg : expr.arguments) {
            compileExpression(*arg);
        }
        
        // Compile callee
        compileExpression(*expr.callee);
        
        emit(OpCode::CALL, {static_cast<uint32_t>(expr.arguments.size())});
    }
    
    void compileArrayLiteral(const ast::ArrayLiteral& expr) {
        for (const auto& element : expr.elements) {
            compileExpression(*element);
        }
        emit(OpCode::ARRAY_NEW, {static_cast<uint32_t>(expr.elements.size())});
    }
    
    void compileObjectLiteral(const ast::ObjectLiteral& expr) {
        for (const auto& [key, value] : expr.properties) {
            compileExpression(*value);
            emit(OpCode::LOAD_CONST, {addConstant(key)});
        }
        emit(OpCode::OBJECT_NEW, {static_cast<uint32_t>(expr.properties.size())});
    }
    
    void compileFunction(const ast::FunctionDeclaration& func) {
        // Save current function state
        BytecodeFunction* saved_function = current_function;
        auto saved_variables = variable_indices;
        uint32_t saved_next_var = next_var_index;
        
        // Create new function
        current_function = new BytecodeFunction(func.name, func.parameters.size(), 0);
        variable_indices.clear();
        next_var_index = 0;
        
        // Add parameters as variables
        for (uint32_t i = 0; i < func.parameters.size(); i++) {
            variable_indices[func.parameters[i]] = i;
        }
        
        // Compile function body
        for (const auto& stmt : func.body->statements) {
            compileStatement(*stmt);
        }
        
        // Add return if none exists
        emit(OpCode::LOAD_CONST, {addConstant(nullptr)});
        emit(OpCode::RETURN);
        
        chunk->addFunction(*current_function);
        delete current_function;
        
        // Restore state
        current_function = saved_function;
        variable_indices = saved_variables;
        next_var_index = saved_next_var;
    }
    
    void compileIfStatement(const ast::IfStatement& stmt) {
        compileExpression(*stmt.condition);
        
        // Placeholder for jump if false
        size_t jump_false_pos = current_function->instructions.size();
        emit(OpCode::JUMP_IF_FALSE, {0});
        
        // Compile then branch
        for (const auto& then_stmt : stmt.thenBranch->statements) {
            compileStatement(*then_stmt);
        }
        
        // Placeholder for jump over else
        size_t jump_else_pos = current_function->instructions.size();
        emit(OpCode::JUMP, {0});
        
        // Fix jump if false
        current_function->instructions[jump_false_pos].operands[0] = 
            static_cast<uint32_t>(current_function->instructions.size());
        
        // Compile else branch if exists
        if (stmt.elseBranch) {
            for (const auto& else_stmt : stmt.elseBranch->statements) {
                compileStatement(*else_stmt);
            }
        }
        
        // Fix jump over else
        current_function->instructions[jump_else_pos].operands[0] = 
            static_cast<uint32_t>(current_function->instructions.size());
    }
    
    void compileWhileStatement(const ast::WhileStatement& stmt) {
        size_t loop_start = current_function->instructions.size();
        
        compileExpression(*stmt.condition);
        
        size_t jump_false_pos = current_function->instructions.size();
        emit(OpCode::JUMP_IF_FALSE, {0});
        
        for (const auto& body_stmt : stmt.body->statements) {
            compileStatement(*body_stmt);
        }
        
        emit(OpCode::JUMP, {static_cast<uint32_t>(loop_start)});
        
        // Fix jump if false
        current_function->instructions[jump_false_pos].operands[0] = 
            static_cast<uint32_t>(current_function->instructions.size());
    }
};

// Factory function
std::unique_ptr<BytecodeCompiler> createBytecodeCompiler() {
    return std::make_unique<HavelBytecodeCompiler>();
}

} // namespace havel::compiler
