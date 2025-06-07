// src/havel-lang/runtime/Interpreter.cpp
#include "Interpreter.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace havel {

// Helper function to convert HavelValue to string for output
std::string Interpreter::ValueToString(const HavelValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return "null";
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    } else if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    } else if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    } else if (std::holds_alternative<std::vector<std::string>>(value)) {
        const auto& vec = std::get<std::vector<std::string>>(value);
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < vec.size(); i++) {
            if (i > 0) ss << ", ";
            ss << vec[i];
        }
        ss << "]";
        return ss.str();
    }
    return "undefined";
}

// Helper function to convert HavelValue to boolean
bool Interpreter::ValueToBool(const HavelValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return false;
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    } else if (std::holds_alternative<int>(value)) {
        return std::get<int>(value) != 0;
    } else if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) != 0.0;
    } else if (std::holds_alternative<std::string>(value)) {
        return !std::get<std::string>(value).empty();
    } else if (std::holds_alternative<std::vector<std::string>>(value)) {
        return !std::get<std::vector<std::string>>(value).empty();
    }
    return false;
}

// Helper function to convert HavelValue to number
double Interpreter::ValueToNumber(const HavelValue& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return 0.0;
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? 1.0 : 0.0;
    } else if (std::holds_alternative<int>(value)) {
        return static_cast<double>(std::get<int>(value));
    } else if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        try {
            return std::stod(std::get<std::string>(value));
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

// Constructor
Interpreter::Interpreter() {
    // Initialize system components
    io = std::make_unique<IO>();
    
    // Initialize standard library modules
    InitializeStandardLibrary();
}

// Execute Havel code
HavelValue Interpreter::Execute(const std::string& sourceCode) {
    // Parse the source code into an AST
    parser::Parser parser;
    auto program = parser.produceAST(sourceCode);
    
    // Evaluate the AST
    return EvaluateProgram(*program);
}

// Register hotkeys from Havel code
void Interpreter::RegisterHotkeys(const std::string& sourceCode) {
    // Parse the source code into an AST
    parser::Parser parser;
    auto program = parser.produceAST(sourceCode);
    
    // Evaluate the AST to register hotkeys
    EvaluateProgram(*program);
}

// Evaluate a Program node
HavelValue Interpreter::EvaluateProgram(const ast::Program& program) {
    HavelValue lastValue = nullptr;
    
    // Evaluate each statement in the program
    for (const auto& statement : program.body) {
        lastValue = EvaluateStatement(*statement);
    }
    
    return lastValue;
}

// Evaluate a Statement node
HavelValue Interpreter::EvaluateStatement(const ast::Statement& statement) {
    switch (statement.kind) {
        case ast::NodeType::HotkeyBinding:
            return EvaluateHotkeyBinding(static_cast<const ast::HotkeyBinding&>(statement));
        case ast::NodeType::BlockStatement:
            return EvaluateBlockStatement(static_cast<const ast::BlockStatement&>(statement));
        case ast::NodeType::ExpressionStatement:
            return EvaluateExpression(*static_cast<const ast::ExpressionStatement&>(statement).expression);
        default:
            throw std::runtime_error("Unknown statement type");
    }
}

// Evaluate an Expression node
HavelValue Interpreter::EvaluateExpression(const ast::Expression& expression) {
    switch (expression.kind) {
        case ast::NodeType::PipelineExpression:
            return EvaluatePipelineExpression(static_cast<const ast::PipelineExpression&>(expression));
        case ast::NodeType::BinaryExpression:
            return EvaluateBinaryExpression(static_cast<const ast::BinaryExpression&>(expression));
        case ast::NodeType::CallExpression:
            return EvaluateCallExpression(static_cast<const ast::CallExpression&>(expression));
        case ast::NodeType::MemberExpression:
            return EvaluateMemberExpression(static_cast<const ast::MemberExpression&>(expression));
        case ast::NodeType::StringLiteral:
            return EvaluateStringLiteral(static_cast<const ast::StringLiteral&>(expression));
        case ast::NodeType::NumberLiteral:
            return EvaluateNumberLiteral(static_cast<const ast::NumberLiteral&>(expression));
        case ast::NodeType::Identifier:
            return EvaluateIdentifier(static_cast<const ast::Identifier&>(expression));
        case ast::NodeType::HotkeyLiteral:
            // Return the hotkey string as a value
            return static_cast<const ast::HotkeyLiteral&>(expression).combination;
        default:
            throw std::runtime_error("Unknown expression type");
    }
}

// Evaluate a HotkeyBinding node
HavelValue Interpreter::EvaluateHotkeyBinding(const ast::HotkeyBinding& binding) {
    // Extract the hotkey from the binding
    const auto* hotkeyLiteral = dynamic_cast<const ast::HotkeyLiteral*>(binding.hotkey.get());
    if (!hotkeyLiteral) {
        throw std::runtime_error("Invalid hotkey in binding");
    }
    
    std::string hotkey = hotkeyLiteral->combination;
    
    // Create a lambda that will evaluate the action when the hotkey is triggered
    auto actionHandler = [this, action = binding.action.get()]() {
        if (action) {
            // Check if the action is an ExpressionStatement
            if (auto exprStmt = dynamic_cast<const ast::ExpressionStatement*>(action)) {
                if (exprStmt->expression) {
                    this->EvaluateExpression(*exprStmt->expression);
                }
            } else {
                // For other statement types, evaluate them as statements
                this->EvaluateStatement(*action);
            }
        }
    };
    
    // Register the hotkey with the IO system
    // Convert string hotkey to Key and modifiers for IO::AddHotkey
    // This is a simplified implementation - would need proper parsing of hotkey string
    Key key = 0; // Default key code
    int modifiers = 0; // Default modifiers
    io->AddHotkey(hotkey, key, modifiers, actionHandler);
    
    return nullptr;
}

// Evaluate a BlockStatement node
HavelValue Interpreter::EvaluateBlockStatement(const ast::BlockStatement& block) {
    HavelValue lastValue = nullptr;
    
    // Evaluate each statement in the block
    for (const auto& statement : block.body) {
        lastValue = EvaluateStatement(*statement);
    }
    
    return lastValue;
}

// Evaluate a PipelineExpression node
HavelValue Interpreter::EvaluatePipelineExpression(const ast::PipelineExpression& pipeline) {
    // Get the first stage of the pipeline
    if (pipeline.stages.empty()) {
        throw std::runtime_error("Pipeline has no stages");
    }
    
    HavelValue value = EvaluateExpression(*pipeline.stages[0]);
    
    // For subsequent stages, we need to handle differently based on the type
    if (pipeline.stages.size() < 2) {
        return value; // Only one stage, return its value
    }
    
    // Process each stage after the first one
    for (size_t i = 1; i < pipeline.stages.size(); i++) {
        auto& stage = pipeline.stages[i];
        
        if (const auto* callExpr = dynamic_cast<const ast::CallExpression*>(stage.get())) {
            // Add the left-hand value as the first argument to the function call
            std::vector<HavelValue> args;
            args.push_back(value);
            
            // Add any other arguments from the call expression
            for (const auto& arg : callExpr->args) {
                args.push_back(EvaluateExpression(*arg));
            }
            
            // Get the function to call
            std::string funcName;
            std::string moduleName;
            
            if (const auto* memberExpr = dynamic_cast<const ast::MemberExpression*>(callExpr->callee.get())) {
                if (const auto* objIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->object.get())) {
                    std::string moduleName = objIdentifier->symbol;
                    
                    if (const auto* propIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->property.get())) {
                        std::string propName = propIdentifier->symbol;
                        
                        // Handle special properties
                        if (moduleName == "clipboard") {
                            if (propName == "out") {
                                return havel::Clipboard::Instance().GetText();
                            }
                        }
                    }
                }
            }
        }
    }
    
    // If we couldn't handle it as a special case, just return the current value
    return value;
}

