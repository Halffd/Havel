#include "Interpreter.hpp"
#include "core/HotkeyManager.hpp"
#include "core/BrightnessManager.hpp"
#include "media/AudioManager.hpp"
#include "gui/GUIManager.hpp"
#include "process/Launcher.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <random>
#include <cmath>

namespace havel {

// Module cache to avoid re-loading and re-executing files
static std::unordered_map<std::string, HavelObject> moduleCache;

// Helper to check for and extract error from HavelResult
static bool isError(const HavelResult& result) {
    return std::holds_alternative<HavelRuntimeError>(result);
}

static HavelValue unwrap(HavelResult& result) {
    if (auto* val = std::get_if<HavelValue>(&result)) {
        return *val;
    }
    if (auto* ret = std::get_if<ReturnValue>(&result)) {
        return ret->value;
    }
    // This should not be called on an error.
    throw std::get<HavelRuntimeError>(result);
}

std::string Interpreter::ValueToString(const HavelValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
        else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, std::string>) return arg;
        else if constexpr (std::is_same_v<T, std::shared_ptr<HavelFunction>>) return "<function>";
        else if constexpr (std::is_same_v<T, BuiltinFunction>) return "<builtin_function>";
        else if constexpr (std::is_same_v<T, HavelArray>) {
            // Recursively format array in JSON style
            std::string result = "[";
            if (arg) {
                for (size_t i = 0; i < arg->size(); ++i) {
                    result += ValueToString((*arg)[i]);
                    if (i < arg->size() - 1) result += ", ";
                }
            }
            result += "]";
            return result;
        }
        else if constexpr (std::is_same_v<T, HavelObject>) {
            // Recursively format object in JSON style
            std::string result = "{";
            if (arg) {
                size_t i = 0;
                for (const auto& [key, val] : *arg) {
                    result += key + ": " + ValueToString(val);
                    if (i < arg->size() - 1) result += ", ";
                    ++i;
                }
            }
            result += "}";
            return result;
        }
        else return "unprintable";
    }, value);
}

bool Interpreter::ValueToBool(const HavelValue& value) {
    return std::visit([](auto&& arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return false;
        else if constexpr (std::is_same_v<T, bool>) return arg;
        else if constexpr (std::is_same_v<T, int>) return arg != 0;
        else if constexpr (std::is_same_v<T, double>) return arg != 0.0;
        else if constexpr (std::is_same_v<T, std::string>) return !arg.empty();
        else return true; // Functions, objects, arrays are truthy
    }, value);
}

double Interpreter::ValueToNumber(const HavelValue& value) {
     return std::visit([](auto&& arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return 0.0;
        else if constexpr (std::is_same_v<T, bool>) return arg ? 1.0 : 0.0;
        else if constexpr (std::is_same_v<T, int>) return static_cast<double>(arg);
        else if constexpr (std::is_same_v<T, double>) return arg;
        else if constexpr (std::is_same_v<T, std::string>) {
            try { return std::stod(arg); } catch(...) { return 0.0; }
        }
        return 0.0;
    }, value);
}

// Constructor with Dependency Injection
Interpreter::Interpreter(IO& io_system, WindowManager& window_mgr,
                        HotkeyManager* hotkey_mgr,
                        BrightnessManager* brightness_mgr,
                        AudioManager* audio_mgr,
                        GUIManager* gui_mgr)
    : io(io_system), windowManager(window_mgr),
      hotkeyManager(hotkey_mgr), brightnessManager(brightness_mgr),
      audioManager(audio_mgr), guiManager(gui_mgr), lastResult(nullptr) {
    environment = std::make_shared<Environment>();
    InitializeStandardLibrary();
}

HavelResult Interpreter::Execute(const std::string& sourceCode) {
    parser::Parser parser;
    auto program = parser.produceAST(sourceCode);
    auto* programPtr = program.get();
    // Keep the AST alive to avoid dangling pointers captured in functions/closures
    loadedPrograms.push_back(std::move(program));
    return Evaluate(*programPtr);
}

void Interpreter::RegisterHotkeys(const std::string& sourceCode) {
    Execute(sourceCode); // Evaluation now handles hotkey registration
}

HavelResult Interpreter::Evaluate(const ast::ASTNode& node) {
    const_cast<ast::ASTNode&>(node).accept(*this);
    return lastResult;
}

void Interpreter::visitProgram(const ast::Program& node) {
    HavelValue lastValue = nullptr;
    for (const auto& stmt : node.body) {
        auto result = Evaluate(*stmt);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        if (std::holds_alternative<ReturnValue>(result)) {
            lastResult = std::get<ReturnValue>(result).value;
            return;
        }
        lastValue = unwrap(result);
    }
    lastResult = lastValue;
}

void Interpreter::visitLetDeclaration(const ast::LetDeclaration& node) {
    HavelValue value = nullptr;
    if (node.value) {
        auto result = Evaluate(*node.value);
        if(isError(result)) {
            lastResult = result;
            return;
        }
        value = unwrap(result);
    }
    environment->Define(node.name->symbol, value);
    lastResult = value;
}

void Interpreter::visitFunctionDeclaration(const ast::FunctionDeclaration& node) {
    auto func = std::make_shared<HavelFunction>(HavelFunction{
        &node,
        this->environment // Capture closure
    });
    environment->Define(node.name->symbol, func);
    lastResult = nullptr;
}

void Interpreter::visitReturnStatement(const ast::ReturnStatement& node) {
    HavelValue value = nullptr;
    if (node.argument) {
        auto result = Evaluate(*node.argument);
        if(isError(result)) {
            lastResult = result;
            return;
        }
        value = unwrap(result);
    }
    lastResult = ReturnValue{value};
}

void Interpreter::visitIfStatement(const ast::IfStatement& node) {
    auto conditionResult = Evaluate(*node.condition);
    if(isError(conditionResult)) {
        lastResult = conditionResult;
        return;
    }

    if (ValueToBool(unwrap(conditionResult))) {
        lastResult = Evaluate(*node.consequence);
    } else if (node.alternative) {
        lastResult = Evaluate(*node.alternative);
    } else {
        lastResult = nullptr;
    }
}

void Interpreter::visitBlockStatement(const ast::BlockStatement& node) {
    auto blockEnv = std::make_shared<Environment>(this->environment);
    auto originalEnv = this->environment;
    this->environment = blockEnv;

    HavelResult blockResult = HavelValue(nullptr);
    for (const auto& stmt : node.body) {
        blockResult = Evaluate(*stmt);
        if (isError(blockResult) || 
            std::holds_alternative<ReturnValue>(blockResult) ||
            std::holds_alternative<BreakValue>(blockResult) ||
            std::holds_alternative<ContinueValue>(blockResult)) {
            break;
        }
    }

    this->environment = originalEnv;
    lastResult = blockResult;
}

void Interpreter::visitHotkeyBinding(const ast::HotkeyBinding& node) {
    auto hotkeyLiteral = dynamic_cast<const ast::HotkeyLiteral*>(node.hotkey.get());
    if (!hotkeyLiteral) {
        lastResult = HavelRuntimeError("Invalid hotkey in binding");
        return;
    }

    std::string hotkey = hotkeyLiteral->combination;

    // Evaluate the action now so Execute() returns the action value for tests
    HavelResult actionEval = Evaluate(*node.action);
    if (isError(actionEval)) { lastResult = actionEval; return; }
    lastResult = actionEval;

    // Keep the action node alive for runtime hotkey execution
    auto action = node.action.get();

    auto actionHandler = [this, action]() {
        if (action) {
            auto result = this->Evaluate(*action);
            if (isError(result)) {
                std::cerr << "Runtime error in hotkey: "
                          << std::get<HavelRuntimeError>(result).what() << std::endl;
            }
        }
    };

    io.Hotkey(hotkey, actionHandler);
}

void Interpreter::visitExpressionStatement(const ast::ExpressionStatement& node) {
    lastResult = Evaluate(*node.expression);
}

void Interpreter::visitBinaryExpression(const ast::BinaryExpression& node) {
    auto leftRes = Evaluate(*node.left);
    if(isError(leftRes)) { lastResult = leftRes; return; }
    auto rightRes = Evaluate(*node.right);
    if(isError(rightRes)) { lastResult = rightRes; return; }
    
    HavelValue left = unwrap(leftRes);
    HavelValue right = unwrap(rightRes);

    switch(node.operator_){
        case ast::BinaryOperator::Add:
            if(std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                lastResult = ValueToString(left) + ValueToString(right);
            } else {
                lastResult = ValueToNumber(left) + ValueToNumber(right);
            }
            break;
        case ast::BinaryOperator::Sub: 
            lastResult = ValueToNumber(left) - ValueToNumber(right); 
            break;
        case ast::BinaryOperator::Mul: 
            lastResult = ValueToNumber(left) * ValueToNumber(right); 
            break;
        case ast::BinaryOperator::Div:
            if(ValueToNumber(right) == 0.0) { lastResult = HavelRuntimeError("Division by zero"); return; }
            lastResult = ValueToNumber(left) / ValueToNumber(right); 
            break;
        case ast::BinaryOperator::Mod:
            if(ValueToNumber(right) == 0.0) { lastResult = HavelRuntimeError("Modulo by zero"); return; }
            lastResult = static_cast<int>(ValueToNumber(left)) % static_cast<int>(ValueToNumber(right)); 
            break;
        case ast::BinaryOperator::Equal:
            lastResult = (ValueToString(left) == ValueToString(right));
            break;
        case ast::BinaryOperator::NotEqual:
            lastResult = (ValueToString(left) != ValueToString(right));
            break;
        case ast::BinaryOperator::Less:
            lastResult = ValueToNumber(left) < ValueToNumber(right);
            break;
        case ast::BinaryOperator::Greater:
            lastResult = ValueToNumber(left) > ValueToNumber(right);
            break;
        case ast::BinaryOperator::LessEqual:
            lastResult = ValueToNumber(left) <= ValueToNumber(right);
            break;
        case ast::BinaryOperator::GreaterEqual:
            lastResult = ValueToNumber(left) >= ValueToNumber(right);
            break;
        case ast::BinaryOperator::And:
            lastResult = ValueToBool(left) && ValueToBool(right);
            break;
        case ast::BinaryOperator::Or:
            lastResult = ValueToBool(left) || ValueToBool(right);
            break;
        default: 
            lastResult = HavelRuntimeError("Unsupported binary operator");
    }
}

