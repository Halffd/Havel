#pragma once

#include "../lexer/Lexer.hpp"
#include "../parser/Parser.h"
#include "../ast/AST.h"
#include "../../window/Window.hpp"
#include "../../window/WindowManager.hpp"
#include "../../gui/Clipboard.hpp"
#include "../../core/IO.hpp"
#include "../../utils/Logger.hpp"

#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <variant>

namespace havel {

// Forward declarations
class Window;

// Value type for the interpreter
using HavelValue = std::variant<
    std::nullptr_t,
    bool,
    int,
    double,
    std::string,
    std::vector<std::string>
>;

// Function type for built-in functions
using BuiltinFunction = std::function<HavelValue(const std::vector<HavelValue>&)>;

// Module class to organize related functions
class Module {
public:
    Module(const std::string& name) : name(name) {}
    
    void AddFunction(const std::string& name, BuiltinFunction func) {
        functions[name] = func;
    }
    
    BuiltinFunction GetFunction(const std::string& name) const {
        auto it = functions.find(name);
        if (it != functions.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    bool HasFunction(const std::string& name) const {
        return functions.find(name) != functions.end();
    }
    
    std::string GetName() const { return name; }
    
private:
    std::string name;
    std::unordered_map<std::string, BuiltinFunction> functions;
};

// Environment class to store variables and functions
class Environment {
public:
    Environment() = default;
    
    void DefineVariable(const std::string& name, const HavelValue& value) {
        variables[name] = value;
    }
    
    HavelValue GetVariable(const std::string& name) const {
        auto it = variables.find(name);
        if (it != variables.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    bool HasVariable(const std::string& name) const {
        return variables.find(name) != variables.end();
    }
    
    void AddModule(std::shared_ptr<Module> module) {
        modules[module->GetName()] = module;
    }
    
    std::shared_ptr<Module> GetModule(const std::string& name) const {
        auto it = modules.find(name);
        if (it != modules.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    bool HasModule(const std::string& name) const {
        return modules.find(name) != modules.end();
    }
    
private:
    std::unordered_map<std::string, HavelValue> variables;
    std::unordered_map<std::string, std::shared_ptr<Module>> modules;
};

// Main Interpreter class
class Interpreter {
public:
    Interpreter();
    ~Interpreter() = default;
    
    // Execute Havel code
    HavelValue Execute(const std::string& sourceCode);
    
    // Register hotkeys from Havel code
    void RegisterHotkeys(const std::string& sourceCode);
    
    // Evaluate AST nodes
    HavelValue EvaluateProgram(const ast::Program& program);
    HavelValue EvaluateStatement(const ast::Statement& statement);
    HavelValue EvaluateExpression(const ast::Expression& expression);
    HavelValue EvaluateHotkeyBinding(const ast::HotkeyBinding& binding);
    HavelValue EvaluateBlockStatement(const ast::BlockStatement& block);
    HavelValue EvaluatePipelineExpression(const ast::PipelineExpression& pipeline);
    HavelValue EvaluateBinaryExpression(const ast::BinaryExpression& binary);
    HavelValue EvaluateCallExpression(const ast::CallExpression& call);
    HavelValue EvaluateMemberExpression(const ast::MemberExpression& member);
    HavelValue EvaluateStringLiteral(const ast::StringLiteral& str);
    HavelValue EvaluateNumberLiteral(const ast::NumberLiteral& num);
    HavelValue EvaluateIdentifier(const ast::Identifier& id);
    
    // Initialize built-in modules and functions
    void InitializeStandardLibrary();
    
    // Helper methods for value conversion
    static std::string ValueToString(const HavelValue& value);
    static bool ValueToBool(const HavelValue& value);
    static double ValueToNumber(const HavelValue& value);
    
private:
    Environment environment;
    std::shared_ptr<havel::IO> io;
    std::shared_ptr<H::Clipboard> clipboard;
    std::shared_ptr<havel::WindowManager> windowManager;
    
    // Initialize built-in modules
    void InitializeClipboardModule();
    void InitializeTextModule();
    void InitializeWindowModule();
    void InitializeSystemModule();
};

} // namespace havel