// Evaluate a BinaryExpression node
HavelValue Interpreter::EvaluateBinaryExpression(const ast::BinaryExpression& binary) {
    // Evaluate left and right operands
    HavelValue left = EvaluateExpression(*binary.left);
    HavelValue right = EvaluateExpression(*binary.right);
    
    // Perform the operation based on the operator string
    if (binary.operator_ == "+") {
        // String concatenation or numeric addition
        if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
            return ValueToString(left) + ValueToString(right);
        } else {
            return ValueToNumber(left) + ValueToNumber(right);
        }
    } else if (binary.operator_ == "-") {
        return ValueToNumber(left) - ValueToNumber(right);
    } else if (binary.operator_ == "*") {
        return ValueToNumber(left) * ValueToNumber(right);
    } else if (binary.operator_ == "/") {
        if (ValueToNumber(right) == 0.0) {
            throw std::runtime_error("Division by zero");
        }
        return ValueToNumber(left) / ValueToNumber(right);
    } else if (binary.operator_ == "==") {
        if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
            return std::get<std::string>(left) == std::get<std::string>(right);
        } else {
            return ValueToNumber(left) == ValueToNumber(right);
        }
    } else if (binary.operator_ == "!=") {
        if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
            return std::get<std::string>(left) != std::get<std::string>(right);
        } else {
            return ValueToNumber(left) != ValueToNumber(right);
        }
    } else if (binary.operator_ == "<") {
        return ValueToNumber(left) < ValueToNumber(right);
    } else if (binary.operator_ == "<=") {
        return ValueToNumber(left) <= ValueToNumber(right);
    } else if (binary.operator_ == ">") {
        return ValueToNumber(left) > ValueToNumber(right);
    } else if (binary.operator_ == ">=") {
        return ValueToNumber(left) >= ValueToNumber(right);
    } else if (binary.operator_ == "&&") {
        return ValueToBool(left) && ValueToBool(right);
    } else if (binary.operator_ == "||") {
        return ValueToBool(left) || ValueToBool(right);
    } else {
        throw std::runtime_error("Unknown binary operator: " + binary.operator_);
    }
}