void Interpreter::visitUnaryExpression(const ast::UnaryExpression& node) {
    auto operandRes = Evaluate(*node.operand);
    if(isError(operandRes)) { lastResult = operandRes; return; }
    HavelValue operand = unwrap(operandRes);

    switch(node.operator_) {
        case ast::UnaryExpression::UnaryOperator::Not: lastResult = !ValueToBool(operand); break;
        case ast::UnaryExpression::UnaryOperator::Minus: lastResult = -ValueToNumber(operand); break;
        case ast::UnaryExpression::UnaryOperator::Plus: lastResult = ValueToNumber(operand); break;
        default: lastResult = HavelRuntimeError("Unsupported unary operator");
    }
}

void Interpreter::visitCallExpression(const ast::CallExpression& node) {
    auto calleeRes = Evaluate(*node.callee);
    if (isError(calleeRes)) { lastResult = calleeRes; return; }
    HavelValue callee = unwrap(calleeRes);

    std::vector<HavelValue> args;
    for (const auto& arg : node.args) {
        auto argRes = Evaluate(*arg);
        if (isError(argRes)) { lastResult = argRes; return; }
        args.push_back(unwrap(argRes));
    }

    if (auto* builtin = std::get_if<BuiltinFunction>(&callee)) {
        lastResult = (*builtin)(args);
    } else if (auto* userFunc = std::get_if<std::shared_ptr<HavelFunction>>(&callee)) {
        auto& func = *userFunc;
        if (args.size() != func->declaration->parameters.size()) {
            lastResult = HavelRuntimeError("Mismatched argument count for function " + func->declaration->name->symbol);
            return;
        }

        auto funcEnv = std::make_shared<Environment>(func->closure);
        for (size_t i = 0; i < args.size(); ++i) {
            funcEnv->Define(func->declaration->parameters[i]->symbol, args[i]);
        }

        auto originalEnv = this->environment;
        this->environment = funcEnv;
        auto bodyResult = Evaluate(*func->declaration->body);
        this->environment = originalEnv;

        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            lastResult = std::get<ReturnValue>(bodyResult).value;
        } else {
            lastResult = nullptr; // Implicit return
        }
    } else {
        lastResult = HavelRuntimeError("Attempted to call a non-callable value: " + ValueToString(callee));
    }
}

void Interpreter::visitMemberExpression(const ast::MemberExpression& node) {
    auto objectResult = Evaluate(*node.object);
    if (isError(objectResult)) { lastResult = objectResult; return; }
    HavelValue objectValue = unwrap(objectResult);

    auto* propId = dynamic_cast<const ast::Identifier*>(node.property.get());
    if (!propId) {
        lastResult = HavelRuntimeError("Invalid property access");
        return;
    }
    std::string propName = propId->symbol;

    // Objects: o.b
    if (auto* objPtr = std::get_if<HavelObject>(&objectValue)) {
        if (*objPtr) {
            auto it = (*objPtr)->find(propName);
            if (it != (*objPtr)->end()) {
                lastResult = it->second;
                return;
            }
        }
        lastResult = HavelValue(nullptr);
        return;
    }

    // Arrays: special properties like length
    if (auto* arrPtr = std::get_if<HavelArray>(&objectValue)) {
        if (propName == "length") {
            lastResult = static_cast<double>((*arrPtr) ? (*arrPtr)->size() : 0);
            return;
        }
    }

    lastResult = HavelRuntimeError("Member access not supported for this type");
}


void Interpreter::visitLambdaExpression(const ast::LambdaExpression& node) {
    // Capture current environment (closure)
    auto closureEnv = this->environment;
    // Build a callable that binds args to parameter names and evaluates body
    BuiltinFunction lambda = [this, closureEnv, &node](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != node.parameters.size()) {
            return HavelRuntimeError("Mismatched argument count for lambda");
        }
        auto funcEnv = std::make_shared<Environment>(closureEnv);
        for (size_t i = 0; i < args.size(); ++i) {
            funcEnv->Define(node.parameters[i]->symbol, args[i]);
        }
        auto originalEnv = this->environment;
        this->environment = funcEnv;
        auto res = Evaluate(*node.body);
        this->environment = originalEnv;
        if (std::holds_alternative<ReturnValue>(res)) return std::get<ReturnValue>(res).value;
        return res;
    };
    lastResult = HavelValue(lambda);
}

void Interpreter::visitPipelineExpression(const ast::PipelineExpression& node) {
    if (node.stages.empty()) {
        lastResult = nullptr;
        return;
    }

    HavelResult currentResult = Evaluate(*node.stages[0]);
    if (isError(currentResult)) {
        lastResult = currentResult;
        return;
    }

    for (size_t i = 1; i < node.stages.size(); ++i) {
        const auto& stage = node.stages[i];
        
        HavelValue currentValue = unwrap(currentResult);
        std::vector<HavelValue> args = { currentValue };
        
        const ast::Expression* calleeExpr = stage.get();
        if(const auto* call = dynamic_cast<const ast::CallExpression*>(stage.get())) {
            calleeExpr = call->callee.get();
            for(const auto& arg : call->args) {
                auto argRes = Evaluate(*arg);
                if(isError(argRes)) { lastResult = argRes; return; }
                args.push_back(unwrap(argRes));
            }
        }

        auto calleeRes = Evaluate(*calleeExpr);
        if(isError(calleeRes)) { lastResult = calleeRes; return; }
        
        HavelValue callee = unwrap(calleeRes);
        if (auto* builtin = std::get_if<BuiltinFunction>(&callee)) {
            currentResult = (*builtin)(args);
        } else if (auto* userFunc = std::get_if<std::shared_ptr<HavelFunction>>(&callee)) {
            // This logic is duplicated from visitCallExpression, could be refactored
            auto& func = *userFunc;
            if (args.size() != func->declaration->parameters.size()) {
                lastResult = HavelRuntimeError("Mismatched argument count for function in pipeline");
                return;
            }
            auto funcEnv = std::make_shared<Environment>(func->closure);
            for (size_t i = 0; i < args.size(); ++i) {
                funcEnv->Define(func->declaration->parameters[i]->symbol, args[i]);
            }
            auto originalEnv = this->environment;
            this->environment = funcEnv;
            currentResult = Evaluate(*func->declaration->body);
            this->environment = originalEnv;
            if(std::holds_alternative<ReturnValue>(currentResult)) {
                currentResult = std::get<ReturnValue>(currentResult).value;
            }

        } else {
            lastResult = HavelRuntimeError("Pipeline stage must be a callable function");
            return;
        }

        if(isError(currentResult)) { lastResult = currentResult; return; }
    }
    lastResult = currentResult;
}

