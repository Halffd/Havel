#include "Interpreter.hpp"
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
        else if constexpr (std::is_same_v<T, HavelArray>) return "<array>";
        else if constexpr (std::is_same_v<T, HavelObject>) return "<object>";
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
Interpreter::Interpreter(IO& io_system, WindowManager& window_mgr)
    : io(io_system), windowManager(window_mgr), lastResult(nullptr) {
    environment = std::make_shared<Environment>();
    InitializeStandardLibrary();
}

HavelResult Interpreter::Execute(const std::string& sourceCode) {
    parser::Parser parser;
    auto program = parser.produceAST(sourceCode);
    return Evaluate(*program);
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
        if (isError(blockResult) || std::holds_alternative<ReturnValue>(blockResult)) {
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
    
    // This is complex. We need to keep the action node alive.
    // For now, let's assume the AST lives as long as the interpreter.
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
    lastResult = nullptr;
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
        case ast::BinaryOperator::Sub: lastResult = ValueToNumber(left) - ValueToNumber(right); break;
        case ast::BinaryOperator::Mul: lastResult = ValueToNumber(left) * ValueToNumber(right); break;
        case ast::BinaryOperator::Div:
            if(ValueToNumber(right) == 0.0) { lastResult = HavelRuntimeError("Division by zero"); return; }
            lastResult = ValueToNumber(left) / ValueToNumber(right); 
            break;
        // ... other operators
        default: lastResult = HavelRuntimeError("Unsupported binary operator");
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
     if(const auto* objId = dynamic_cast<const ast::Identifier*>(node.object.get())) {
        if(const auto* propId = dynamic_cast<const ast::Identifier*>(node.property.get())) {
            std::string fullName = objId->symbol + "." + propId->symbol;
            if (auto val = environment->Get(fullName)) {
                lastResult = *val;
                return;
            }
        }
    }
    lastResult = HavelRuntimeError("Member access not implemented for this object type.");
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

    // Check cache first
    if (moduleCache.count(path)) {
        exports = moduleCache.at(path);
    } else {
        // Check for built-in modules
        if (path.rfind("havel:", 0) == 0) {
            std::string moduleName = path.substr(6);
            auto moduleVal = environment->Get(moduleName);
            if (moduleVal && std::holds_alternative<HavelObject>(*moduleVal)) {
                exports = std::get<HavelObject>(*moduleVal);
            } else {
                lastResult = HavelRuntimeError("Built-in module not found: " + moduleName);
                return;
            }
        } else {
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
        }
        // Cache the result
        moduleCache[path] = exports;
    }

    // Import symbols into the current environment
    for (const auto& item : node.importedItems) {
        const std::string& originalName = item.first;
        const std::string& alias = item.second;
        
        if (exports.count(originalName)) {
            environment->Define(alias, exports.at(originalName));
        } else {
            lastResult = HavelRuntimeError("Module '" + path + "' does not export symbol: " + originalName);
            return;
        }
    }

    lastResult = nullptr;
}


void Interpreter::visitStringLiteral(const ast::StringLiteral& node) { lastResult = node.value; }
void Interpreter::visitNumberLiteral(const ast::NumberLiteral& node) { lastResult = node.value; }
void Interpreter::visitHotkeyLiteral(const ast::HotkeyLiteral& node) { lastResult = node.combination; }
void Interpreter::visitIdentifier(const ast::Identifier& node) {
    if (auto val = environment->Get(node.symbol)) {
        lastResult = *val;
    } else {
        lastResult = HavelRuntimeError("Undefined variable: " + node.symbol);
    }
}
// Stubs for unimplemented visit methods
void Interpreter::visitWhileStatement(const ast::WhileStatement& node) { lastResult = HavelRuntimeError("While loops not implemented."); }
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
}
void Interpreter::InitializeSystemBuiltins() {
    environment->Define("print", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
        for(const auto& arg : args) {
            std::cout << this->ValueToString(arg) << " ";
        }
        std::cout << std::endl;
        return HavelValue(nullptr);
    }));
    
    environment->Define("sleep", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
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
    
    environment->Define("window.focus", BuiltinFunction([this](const std::vector<HavelValue>& args) -> HavelResult {
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

} // namespace havel