// Evaluate a CallExpression node
HavelValue Interpreter::EvaluateCallExpression(const ast::CallExpression& call) {
    // Evaluate the arguments
    std::vector<HavelValue> args;
    for (const auto& arg : call.args) {
        args.push_back(EvaluateExpression(*arg));
    }
    
    // Handle different types of callees
    if (const auto* identifier = dynamic_cast<const ast::Identifier*>(call.callee.get())) {
        // It's a simple function call like print("Hello")
        std::string funcName = identifier->symbol;
        
        // Check if it's a built-in function
        if (funcName == "print") {
            // Print to console
            for (const auto& arg : args) {
                std::cout << ValueToString(arg);
            }
            std::cout << std::endl;
            return nullptr;
        }
        else if (funcName == "send") {
            // Send text to the active window
            if (!args.empty()) {
                std::string text = ValueToString(args[0]);
                io->Send(text.c_str());
                return text;
            }
            return nullptr;
        }
    } else if (const auto* memberExpr = dynamic_cast<const ast::MemberExpression*>(call.callee.get())) {
        // It's a method call like clipboard.set("text")
        if (const auto* objIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->object.get())) {
            std::string moduleName = objIdentifier->symbol;
            
            if (const auto* propIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->property.get())) {
                std::string methodName = propIdentifier->symbol;
                
                // Get the module
                auto module = environment.GetModule(moduleName);
                if (module && module->HasFunction(methodName)) {
                    auto func = module->GetFunction(methodName);
                    return func(args);
                }
            }
        }
    }
    
    return nullptr;
}

// Evaluate a MemberExpression node
HavelValue Interpreter::EvaluateMemberExpression(const ast::MemberExpression& member) {
    // Handle property access on objects
    if (const auto* objIdentifier = dynamic_cast<const ast::Identifier*>(member.object.get())) {
        std::string moduleName = objIdentifier->symbol;
        
        if (const auto* propIdentifier = dynamic_cast<const ast::Identifier*>(member.property.get())) {
            std::string propName = propIdentifier->symbol;
            
            // Handle special properties
            if (moduleName == "clipboard") {
                if (propName == "text") {
                    return havel::Clipboard::Instance().GetText();
                }
            } else if (moduleName == "window") {
                if (propName == "title") {
                    wID activeWin = WindowManager::GetActiveWindow();
                    if (activeWin != 0) {
                        Window window("", activeWin);
                        return window.Title(activeWin);
                    }
                    return "";
                }
            }
        }
    }
    
    return nullptr;
}

// Evaluate a StringLiteral node
HavelValue Interpreter::EvaluateStringLiteral(const ast::StringLiteral& str) {
    return str.value;
}

// Evaluate a NumberLiteral node
HavelValue Interpreter::EvaluateNumberLiteral(const ast::NumberLiteral& num) {
    // NumberLiteral.value is already a double, so just return it
    return num.value;
}