void Interpreter::visitImportStatement(const ast::ImportStatement& node) {
    std::string path = node.modulePath;
    HavelObject exports;

    // Special case: no path provided -> import built-in modules by name
    if (path.empty()) {
        for (const auto& item : node.importedItems) {
            const std::string& moduleName = item.first;
            const std::string& alias = item.second;
            auto val = environment->Get(moduleName);
            if (!val || !std::holds_alternative<HavelObject>(*val)) {
                lastResult = HavelRuntimeError("Built-in module not found or not an object: " + moduleName);
                return;
            }
            environment->Define(alias, std::get<HavelObject>(*val));
        }
        lastResult = nullptr;
        return;
    }

    // Check cache first
    if (moduleCache.count(path)) {
        exports = moduleCache.at(path);
    } else {
        // Check for built-in modules by name (with or without 'havel:' prefix)
        std::string moduleName = path;
        if (path.rfind("havel:", 0) == 0) moduleName = path.substr(6);
        auto moduleVal = environment->Get(moduleName);
        if (moduleVal && std::holds_alternative<HavelObject>(*moduleVal)) {
            exports = std::get<HavelObject>(*moduleVal);
        } else if (!moduleVal) {
            // Load from file
            std::ifstream file(path);
            if (!file) {
                lastResult = HavelRuntimeError("Cannot open module file: " + path);
                return;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Execute module in a new environment
            Interpreter moduleInterpreter(io, windowManager);
            auto moduleResult = moduleInterpreter.Execute(source);
            if (isError(moduleResult)) {
                lastResult = moduleResult;
                return;
            }

            HavelValue exportedValue = unwrap(moduleResult);
            if (!std::holds_alternative<HavelObject>(exportedValue)) {
                lastResult = HavelRuntimeError("Module must return an object of exports: " + path);
                return;
            }
            exports = std::get<HavelObject>(exportedValue);
        } else {
            lastResult = HavelRuntimeError("Built-in module not found: " + moduleName);
            return;
        }
        // Cache the result
        moduleCache[path] = exports;
    }

    // Wildcard import: import * from module
    if (node.importedItems.size() == 1 && node.importedItems[0].first == "*") {
        if (exports) {
            for (const auto& [k, v] : *exports) {
                environment->Define(k, v);
            }
            lastResult = nullptr;
            return;
        }
    }

    // Import symbols into the current environment
    for (const auto& item : node.importedItems) {
        const std::string& originalName = item.first;
        const std::string& alias = item.second;
        
        if (exports && exports->count(originalName)) {
            environment->Define(alias, exports->at(originalName));
        } else {
            lastResult = HavelRuntimeError("Module '" + path + "' does not export symbol: " + originalName);
            return;
        }
    }

    lastResult = nullptr;
}


void Interpreter::visitStringLiteral(const ast::StringLiteral& node) { lastResult = node.value; }

void Interpreter::visitInterpolatedStringExpression(const ast::InterpolatedStringExpression& node) {
    std::string result;
    
    for (const auto& segment : node.segments) {
        if (segment.isString) {
            result += segment.stringValue;
        } else {
            // Evaluate the expression
            auto exprResult = Evaluate(*segment.expression);
            if (isError(exprResult)) {
                lastResult = exprResult;
                return;
            }
            // Convert result to string and append
            result += ValueToString(unwrap(exprResult));
        }
    }
    
    lastResult = result;
}

void Interpreter::visitNumberLiteral(const ast::NumberLiteral& node) { lastResult = node.value; }
void Interpreter::visitHotkeyLiteral(const ast::HotkeyLiteral& node) { lastResult = node.combination; }
void Interpreter::visitIdentifier(const ast::Identifier& node) {
    if (auto val = environment->Get(node.symbol)) {
        lastResult = *val;
    } else {
        lastResult = HavelRuntimeError("Undefined variable: " + node.symbol);
    }
}

void Interpreter::visitArrayLiteral(const ast::ArrayLiteral& node) {
    auto array = std::make_shared<std::vector<HavelValue>>();
    
    for (const auto& element : node.elements) {
        auto result = Evaluate(*element);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        array->push_back(unwrap(result));
    }
    
    lastResult = HavelValue(array);
}

void Interpreter::visitObjectLiteral(const ast::ObjectLiteral& node) {
    auto object = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    for (const auto& [key, valueExpr] : node.pairs) {
        auto result = Evaluate(*valueExpr);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        (*object)[key] = unwrap(result);
    }
    
    lastResult = HavelValue(object);
}

void Interpreter::visitConfigBlock(const ast::ConfigBlock& node) {
    auto configObject = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    auto& config = Configs::Get();
    
    // Special handling for "file" key - if present, load that config file
    for (const auto& [key, valueExpr] : node.pairs) {
        if (key == "file") {
            auto result = Evaluate(*valueExpr);
            if (isError(result)) {
                lastResult = result;
                return;
            }
            std::string filePath = ValueToString(unwrap(result));
            
            // Expand ~ to home directory
            if (!filePath.empty() && filePath[0] == '~') {
                const char* home = std::getenv("HOME");
                if (home) {
                    filePath = std::string(home) + filePath.substr(1);
                }
            }
            
            config.Load(filePath);
        }
    }
    
    // Process all config key-value pairs
    for (const auto& [key, valueExpr] : node.pairs) {
        auto result = Evaluate(*valueExpr);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        
        HavelValue value = unwrap(result);
(*configObject)[key] = value;
        
        // Write to actual Configs if not "file" or "defaults"
        if (key != "file" && key != "defaults") {
            // Use "Havel." prefix for config keys from the language
            std::string configKey = "Havel." + key;
            
            // Convert HavelValue to string for Configs
            std::string strValue = ValueToString(value);
            
            // Handle different value types appropriately
            if (std::holds_alternative<bool>(value)) {
                config.Set(configKey, std::get<bool>(value) ? "true" : "false");
            } else if (std::holds_alternative<int>(value)) {
                config.Set(configKey, std::get<int>(value));
            } else if (std::holds_alternative<double>(value)) {
                config.Set(configKey, std::get<double>(value));
            } else {
                config.Set(configKey, strValue);
            }
        }
        
        // Handle defaults object
        if (key == "defaults" && std::holds_alternative<HavelObject>(value)) {
auto& defaults = std::get<HavelObject>(value);
            if (defaults) for (const auto& [defaultKey, defaultValue] : *defaults) {
                std::string configKey = "Havel." + defaultKey;
                std::string strValue = ValueToString(defaultValue);
                
                // Only set if not already set
                if (config.Get<std::string>(configKey, "").empty()) {
                    config.Set(configKey, strValue);
                }
            }
        }
    }
    
    // Save config to file
    config.Save();
    
    // Store the config block as a special variable for script access
    environment->Define("__config__", HavelValue(configObject));
    
    lastResult = nullptr; // Config blocks don't return a value
}

void Interpreter::visitDevicesBlock(const ast::DevicesBlock& node) {
    auto devicesObject = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    auto& config = Configs::Get();
    
    // Device configuration mappings
    std::unordered_map<std::string, std::string> deviceKeyMap = {
        {"keyboard", "Device.Keyboard"},
        {"mouse", "Device.Mouse"},
        {"joystick", "Device.Joystick"},
        {"mouseSensitivity", "Mouse.Sensitivity"},
        {"ignoreMouse", "Device.IgnoreMouse"}
    };
    
    for (const auto& [key, valueExpr] : node.pairs) {
        auto result = Evaluate(*valueExpr);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        
        HavelValue value = unwrap(result);
(*devicesObject)[key] = value;
        
        // Map to config keys and write to Configs
        auto it = deviceKeyMap.find(key);
        if (it != deviceKeyMap.end()) {
            std::string configKey = it->second;
            
            // Convert value to appropriate type
            if (std::holds_alternative<bool>(value)) {
                config.Set(configKey, std::get<bool>(value) ? "true" : "false");
            } else if (std::holds_alternative<int>(value)) {
                config.Set(configKey, std::get<int>(value));
            } else if (std::holds_alternative<double>(value)) {
                config.Set(configKey, std::get<double>(value));
            } else {
                config.Set(configKey, ValueToString(value));
            }
        } else {
            // Unknown device config key, store with Device prefix
            config.Set("Device." + key, ValueToString(value));
        }
    }
    
    // Save config
    config.Save();
    
    // Store the devices block as a special variable for script access
    environment->Define("__devices__", HavelValue(devicesObject));
    
    lastResult = nullptr; // Devices blocks don't return a value
}

void Interpreter::visitModesBlock(const ast::ModesBlock& node) {
    auto modesObject = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Process mode definitions
    for (const auto& [modeName, valueExpr] : node.pairs) {
        auto result = Evaluate(*valueExpr);
        if (isError(result)) {
            lastResult = result;
            return;
        }
        
        HavelValue value = unwrap(result);
(*modesObject)[modeName] = value;
        
        // If value is an object with class/title/ignore arrays, register with condition system
if (std::holds_alternative<HavelObject>(value)) {
            auto& modeConfig = std::get<HavelObject>(value);
            
            // Store mode configuration for condition checking
            // The mode config will be checked later when evaluating conditions
            // Format: modes.gaming.class = ["steam", "lutris", ...]
            if (modeConfig) for (const auto& [configKey, configValue] : *modeConfig) {
                std::string fullKey = "__mode_" + modeName + "_" + configKey;
                environment->Define(fullKey, configValue);
            }
        }
    }
    
    // Initialize current mode (default to first mode or "default")
    if (modesObject && !modesObject->empty()) {
        std::string initialMode = modesObject->begin()->first;
        environment->Define("__current_mode__", HavelValue(initialMode));
        environment->Define("__previous_mode__", HavelValue(std::string("default")));
    } else {
        environment->Define("__current_mode__", HavelValue(std::string("default")));
        environment->Define("__previous_mode__", HavelValue(std::string("default")));
    }
    
    // Store the modes block as a special variable for script access
    environment->Define("__modes__", HavelValue(modesObject));
    
    lastResult = nullptr; // Modes blocks don't return a value
}

void Interpreter::visitIndexExpression(const ast::IndexExpression& node) {
    auto objectResult = Evaluate(*node.object);
    if (isError(objectResult)) {
        lastResult = objectResult;
        return;
    }
    
    auto indexResult = Evaluate(*node.index);
    if (isError(indexResult)) {
        lastResult = indexResult;
        return;
    }
    
    HavelValue objectValue = unwrap(objectResult);
    HavelValue indexValue = unwrap(indexResult);
    
    // Handle array indexing
    if (auto* arrayPtr = std::get_if<HavelArray>(&objectValue)) {
        // Convert index to integer
        int index = static_cast<int>(ValueToNumber(indexValue));
        
        if (!*arrayPtr || index < 0 || index >= static_cast<int>((*arrayPtr)->size())) {
            lastResult = HavelRuntimeError("Array index out of bounds: " + std::to_string(index));
            return;
        }
        
        lastResult = (**arrayPtr)[index];
        return;
    }
    
    // Handle object property access
    if (auto* objectPtr = std::get_if<HavelObject>(&objectValue)) {
        std::string key = ValueToString(indexValue);
        
        if (*objectPtr) {
            auto it = (*objectPtr)->find(key);
            if (it != (*objectPtr)->end()) {
                lastResult = it->second;
            } else {
                lastResult = nullptr; // Return null for missing properties
            }
        } else {
            lastResult = nullptr;
        }
        return;
    }
    
    lastResult = HavelRuntimeError("Cannot index non-array/non-object value");
}

void Interpreter::visitTernaryExpression(const ast::TernaryExpression& node) {
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
        lastResult = conditionResult;
        return;
    }
    
    if (ValueToBool(unwrap(conditionResult))) {
        lastResult = Evaluate(*node.trueValue);
    } else {
        lastResult = Evaluate(*node.falseValue);
    }
}

void Interpreter::visitWhileStatement(const ast::WhileStatement& node) {
    // Evaluate condition and loop while true
    while (true) {
        auto conditionResult = Evaluate(*node.condition);
        if (isError(conditionResult)) {
            lastResult = conditionResult;
            return;
        }
        
        if (!ValueToBool(unwrap(conditionResult))) {
            break; // Exit loop when condition is false
        }
        
        // Execute loop body
        auto bodyResult = Evaluate(*node.body);
        
        // Handle errors and return statements
        if (isError(bodyResult)) {
            lastResult = bodyResult;
            return;
        }
        
        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            lastResult = bodyResult;
            return;
        }
        
        // Handle break
        if (std::holds_alternative<BreakValue>(bodyResult)) {
            break;
        }
        
        // Handle continue
        if (std::holds_alternative<ContinueValue>(bodyResult)) {
            continue;
        }
    }
    
    lastResult = nullptr;
}
void Interpreter::visitRangeExpression(const ast::RangeExpression& node) {
    auto startResult = Evaluate(*node.start);
    if (isError(startResult)) {
        lastResult = startResult;
        return;
    }
    
    auto endResult = Evaluate(*node.end);
    if (isError(endResult)) {
        lastResult = endResult;
        return;
    }
    
    int start = static_cast<int>(ValueToNumber(unwrap(startResult)));
    int end = static_cast<int>(ValueToNumber(unwrap(endResult)));
    
    // Create an array from start to end (inclusive)
    auto rangeArray = std::make_shared<std::vector<HavelValue>>();
    for (int i = start; i <= end; ++i) {
        rangeArray->push_back(HavelValue(i));
    }
    
    lastResult = rangeArray;
}

void Interpreter::visitAssignmentExpression(const ast::AssignmentExpression& node) {
    // Evaluate the right-hand side
    auto valueResult = Evaluate(*node.value);
    if (isError(valueResult)) {
        lastResult = valueResult;
        return;
    }
    HavelValue value = unwrap(valueResult);
    
    auto applyCompound = [](const std::string& op, const HavelValue& lhs, const HavelValue& rhs) -> HavelValue {
        if (op == "=") return rhs;
        if (op == "+=") return HavelValue(ValueToNumber(lhs) + ValueToNumber(rhs));
        if (op == "-") return HavelValue(ValueToNumber(lhs) - ValueToNumber(rhs)); // not used
        if (op == "-=") return HavelValue(ValueToNumber(lhs) - ValueToNumber(rhs));
        if (op == "*=") return HavelValue(ValueToNumber(lhs) * ValueToNumber(rhs));
        if (op == "/=") {
            double denom = ValueToNumber(rhs);
            if (denom == 0.0) throw HavelRuntimeError("Division by zero");
            return HavelValue(ValueToNumber(lhs) / denom);
        }
        return rhs; // fallback
    };

    const std::string& op = node.operator_;

    // Determine what we're assigning to
    if (auto* identifier = dynamic_cast<const ast::Identifier*>(node.target.get())) {
        // Simple variable assignment (may be compound)
        auto current = environment->Get(identifier->symbol);
        if (!current.has_value()) {
            lastResult = HavelRuntimeError("Undefined variable: " + identifier->symbol);
            return;
        }
        HavelValue newValue = applyCompound(op, *current, value);
        if (!environment->Assign(identifier->symbol, newValue)) {
            lastResult = HavelRuntimeError("Undefined variable: " + identifier->symbol);
            return;
        }
        value = newValue;
    } 
else if (auto* index = dynamic_cast<const ast::IndexExpression*>(node.target.get())) {
        // Array/object index assignment (array[0] = value)
        auto objectResult = Evaluate(*index->object);
        if (isError(objectResult)) {
            lastResult = objectResult;
            return;
        }
        
        auto indexResult = Evaluate(*index->index);
        if (isError(indexResult)) {
            lastResult = indexResult;
            return;
        }
        
        HavelValue objectValue = unwrap(objectResult);
        HavelValue indexValue = unwrap(indexResult);
        
        if (auto* arrayPtr = std::get_if<HavelArray>(&objectValue)) {
            int idx = static_cast<int>(ValueToNumber(indexValue));
            if (!*arrayPtr || idx < 0 || idx >= static_cast<int>((*arrayPtr)->size())) {
                lastResult = HavelRuntimeError("Array index out of bounds");
                return;
            }
            // Apply compound operator to existing value
            HavelValue newValue = applyCompound(op, (**arrayPtr)[idx], value);
            (**arrayPtr)[idx] = newValue;
            value = newValue;
        } else if (auto* objectPtr = std::get_if<HavelObject>(&objectValue)) {
            std::string key = ValueToString(indexValue);
            if (!*objectPtr) {
                *objectPtr = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            }
            // If property exists, apply compound operator; otherwise treat as simple assignment
            auto it = (**objectPtr).find(key);
            if (it != (**objectPtr).end()) {
                HavelValue newValue = applyCompound(op, it->second, value);
                it->second = newValue;
                value = newValue;
            } else {
                (**objectPtr)[key] = value;
            }
        } else {
            lastResult = HavelRuntimeError("Cannot index non-array/non-object value");
            return;
        }
    }
    else {
        lastResult = HavelRuntimeError("Invalid assignment target");
        return;
    }
    
    lastResult = value;  // Assignment expressions return the assigned value
}

void Interpreter::visitForStatement(const ast::ForStatement& node) {
    // Evaluate the iterable
    auto iterableResult = Evaluate(*node.iterable);
    if (isError(iterableResult)) {
        lastResult = iterableResult;
        return;
    }
    
    HavelValue iterableValue = unwrap(iterableResult);
    
    // Check if iterable is an array
    if (auto* array = std::get_if<HavelArray>(&iterableValue)) {
        // Iterate over each element
        if (*array) for (const auto& element : **array) {
            // Define iterator variable in current scope
            environment->Define(node.iterator->symbol, element);
            
            // Execute loop body
            auto bodyResult = Evaluate(*node.body);
            
            // Handle errors and return statements
            if (isError(bodyResult)) {
                lastResult = bodyResult;
                return;
            }
            
            if (std::holds_alternative<ReturnValue>(bodyResult)) {
                lastResult = bodyResult;
                return;
            }
            
            // Handle break
            if (std::holds_alternative<BreakValue>(bodyResult)) {
                break;
            }
            
            // Handle continue
            if (std::holds_alternative<ContinueValue>(bodyResult)) {
                continue;
            }
        }
        
        lastResult = nullptr;
        return;
    }
    
    lastResult = HavelRuntimeError("for-in loop requires an iterable (array)");
}

void Interpreter::visitLoopStatement(const ast::LoopStatement& node) {
    // Infinite loop
    while (true) {
        // Execute loop body
        auto bodyResult = Evaluate(*node.body);
        
        // Handle errors and return statements
        if (isError(bodyResult)) {
            lastResult = bodyResult;
            return;
        }
        
        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            lastResult = bodyResult;
            return;
        }
        
        // Handle break
        if (std::holds_alternative<BreakValue>(bodyResult)) {
            break;
        }
        
        // Handle continue
        if (std::holds_alternative<ContinueValue>(bodyResult)) {
            continue;
        }
    }
    
    lastResult = nullptr;
}

void Interpreter::visitBreakStatement(const ast::BreakStatement& node) {
    lastResult = BreakValue{};
}

void Interpreter::visitContinueStatement(const ast::ContinueStatement& node) {
    lastResult = ContinueValue{};
}

void Interpreter::visitOnModeStatement(const ast::OnModeStatement& node) {
    // Get current mode
    auto currentModeOpt = environment->Get("__current_mode__");
    std::string currentMode = "default";
    
    if (currentModeOpt && std::holds_alternative<std::string>(*currentModeOpt)) {
        currentMode = std::get<std::string>(*currentModeOpt);
    }
    
    // Check if we're entering the specified mode
    if (currentMode == node.modeName) {
        // Execute the on-mode body
        lastResult = Evaluate(*node.body);
    } else if (node.alternative) {
        // Execute the else block if provided
        lastResult = Evaluate(*node.alternative);
    } else {
        lastResult = nullptr;
    }
}

void Interpreter::visitOffModeStatement(const ast::OffModeStatement& node) {
    // Get previous mode (we'll track this when mode changes)
    auto prevModeOpt = environment->Get("__previous_mode__");
    auto currentModeOpt = environment->Get("__current_mode__");
    
    std::string previousMode = "default";
    std::string currentMode = "default";
    
    if (prevModeOpt && std::holds_alternative<std::string>(*prevModeOpt)) {
        previousMode = std::get<std::string>(*prevModeOpt);
    }
    if (currentModeOpt && std::holds_alternative<std::string>(*currentModeOpt)) {
        currentMode = std::get<std::string>(*currentModeOpt);
    }
    
    // Check if we're leaving the specified mode
    if (previousMode == node.modeName && currentMode != node.modeName) {
        // Execute the off-mode body
        lastResult = Evaluate(*node.body);
    } else {
        lastResult = nullptr;
    }
}