// Evaluate an Identifier node
HavelValue Interpreter::EvaluateIdentifier(const ast::Identifier& id) {
    // Check if it's a variable
    if (environment.HasVariable(id.symbol)) {
        return environment.GetVariable(id.symbol);
    }
    
    // If not found, return null
    return nullptr;
}

// Initialize the standard library
void Interpreter::InitializeStandardLibrary() {
    InitializeClipboardModule();
    InitializeTextModule();
    InitializeWindowModule();
    InitializeSystemModule();
}

// Initialize the clipboard module
void Interpreter::InitializeClipboardModule() {
    auto clipboardModule = std::make_shared<Module>("clipboard");
    
    // Add clipboard.getText() function
    clipboardModule->AddFunction("getText", [](const std::vector<HavelValue>&) -> HavelValue {
        return havel::Clipboard::Instance().GetText();
    });
    
    // Add clipboard.setText(text) function
    clipboardModule->AddFunction("setText", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string text = Interpreter::ValueToString(args[0]);
            havel::Clipboard::Instance().SetText(text);
            return true;
        }
        return false;
    });
    
    environment.AddModule(clipboardModule);
}

// Initialize the text module
void Interpreter::InitializeTextModule() {
    auto textModule = std::make_shared<Module>("text");
    
    // Add text.upper(str) function
    textModule->AddFunction("upper", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string text = Interpreter::ValueToString(args[0]);
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            return text;
        }
        return "";
    });
    
    // Add text.lower(str) function
    textModule->AddFunction("lower", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string text = Interpreter::ValueToString(args[0]);
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            return text;
        }
        return "";
    });
    
    // Add text.trim(str) function
    textModule->AddFunction("trim", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string text = Interpreter::ValueToString(args[0]);
            // Trim leading whitespace
            text.erase(0, text.find_first_not_of(" \t\n\r"));
            // Trim trailing whitespace
            text.erase(text.find_last_not_of(" \t\n\r") + 1);
            return text;
        }
        return "";
    });
    
    // Add text.replace(str, search, replace) function
    textModule->AddFunction("replace", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.size() >= 3) {
            std::string text = Interpreter::ValueToString(args[0]);
            std::string search = Interpreter::ValueToString(args[1]);
            std::string replace = Interpreter::ValueToString(args[2]);
            
            size_t pos = 0;
            while ((pos = text.find(search, pos)) != std::string::npos) {
                text.replace(pos, search.length(), replace);
                pos += replace.length();
            }
            
            return text;
        }
        return args.empty() ? "" : Interpreter::ValueToString(args[0]);
    });
    
    environment.AddModule(textModule);
}

// Initialize the window module
void Interpreter::InitializeWindowModule() {
    auto windowModule = std::make_shared<Module>("window");
    
    // Add window.getTitle() function
    windowModule->AddFunction("getTitle", [](const std::vector<HavelValue>& args) -> HavelValue {
        wID activeWin = WindowManager::GetActiveWindow();
        if (activeWin != 0) {
            Window window("", activeWin);
            return window.Title(activeWin);
        }
        return "";
    });
    
    // Add window.getClass() function
    windowModule->AddFunction("getClass", [](const std::vector<HavelValue>& args) -> HavelValue {
        return WindowManager::GetActiveWindowClass();
    });
    
    // Add window.focus(title) function
    windowModule->AddFunction("focus", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string title = Interpreter::ValueToString(args[0]);
            wID winId = WindowManager::FindByTitle(title.c_str());
            if (winId != 0) {
                Window window("", winId);
                window.Activate(winId);
                return true;
            }
        }
        return false;
    });
    
    environment.AddModule(windowModule);
}

// Initialize the system module
void Interpreter::InitializeSystemModule() {
    auto systemModule = std::make_shared<Module>("system");
    
    // Add system.sleep(ms) function
    systemModule->AddFunction("sleep", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            int ms = static_cast<int>(Interpreter::ValueToNumber(args[0]));
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        return nullptr;
    });
    
    // Add system.exec(command) function
    systemModule->AddFunction("exec", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (!args.empty()) {
            std::string command = Interpreter::ValueToString(args[0]);
            int result = system(command.c_str());
            return result;
        }
        return nullptr;
    });
    
    environment.AddModule(systemModule);
}

} // namespace havel