// Stubs for unimplemented visit methods
void Interpreter::visitTypeDeclaration(const ast::TypeDeclaration& node) { lastResult = HavelRuntimeError("Type declarations not implemented."); }
void Interpreter::visitTypeAnnotation(const ast::TypeAnnotation& node) { lastResult = HavelRuntimeError("Type annotations not implemented."); }
void Interpreter::visitUnionType(const ast::UnionType& node) { lastResult = HavelRuntimeError("Union types not implemented."); }
void Interpreter::visitRecordType(const ast::RecordType& node) { lastResult = HavelRuntimeError("Record types not implemented."); }
void Interpreter::visitFunctionType(const ast::FunctionType& node) { lastResult = HavelRuntimeError("Function types not implemented."); }
void Interpreter::visitTypeReference(const ast::TypeReference& node) { lastResult = HavelRuntimeError("Type references not implemented."); }
void Interpreter::visitTryExpression(const ast::TryExpression& node) { lastResult = HavelRuntimeError("Try expressions not implemented."); }

void Interpreter::InitializeStandardLibrary() {
    InitializeSystemBuiltins();
    InitializeWindowBuiltins();
    InitializeClipboardBuiltins();
    InitializeTextBuiltins();
    InitializeFileBuiltins();
    InitializeArrayBuiltins();
    InitializeIOBuiltins();
    InitializeBrightnessBuiltins();
    InitializeDebugBuiltins();
    InitializeAudioBuiltins();
    InitializeMediaBuiltins();
    InitializeLauncherBuiltins();
    InitializeGUIBuiltins();
}
void Interpreter::InitializeSystemBuiltins() {
    // Define boolean constants
    environment->Define("true", HavelValue(true));
    environment->Define("false", HavelValue(false));
    environment->Define("null", HavelValue(nullptr));
    
    environment->Define("print", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        for(const auto& arg : args) {
            std::cout << this->ValueToString(arg) << " ";
        }
        std::cout << std::endl;
        std::cout.flush();
        return HavelValue(nullptr);
    }));
    
    // repeat(n, fn)
    environment->Define("repeat", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("repeat() requires (count, function)");
        int count = static_cast<int>(ValueToNumber(args[0]));
        const HavelValue& fn = args[1];
        for (int i = 0; i < count; ++i) {
            std::vector<HavelValue> fnArgs = { HavelValue(static_cast<double>(i)) };
            HavelResult res;
            if (auto* builtin = std::get_if<BuiltinFunction>(&fn)) {
                res = (*builtin)(fnArgs);
            } else if (auto* userFunc = std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
                auto& func = *userFunc;
                auto funcEnv = std::make_shared<Environment>(func->closure);
                for (size_t p = 0; p < func->declaration->parameters.size() && p < fnArgs.size(); ++p) {
                    funcEnv->Define(func->declaration->parameters[p]->symbol, fnArgs[p]);
                }
                auto originalEnv = this->environment;
                this->environment = funcEnv;
                res = Evaluate(*func->declaration->body);
                this->environment = originalEnv;
                if (isError(res)) return res;
            } else {
                return HavelRuntimeError("repeat() requires callable function");
            }
            if (isError(res)) return res;
        }
        return HavelValue(nullptr);
    }));
    
    environment->Define("log", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[LOG] ";
        for(const auto& arg : args) {
            std::cout << this->ValueToString(arg) << " ";
        }
        std::cout << std::endl;
        std::cout.flush();
        return HavelValue(nullptr);
    }));
    
    environment->Define("warn", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cerr << "[WARN] ";
        for(const auto& arg : args) {
            std::cerr << this->ValueToString(arg) << " ";
        }
        std::cerr << std::endl;
        std::cerr.flush();
        return HavelValue(nullptr);
    }));
    
    environment->Define("error", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cerr << "[ERROR] ";
        for(const auto& arg : args) {
            std::cerr << this->ValueToString(arg) << " ";
        }
        std::cerr << std::endl;
        std::cerr.flush();
        return HavelValue(nullptr);
    }));
    
    environment->Define("fatal", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cerr << "[FATAL] ";
        for(const auto& arg : args) {
            std::cerr << this->ValueToString(arg) << " ";
        }
        std::cerr << std::endl;
        std::cerr.flush();
        std::exit(1);
        return HavelValue(nullptr);
    }));
    
    environment->Define("sleep", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("sleep() requires milliseconds");
        double ms = ValueToNumber(args[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds((int)ms));
        return HavelValue(nullptr);
    }));
    
    environment->Define("exit", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        int code = args.empty() ? 0 : (int)ValueToNumber(args[0]);
        std::exit(code);
        return HavelValue(nullptr);
    }));
    environment->Define("type", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("type() requires an argument");
        return std::visit([](auto&& arg) -> HavelValue {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) return HavelValue("null");
            else if constexpr (std::is_same_v<T, bool>) return HavelValue("boolean");
            else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>) return HavelValue("number");
            else if constexpr (std::is_same_v<T, std::string>) return HavelValue("string");
            else if constexpr (std::is_same_v<T, HavelArray>) return HavelValue("array");
            else if constexpr (std::is_same_v<T, HavelObject>) return HavelValue("object");
            else if constexpr (std::is_same_v<T, std::shared_ptr<HavelFunction>>) return HavelValue("function");
            else if constexpr (std::is_same_v<T, BuiltinFunction>) return HavelValue("builtin");
            else return HavelValue("unknown");
        }, args[0]);
    }));

    // Send text/keys to the system
    environment->Define("send", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("send() requires text");
        std::string text = this->ValueToString(args[0]);
        this->io.Send(text.c_str());
        return HavelValue(nullptr);
    }));
    
    // === IO METHODS ===
    // Mouse methods
    environment->Define("io.mouseMove", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("io.mouseMove() requires (dx, dy)");
        int dx = static_cast<int>(std::get<double>(args[0]));
        int dy = static_cast<int>(std::get<double>(args[1]));
        this->io.MouseMove(dx, dy);
        return HavelValue(nullptr);
    }));
    
    environment->Define("io.mouseMoveTo", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("io.mouseMoveTo() requires (x, y)");
        int x = static_cast<int>(std::get<double>(args[0]));
        int y = static_cast<int>(std::get<double>(args[1]));
        this->io.MouseMoveTo(x, y);
        return HavelValue(nullptr);
    }));
    
    environment->Define("io.mouseClick", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(std::get<double>(args[0]));
        this->io.MouseClick(button);
        return HavelValue(nullptr);
    }));
    
    environment->Define("io.mouseDown", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(std::get<double>(args[0]));
        this->io.MouseDown(button);
        return HavelValue(nullptr);
    }));
    
    environment->Define("io.mouseUp", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(std::get<double>(args[0]));
        this->io.MouseUp(button);
        return HavelValue(nullptr);
    }));
    
    environment->Define("io.mouseWheel", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        int amount = args.empty() ? 1 : static_cast<int>(std::get<double>(args[0]));
        this->io.MouseWheel(amount);
        return HavelValue(nullptr);
    }));
    
    // Key state methods
    environment->Define("io.getKeyState", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("io.getKeyState() requires key name");
        std::string key = this->ValueToString(args[0]);
        return HavelValue(this->io.GetKeyState(key));
    }));
    
    environment->Define("io.isShiftPressed", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->io.IsShiftPressed());
    }));
    
    environment->Define("io.isCtrlPressed", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->io.IsCtrlPressed());
    }));
    
    environment->Define("io.isAltPressed", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->io.IsAltPressed());
    }));
    
    environment->Define("io.isWinPressed", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->io.IsWinPressed());
    }));
    
    // === AUDIO MANAGER METHODS ===
    // Volume control
    environment->Define("audio.setVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.setVolume() requires volume (0.0-1.0)");
        double volume = std::get<double>(args[0]);
        return HavelValue(this->audioManager.setVolume(volume));
    }));
    
    environment->Define("audio.getVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->audioManager.getVolume());
    }));
    
    environment->Define("audio.increaseVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
        return HavelValue(this->audioManager.increaseVolume(amount));
    }));
    
    environment->Define("audio.decreaseVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
        return HavelValue(this->audioManager.decreaseVolume(amount));
    }));
    
    // Mute control
    environment->Define("audio.toggleMute", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->audioManager.toggleMute());
    }));
    
    environment->Define("audio.setMute", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.setMute() requires boolean");
        bool muted = std::get<bool>(args[0]);
        return HavelValue(this->audioManager.setMute(muted));
    }));
    
    environment->Define("audio.isMuted", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->audioManager.isMuted());
    }));
    
    // Application volume control
    environment->Define("audio.setAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("audio.setAppVolume() requires (appName, volume)");
        std::string appName = this->ValueToString(args[0]);
        double volume = std::get<double>(args[1]);
        return HavelValue(this->audioManager.setApplicationVolume(appName, volume));
    }));
    
    environment->Define("audio.getAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.getAppVolume() requires appName");
        std::string appName = this->ValueToString(args[0]);
        return HavelValue(this->audioManager.getApplicationVolume(appName));
    }));
    
    environment->Define("audio.increaseAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.increaseAppVolume() requires appName");
        std::string appName = this->ValueToString(args[0]);
        double amount = args.size() > 1 ? std::get<double>(args[1]) : 0.05;
        return HavelValue(this->audioManager.increaseApplicationVolume(appName, amount));
    }));
    
    environment->Define("audio.decreaseAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.decreaseAppVolume() requires appName");
        std::string appName = this->ValueToString(args[0]);
        double amount = args.size() > 1 ? std::get<double>(args[1]) : 0.05;
        return HavelValue(this->audioManager.decreaseApplicationVolume(appName, amount));
    }));
    
    // Active window application volume
    environment->Define("audio.setActiveAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("audio.setActiveAppVolume() requires volume");
        double volume = std::get<double>(args[0]);
        return HavelValue(this->audioManager.setActiveApplicationVolume(volume));
    }));
    
    environment->Define("audio.getActiveAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        return HavelValue(this->audioManager.getActiveApplicationVolume());
    }));
    
    environment->Define("audio.increaseActiveAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
        return HavelValue(this->audioManager.increaseActiveApplicationVolume(amount));
    }));
    
    environment->Define("audio.decreaseActiveAppVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
        return HavelValue(this->audioManager.decreaseActiveApplicationVolume(amount));
    }));
    
    // Get applications list
    environment->Define("audio.getApplications", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        auto apps = this->audioManager.getApplications();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& app : apps) {
            auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["name"] = HavelValue(app.name);
            (*obj)["volume"] = HavelValue(app.volume);
            (*obj)["isMuted"] = HavelValue(app.isMuted);
            (*obj)["index"] = HavelValue(static_cast<double>(app.index));
            arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
    }));
    
    // Expose as module objects
    auto clip = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    if (auto v = environment->Get("clipboard.get")) (*clip)["get"] = *v;
    if (auto v = environment->Get("clipboard.set")) (*clip)["set"] = *v;
    if (auto v = environment->Get("clipboard.clear")) (*clip)["clear"] = *v;
    environment->Define("clipboard", HavelValue(clip));
    
    // Create io module
    auto ioMod = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    if (auto v = environment->Get("io.mouseMove")) (*ioMod)["mouseMove"] = *v;
    if (auto v = environment->Get("io.mouseMoveTo")) (*ioMod)["mouseMoveTo"] = *v;
    if (auto v = environment->Get("io.mouseClick")) (*ioMod)["mouseClick"] = *v;
    if (auto v = environment->Get("io.mouseDown")) (*ioMod)["mouseDown"] = *v;
    if (auto v = environment->Get("io.mouseUp")) (*ioMod)["mouseUp"] = *v;
    if (auto v = environment->Get("io.mouseWheel")) (*ioMod)["mouseWheel"] = *v;
    if (auto v = environment->Get("io.getKeyState")) (*ioMod)["getKeyState"] = *v;
    if (auto v = environment->Get("io.isShiftPressed")) (*ioMod)["isShiftPressed"] = *v;
    if (auto v = environment->Get("io.isCtrlPressed")) (*ioMod)["isCtrlPressed"] = *v;
    if (auto v = environment->Get("io.isAltPressed")) (*ioMod)["isAltPressed"] = *v;
    if (auto v = environment->Get("io.isWinPressed")) (*ioMod)["isWinPressed"] = *v;
    environment->Define("io", HavelValue(ioMod));
    
    // Create audio module
    auto audioMod = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    if (auto v = environment->Get("audio.setVolume")) (*audioMod)["setVolume"] = *v;
    if (auto v = environment->Get("audio.getVolume")) (*audioMod)["getVolume"] = *v;
    if (auto v = environment->Get("audio.increaseVolume")) (*audioMod)["increaseVolume"] = *v;
    if (auto v = environment->Get("audio.decreaseVolume")) (*audioMod)["decreaseVolume"] = *v;
    if (auto v = environment->Get("audio.toggleMute")) (*audioMod)["toggleMute"] = *v;
    if (auto v = environment->Get("audio.setMute")) (*audioMod)["setMute"] = *v;
    if (auto v = environment->Get("audio.isMuted")) (*audioMod)["isMuted"] = *v;
    if (auto v = environment->Get("audio.setAppVolume")) (*audioMod)["setAppVolume"] = *v;
    if (auto v = environment->Get("audio.getAppVolume")) (*audioMod)["getAppVolume"] = *v;
    if (auto v = environment->Get("audio.increaseAppVolume")) (*audioMod)["increaseAppVolume"] = *v;
    if (auto v = environment->Get("audio.decreaseAppVolume")) (*audioMod)["decreaseAppVolume"] = *v;
    if (auto v = environment->Get("audio.setActiveAppVolume")) (*audioMod)["setActiveAppVolume"] = *v;
    if (auto v = environment->Get("audio.getActiveAppVolume")) (*audioMod)["getActiveAppVolume"] = *v;
    if (auto v = environment->Get("audio.increaseActiveAppVolume")) (*audioMod)["increaseActiveAppVolume"] = *v;
    if (auto v = environment->Get("audio.decreaseActiveAppVolume")) (*audioMod)["decreaseActiveAppVolume"] = *v;
    if (auto v = environment->Get("audio.getApplications")) (*audioMod)["getApplications"] = *v;
    environment->Define("audio", HavelValue(audioMod));
}

void Interpreter::InitializeWindowBuiltins() {
    environment->Define("window.getTitle", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        havel::Window activeWin = havel::Window(this->windowManager.GetActiveWindow());
        if (activeWin.Exists()) {
            return HavelValue(activeWin.Title());
        }
        return HavelValue(std::string(""));
    }));
    
    environment->Define("window.maximize", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        havel::Window activeWin = havel::Window(this->windowManager.GetActiveWindow());
        activeWin.Max();
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.minimize", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        havel::Window activeWin = havel::Window(this->windowManager.GetActiveWindow());
        activeWin.Min();
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.next", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        this->windowManager.AltTab();
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.previous", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        this->windowManager.AltTab();
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.close", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        Window w(this->windowManager.GetActiveWindow());
        w.Close();
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.center", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        this->windowManager.Center(this->windowManager.GetActiveWindow());
        return HavelValue(nullptr);
    }));
    
    environment->Define("window.focus", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("window.focus() requires window title");
        std::string title = ValueToString(args[0]);
        wID winId = havel::WindowManager::FindByTitle(title.c_str());
        if (winId != 0) {
            havel::Window window("", winId);
            window.Activate(winId);
            return HavelValue(true);
        }
        return HavelValue(false);
    }));

    // Expose as module object: window
    auto win = std::make_shared<std::unordered_map<std::string, HavelValue>>();
if (auto v = environment->Get("window.getTitle")) (*win)["getTitle"] = *v;
    if (auto v = environment->Get("window.maximize")) (*win)["maximize"] = *v;
    if (auto v = environment->Get("window.minimize")) (*win)["minimize"] = *v;
    if (auto v = environment->Get("window.next")) (*win)["next"] = *v;
    if (auto v = environment->Get("window.previous")) (*win)["previous"] = *v;
    if (auto v = environment->Get("window.close")) (*win)["close"] = *v;
    if (auto v = environment->Get("window.center")) (*win)["center"] = *v;
    if (auto v = environment->Get("window.focus")) (*win)["focus"] = *v;
    environment->Define("window", HavelValue(win));
}

void Interpreter::InitializeClipboardBuiltins() {
    environment->Define("clipboard.get", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
    }));
    
    environment->Define("clipboard.set", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("clipboard.set() requires text");
        std::string text = this->ValueToString(args[0]);
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(QString::fromStdString(text));
        return HavelValue(true);
    }));
    
    environment->Define("clipboard.clear", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->clear();
        return HavelValue(nullptr);
    }));
}

void Interpreter::InitializeTextBuiltins() {
    environment->Define("upper", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("upper() requires text");
        std::string text = this->ValueToString(args[0]);
        std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        return HavelValue(text);
    }));
    
    environment->Define("lower", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("lower() requires text");
        std::string text = this->ValueToString(args[0]);
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        return HavelValue(text);
    }));
    
    environment->Define("trim", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("trim() requires text");
        std::string text = this->ValueToString(args[0]);
        // Trim whitespace
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), text.end());
        return HavelValue(text);
    }));
    
    environment->Define("length", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("length() requires text");
        std::string text = this->ValueToString(args[0]);
        return HavelValue((double)text.length());
    }));
    
    environment->Define("replace", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 3) return HavelRuntimeError("replace() requires (text, search, replacement)");
        std::string text = this->ValueToString(args[0]);
        std::string search = this->ValueToString(args[1]);
        std::string replacement = this->ValueToString(args[2]);
        
        size_t pos = 0;
        while ((pos = text.find(search, pos)) != std::string::npos) {
            text.replace(pos, search.length(), replacement);
            pos += replacement.length();
        }
        return HavelValue(text);
    }));
    
    environment->Define("contains", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("contains() requires (text, search)");
        std::string text = this->ValueToString(args[0]);
        std::string search = this->ValueToString(args[1]);
        return HavelValue(text.find(search) != std::string::npos);
    }));
}

void Interpreter::InitializeFileBuiltins() {
    environment->Define("file.read", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("file.read() requires path");
        std::string path = this->ValueToString(args[0]);
        std::ifstream file(path);
        if (!file.is_open()) return HavelRuntimeError("Cannot open file: " + path);
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return HavelValue(content);
    }));
    
    environment->Define("file.write", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("file.write() requires (path, content)");
        std::string path = this->ValueToString(args[0]);
        std::string content = this->ValueToString(args[1]);
        
        std::ofstream file(path);
        if (!file.is_open()) return HavelRuntimeError("Cannot write to file: " + path);
        
        file << content;
        return HavelValue(true);
    }));
    
    environment->Define("file.exists", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("file.exists() requires path");
        std::string path = this->ValueToString(args[0]);
        return HavelValue(std::filesystem::exists(path));
    }));
}

void Interpreter::InitializeArrayBuiltins() {
    // Array map
    environment->Define("map", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("map() requires (array, function)");
        if (!std::holds_alternative<HavelArray>(args[0])) return HavelRuntimeError("map() first arg must be array");
        
        auto array = std::get<HavelArray>(args[0]);
        auto& fn = args[1];
        
        auto result = std::make_shared<std::vector<HavelValue>>();
        if (array) {
            for (const auto& item : *array) {
                // Call function with item
                std::vector<HavelValue> fnArgs = {item};
                HavelResult res;
                
                if (auto* builtin = std::get_if<BuiltinFunction>(&fn)) {
                    res = (*builtin)(fnArgs);
                } else if (auto* userFunc = std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
                    auto& func = *userFunc;
                    if (fnArgs.size() != func->declaration->parameters.size()) {
                        return HavelRuntimeError("Function parameter count mismatch");
                    }
                    
                    auto funcEnv = std::make_shared<Environment>(func->closure);
                    for (size_t i = 0; i < fnArgs.size(); ++i) {
                        funcEnv->Define(func->declaration->parameters[i]->symbol, fnArgs[i]);
                    }
                    
                    auto originalEnv = this->environment;
                    this->environment = funcEnv;
                    res = Evaluate(*func->declaration->body);
                    this->environment = originalEnv;
                    
                    if (std::holds_alternative<ReturnValue>(res)) {
                        result->push_back(std::get<ReturnValue>(res).value);
                    } else if (!isError(res)) {
                        result->push_back(unwrap(res));
                    } else {
                        return res;
                    }
                    continue;
                } else {
                    return HavelRuntimeError("map() requires callable function");
                }
                
                if (isError(res)) return res;
                result->push_back(unwrap(res));
            }
        }
        return HavelValue(result);
    }));
    
    // Array filter
    environment->Define("filter", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("filter() requires (array, predicate)");
        if (!std::holds_alternative<HavelArray>(args[0])) return HavelRuntimeError("filter() first arg must be array");
        
        auto array = std::get<HavelArray>(args[0]);
        auto& fn = args[1];
        
        auto result = std::make_shared<std::vector<HavelValue>>();
        if (array) {
            for (const auto& item : *array) {
                std::vector<HavelValue> fnArgs = {item};
                HavelResult res;
                
                if (auto* builtin = std::get_if<BuiltinFunction>(&fn)) {
                    res = (*builtin)(fnArgs);
                } else if (auto* userFunc = std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
                    auto& func = *userFunc;
                    auto funcEnv = std::make_shared<Environment>(func->closure);
                    for (size_t i = 0; i < fnArgs.size(); ++i) {
                        funcEnv->Define(func->declaration->parameters[i]->symbol, fnArgs[i]);
                    }
                    
                    auto originalEnv = this->environment;
                    this->environment = funcEnv;
                    res = Evaluate(*func->declaration->body);
                    this->environment = originalEnv;
                    
                    if (std::holds_alternative<ReturnValue>(res)) {
                        if (ValueToBool(std::get<ReturnValue>(res).value)) {
                            result->push_back(item);
                        }
                    } else if (!isError(res) && ValueToBool(unwrap(res))) {
                        result->push_back(item);
                    } else if (isError(res)) {
                        return res;
                    }
                    continue;
                } else {
                    return HavelRuntimeError("filter() requires callable function");
                }
                
                if (isError(res)) return res;
                if (ValueToBool(unwrap(res))) {
                    result->push_back(item);
                }
            }
        }
        return HavelValue(result);
    }));
    
    // Array push
    environment->Define("push", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) return HavelRuntimeError("push() requires (array, value)");
        if (!std::holds_alternative<HavelArray>(args[0])) return HavelRuntimeError("push() first arg must be array");
        
        auto array = std::get<HavelArray>(args[0]);
        if (!array) return HavelRuntimeError("push() received null array");
        array->push_back(args[1]);
        return HavelValue(array);
    }));
    
    // Array pop
    environment->Define("pop", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("pop() requires array");
        if (!std::holds_alternative<HavelArray>(args[0])) return HavelRuntimeError("pop() arg must be array");
        
        auto array = std::get<HavelArray>(args[0]);
        if (!array || array->empty()) return HavelRuntimeError("Cannot pop from empty array");
        
        HavelValue last = array->back();
        array->pop_back();
        return last;
    }));
    
    // Array join
    environment->Define("join", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("join() requires array");
        if (!std::holds_alternative<HavelArray>(args[0])) return HavelRuntimeError("join() first arg must be array");
        
        auto array = std::get<HavelArray>(args[0]);
        std::string separator = args.size() > 1 ? ValueToString(args[1]) : ",";
        
        std::string result;
        if (array) {
            for (size_t i = 0; i < array->size(); ++i) {
                result += ValueToString((*array)[i]);
                if (i < array->size() - 1) result += separator;
            }
        }
        return HavelValue(result);
    }));
    
    // String split
    environment->Define("split", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("split() requires string");
        std::string text = ValueToString(args[0]);
        std::string delimiter = args.size() > 1 ? ValueToString(args[1]) : ",";
        
        auto result = std::make_shared<std::vector<HavelValue>>();
        size_t start = 0;
        size_t end = text.find(delimiter);
        
        while (end != std::string::npos) {
            result->push_back(HavelValue(text.substr(start, end - start)));
            start = end + delimiter.length();
            end = text.find(delimiter, start);
        }
        result->push_back(HavelValue(text.substr(start)));
        
        return HavelValue(result);
    }));
    
    // Expose as module object: brightnessManager
    auto bm = std::make_shared<std::unordered_map<std::string, HavelValue>>();
if (auto v = environment->Get("brightnessManager.getBrightness")) (*bm)["getBrightness"] = *v;
    if (auto v = environment->Get("brightnessManager.setBrightness")) (*bm)["setBrightness"] = *v;
    if (auto v = environment->Get("brightnessManager.increaseBrightness")) (*bm)["increaseBrightness"] = *v;
    if (auto v = environment->Get("brightnessManager.decreaseBrightness")) (*bm)["decreaseBrightness"] = *v;
    environment->Define("brightnessManager", HavelValue(bm));
}

void Interpreter::InitializeIOBuiltins() {
    // IO block - disable all input
    environment->Define("io.block", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (hotkeyManager) {
            // TODO: Add actual block method to HotkeyManager when available
            std::cout << "[INFO] IO input blocked" << std::endl;
        } else {
            std::cout << "[WARN] HotkeyManager not available" << std::endl;
        }
        return HavelValue(nullptr);
    }));
    
    // IO unblock - enable all input
    environment->Define("io.unblock", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (hotkeyManager) {
            // TODO: Add actual unblock method to HotkeyManager when available
            std::cout << "[INFO] IO input unblocked" << std::endl;
        } else {
            std::cout << "[WARN] HotkeyManager not available" << std::endl;
        }
        return HavelValue(nullptr);
    }));
    
    // IO grab - grab exclusive input
    environment->Define("io.grab", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (hotkeyManager) {
            // TODO: Add actual grab method to HotkeyManager when available
            std::cout << "[INFO] IO input grabbed" << std::endl;
        } else {
            std::cout << "[WARN] HotkeyManager not available" << std::endl;
        }
        return HavelValue(nullptr);
    }));
    
    // IO ungrab - release exclusive input
    environment->Define("io.ungrab", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (hotkeyManager) {
            // TODO: Add actual ungrab method to HotkeyManager when available
            std::cout << "[INFO] IO input ungrabbed" << std::endl;
        } else {
            std::cout << "[WARN] HotkeyManager not available" << std::endl;
        }
        return HavelValue(nullptr);
    }));
    
    // IO test keycode - print keycode for next pressed key
    environment->Define("io.testKeycode", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] Press any key to see its keycode... (Not yet implemented)" << std::endl;
        // TODO: Implement keycode testing mode
        return HavelValue(nullptr);
    }));
    
    // Expose as module object: audioManager
    auto am = std::make_shared<std::unordered_map<std::string, HavelValue>>();
if (auto v = environment->Get("audio.getVolume")) (*am)["getVolume"] = *v;
    if (auto v = environment->Get("audio.setVolume")) (*am)["setVolume"] = *v;
    if (auto v = environment->Get("audio.increaseVolume")) (*am)["increaseVolume"] = *v;
    if (auto v = environment->Get("audio.decreaseVolume")) (*am)["decreaseVolume"] = *v;
    if (auto v = environment->Get("audio.toggleMute")) (*am)["toggleMute"] = *v;
    if (auto v = environment->Get("audio.setMute")) (*am)["setMute"] = *v;
    if (auto v = environment->Get("audio.isMuted")) (*am)["isMuted"] = *v;
    environment->Define("audioManager", HavelValue(am));
}

void Interpreter::InitializeBrightnessBuiltins() {
    // Brightness get
    environment->Define("brightnessManager.getBrightness", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!brightnessManager) return HavelRuntimeError("BrightnessManager not available");
        return HavelValue(brightnessManager->getBrightness());
    }));
    
    // Brightness set
    environment->Define("brightnessManager.setBrightness", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!brightnessManager) return HavelRuntimeError("BrightnessManager not available");
        if (args.empty()) return HavelRuntimeError("setBrightness() requires brightness value");
        double brightness = ValueToNumber(args[0]);
        brightnessManager->setBrightness(brightness);
        return HavelValue(nullptr);
    }));
    
    // Brightness increase
    environment->Define("brightnessManager.increaseBrightness", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!brightnessManager) return HavelRuntimeError("BrightnessManager not available");
        double step = args.empty() ? 0.1 : ValueToNumber(args[0]);
        brightnessManager->increaseBrightness(step);
        return HavelValue(nullptr);
    }));
    
    // Brightness decrease
    environment->Define("brightnessManager.decreaseBrightness", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!brightnessManager) return HavelRuntimeError("BrightnessManager not available");
        double step = args.empty() ? 0.1 : ValueToNumber(args[0]);
        brightnessManager->decreaseBrightness(step);
        return HavelValue(nullptr);
    }));
    
    // Expose as module object: launcher
    auto launcher = std::make_shared<std::unordered_map<std::string, HavelValue>>();
if (auto v = environment->Get("run")) (*launcher)["run"] = *v;
    if (auto v = environment->Get("runAsync")) (*launcher)["runAsync"] = *v;
    if (auto v = environment->Get("runDetached")) (*launcher)["runDetached"] = *v;
    if (auto v = environment->Get("terminal")) (*launcher)["terminal"] = *v;
    environment->Define("launcher", HavelValue(launcher));
}

void Interpreter::InitializeDebugBuiltins() {
    // Debug flag
    environment->Define("debug", HavelValue(false));
    
    // Debug print with conditional execution
    environment->Define("debug.print", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        auto debugFlag = environment->Get("debug");
        bool isDebug = debugFlag && ValueToBool(*debugFlag);
        
        if (isDebug) {
            std::cout << "[DEBUG] ";
            for(const auto& arg : args) {
                std::cout << this->ValueToString(arg) << " ";
            }
            std::cout << std::endl;
        }
        return HavelValue(nullptr);
    }));
    
    // Assert function
    environment->Define("assert", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("assert() requires condition");
        if (!ValueToBool(args[0])) {
            std::string msg = args.size() > 1 ? ValueToString(args[1]) : "Assertion failed";
            return HavelRuntimeError(msg);
        }
        return HavelValue(nullptr);
    }));
}

void Interpreter::InitializeAudioBuiltins() {
    // Volume control
    environment->Define("audio.getVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        return HavelValue(audioManager->getVolume());
    }));
    
    environment->Define("audio.setVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) return HavelRuntimeError("setVolume() requires volume value (0.0-1.0)");
        double volume = ValueToNumber(args[0]);
        audioManager->setVolume(volume);
        return HavelValue(nullptr);
    }));
    
    environment->Define("audio.increaseVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        audioManager->increaseVolume(amount);
        return HavelValue(nullptr);
    }));
    
    environment->Define("audio.decreaseVolume", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        audioManager->decreaseVolume(amount);
        return HavelValue(nullptr);
    }));
    
    // Mute control
    environment->Define("audio.toggleMute", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        audioManager->toggleMute();
        return HavelValue(nullptr);
    }));
    
    environment->Define("audio.setMute", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) return HavelRuntimeError("setMute() requires boolean value");
        bool muted = ValueToBool(args[0]);
        audioManager->setMute(muted);
        return HavelValue(nullptr);
    }));
    
    environment->Define("audio.isMuted", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!audioManager) return HavelRuntimeError("AudioManager not available");
        return HavelValue(audioManager->isMuted());
    }));
}

void Interpreter::InitializeMediaBuiltins() {
    // Note: Media controls typically work through MPVController or system media keys
    // These are placeholders for future implementation
    environment->Define("media.play", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] media.play() not yet implemented" << std::endl;
        return HavelValue(nullptr);
    }));
    
    environment->Define("media.pause", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] media.pause() not yet implemented" << std::endl;
        return HavelValue(nullptr);
    }));
    
    environment->Define("media.toggle", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] media.toggle() not yet implemented" << std::endl;
        return HavelValue(nullptr);
    }));
    
    environment->Define("media.next", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] media.next() not yet implemented" << std::endl;
        return HavelValue(nullptr);
    }));
    
    environment->Define("media.previous", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        std::cout << "[INFO] media.previous() not yet implemented" << std::endl;
        return HavelValue(nullptr);
    }));
}

void Interpreter::InitializeLauncherBuiltins() {
    // Process launching
    environment->Define("run", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("run() requires command");
        std::string command = ValueToString(args[0]);
        
        auto result = Launcher::runSync(command);
        return HavelValue(result.success);
    }));
    
    environment->Define("runAsync", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("runAsync() requires command");
        std::string command = ValueToString(args[0]);
        
        auto result = Launcher::runAsync(command);
        return HavelValue(static_cast<double>(result.pid));
    }));
    
    environment->Define("runDetached", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("runDetached() requires command");
        std::string command = ValueToString(args[0]);
        
        auto result = Launcher::runDetached(command);
        return HavelValue(result.success);
    }));
    
    environment->Define("terminal", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) return HavelRuntimeError("terminal() requires command");
        std::string command = ValueToString(args[0]);
        
        auto result = Launcher::terminal(command);
        return HavelValue(result.success);
    }));
    
    // Expose as module object: gui
    auto guiObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
if (auto v = environment->Get("gui.menu")) (*guiObj)["menu"] = *v;
    if (auto v = environment->Get("gui.input")) (*guiObj)["input"] = *v;
    if (auto v = environment->Get("gui.confirm")) (*guiObj)["confirm"] = *v;
    if (auto v = environment->Get("gui.notify")) (*guiObj)["notify"] = *v;
    if (auto v = environment->Get("gui.fileDialog")) (*guiObj)["fileDialog"] = *v;
    if (auto v = environment->Get("gui.directoryDialog")) (*guiObj)["directoryDialog"] = *v;
    environment->Define("gui", HavelValue(guiObj));
}

void Interpreter::InitializeGUIBuiltins() {
    // Menu and dialogs
    environment->Define("gui.menu", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2) return HavelRuntimeError("gui.menu() requires (title, options)");
        
        std::string title = ValueToString(args[0]);
        if (!std::holds_alternative<HavelArray>(args[1])) {
            return HavelRuntimeError("gui.menu() second arg must be array");
        }
        
        auto optionsArray = std::get<HavelArray>(args[1]);
        std::vector<std::string> options;
        if (optionsArray) {
            for (const auto& opt : *optionsArray) {
                options.push_back(ValueToString(opt));
            }
        }
        
        std::string selected = guiManager->showMenu(title, options);
        return HavelValue(selected);
    }));
    
    environment->Define("gui.input", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        if (args.empty()) return HavelRuntimeError("gui.input() requires title");
        
        std::string title = ValueToString(args[0]);
        std::string prompt = args.size() > 1 ? ValueToString(args[1]) : "";
        std::string defaultValue = args.size() > 2 ? ValueToString(args[2]) : "";
        
        std::string input = guiManager->showInputDialog(title, prompt, defaultValue);
        return HavelValue(input);
    }));
    
    environment->Define("gui.confirm", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2) return HavelRuntimeError("gui.confirm() requires (title, message)");
        
        std::string title = ValueToString(args[0]);
        std::string message = ValueToString(args[1]);
        
        bool confirmed = guiManager->showConfirmDialog(title, message);
        return HavelValue(confirmed);
    }));
    
    environment->Define("gui.notify", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2) return HavelRuntimeError("gui.notify() requires (title, message)");
        
        std::string title = ValueToString(args[0]);
        std::string message = ValueToString(args[1]);
        std::string icon = args.size() > 2 ? ValueToString(args[2]) : "info";
        
        guiManager->showNotification(title, message, icon);
        return HavelValue(nullptr);
    }));
    
    // Window transparency
    environment->Define("window.setTransparency", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        if (args.empty()) return HavelRuntimeError("setTransparency() requires opacity (0.0-1.0)");
        
        double opacity = ValueToNumber(args[0]);
        bool success = guiManager->setActiveWindowTransparency(opacity);
        return HavelValue(success);
    }));
    
    // File dialogs
    environment->Define("gui.fileDialog", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        
        std::string title = args.size() > 0 ? ValueToString(args[0]) : "Select File";
        std::string startDir = args.size() > 1 ? ValueToString(args[1]) : "";
        std::string filter = args.size() > 2 ? ValueToString(args[2]) : "";
        
        std::string selected = guiManager->showFileDialog(title, startDir, filter, false);
        return HavelValue(selected);
    }));
    
    environment->Define("gui.directoryDialog", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        if (!guiManager) return HavelRuntimeError("GUIManager not available");
        
        std::string title = args.size() > 0 ? ValueToString(args[0]) : "Select Directory";
        std::string startDir = args.size() > 1 ? ValueToString(args[1]) : "";
        
        std::string selected = guiManager->showDirectoryDialog(title, startDir);
        return HavelValue(selected);
    }));
}

} // namespace havel
