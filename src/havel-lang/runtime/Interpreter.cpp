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
        std::stringstream ss;
        ss << "[";
        const auto& vec = std::get<std::vector<std::string>>(value);
        for (size_t i = 0; i < vec.size(); ++i) {
            ss << vec[i];
            if (i < vec.size() - 1) {
                ss << ", ";
            }
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

// Constructor - initialize the interpreter with standard library
Interpreter::Interpreter() {
    // Initialize system components
    io = std::make_shared<IO>();
    clipboard = std::make_shared<havel::Clipboard>(havel::Clipboard::Instance());
    windowManager = std::make_shared<WindowManager>();
    
    // Initialize standard library modules
    InitializeStandardLibrary();
}

// Main execution method
HavelValue Interpreter::Execute(const std::string& sourceCode) {
    try {
        // Parse the source code
        Lexer lexer(sourceCode);
        auto tokens = lexer.tokenize();
        
        parser::Parser parser;
        auto program = parser.produceAST(sourceCode);
        
        // Evaluate the program
        return EvaluateProgram(*program);
    } catch (const std::exception& e) {
        std::cerr << "Interpreter error: " << e.what() << std::endl;
        return nullptr;
    }
}

// Register hotkeys from Havel code
void Interpreter::RegisterHotkeys(const std::string& sourceCode) {
    try {
        // Parse the source code
        Lexer lexer(sourceCode);
        auto tokens = lexer.tokenize();
        
        parser::Parser parser;
        auto program = parser.produceAST(sourceCode);
        
        // Evaluate the program to register hotkeys
        EvaluateProgram(*program);
    } catch (const std::exception& e) {
        std::cerr << "Hotkey registration error: " << e.what() << std::endl;
    }
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
    // Use dynamic_cast to determine the statement type
    if (const auto* hotkeyBinding = dynamic_cast<const ast::HotkeyBinding*>(&statement)) {
        return EvaluateHotkeyBinding(*hotkeyBinding);
    } else if (const auto* blockStatement = dynamic_cast<const ast::BlockStatement*>(&statement)) {
        return EvaluateBlockStatement(*blockStatement);
    } else if (const auto* expr = dynamic_cast<const ast::Expression*>(&statement)) {
        return EvaluateExpression(*expr);
    }
    
    return nullptr;
}

// Evaluate an Expression node
HavelValue Interpreter::EvaluateExpression(const ast::Expression& expression) {
    // Use dynamic_cast to determine the expression type
    if (const auto* stringLiteral = dynamic_cast<const ast::StringLiteral*>(&expression)) {
        return EvaluateStringLiteral(*stringLiteral);
    } else if (const auto* numberLiteral = dynamic_cast<const ast::NumberLiteral*>(&expression)) {
        return EvaluateNumberLiteral(*numberLiteral);
    } else if (const auto* identifier = dynamic_cast<const ast::Identifier*>(&expression)) {
        return EvaluateIdentifier(*identifier);
    } else if (const auto* binaryExpr = dynamic_cast<const ast::BinaryExpression*>(&expression)) {
        return EvaluateBinaryExpression(*binaryExpr);
    } else if (const auto* callExpr = dynamic_cast<const ast::CallExpression*>(&expression)) {
        return EvaluateCallExpression(*callExpr);
    } else if (const auto* memberExpr = dynamic_cast<const ast::MemberExpression*>(&expression)) {
        return EvaluateMemberExpression(*memberExpr);
    } else if (const auto* pipelineExpr = dynamic_cast<const ast::PipelineExpression*>(&expression)) {
        return EvaluatePipelineExpression(*pipelineExpr);
    }
    
    return nullptr;
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
            this->EvaluateExpression(*action);
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
    // Evaluate the left-hand side of the pipeline
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
        for (const auto& arg : callExpr->arguments) {
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
                            return clipboard->GetText();
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
        case ast::BinaryOperator::Equal:
            if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
                return std::get<std::string>(left) == std::get<std::string>(right);
            } else {
                return ValueToNumber(left) == ValueToNumber(right);
            }
        case ast::BinaryOperator::NotEqual:
            if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
                return std::get<std::string>(left) != std::get<std::string>(right);
            } else {
                return ValueToNumber(left) != ValueToNumber(right);
            }
        case ast::BinaryOperator::LessThan:
            return ValueToNumber(left) < ValueToNumber(right);
        case ast::BinaryOperator::LessThanOrEqual:
            return ValueToNumber(left) <= ValueToNumber(right);
        case ast::BinaryOperator::GreaterThan:
            return ValueToNumber(left) > ValueToNumber(right);
        case ast::BinaryOperator::GreaterThanOrEqual:
            return ValueToNumber(left) >= ValueToNumber(right);
        case ast::BinaryOperator::And:
            return ValueToBool(left) && ValueToBool(right);
        case ast::BinaryOperator::Or:
            return ValueToBool(left) || ValueToBool(right);
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

// Evaluate a CallExpression node
HavelValue Interpreter::EvaluateCallExpression(const ast::CallExpression& call) {
    // Evaluate the arguments
    std::vector<HavelValue> args;
    for (const auto& arg : call.arguments) {
        args.push_back(EvaluateExpression(*arg));
    }
    
    // Handle different types of callees
    if (const auto* memberExpr = dynamic_cast<const ast::MemberExpression*>(call.callee.get())) {
        // It's a method call on an object (e.g., text.upper())
        if (const auto* objIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->object.get())) {
            std::string moduleName = objIdentifier->name;
            
            if (const auto* propIdentifier = dynamic_cast<const ast::Identifier*>(memberExpr->property.get())) {
                std::string funcName = propIdentifier->name;
                
                // Get the module
                auto module = environment.GetModule(moduleName);
                if (module && module->HasFunction(funcName)) {
                    auto func = module->GetFunction(funcName);
                    return func(args);
                }
            }
        }
    } else if (const auto* identifier = dynamic_cast<const ast::Identifier*>(call.callee.get())) {
        // It's a simple function call (e.g., send())
        std::string funcName = identifier->name;
        
        // Check if it's a built-in function
        if (funcName == "send") {
            // Send text to the active window
            if (!args.empty()) {
                std::string text = ValueToString(args[0]);
                io->SendText(text);
                return text;
            }
        }
    }
    
    return nullptr;
}

// Evaluate a MemberExpression node
HavelValue Interpreter::EvaluateMemberExpression(const ast::MemberExpression& member) {
    // Handle property access on objects
    if (const auto* objIdentifier = dynamic_cast<const ast::Identifier*>(member.object.get())) {
        std::string moduleName = objIdentifier->name;
        
        if (const auto* propIdentifier = dynamic_cast<const ast::Identifier*>(member.property.get())) {
            std::string propName = propIdentifier->name;
            
            // Handle special properties
            if (moduleName == "clipboard") {
                if (propName == "out") {
                    return clipboard->GetText();
                }
            } else if (moduleName == "window") {
                if (propName == "current") {
                    auto currentWindow = windowManager->GetActiveWindow();
                    return currentWindow ? currentWindow->GetTitle() : "";
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
    // Check if it's an integer or a float
    if (num.value == static_cast<int>(num.value)) {
        return static_cast<int>(num.value);
    } else {
        return num.value;
    }
}

// Evaluate an Identifier node
HavelValue Interpreter::EvaluateIdentifier(const ast::Identifier& id) {
    // Look up the identifier in the environment
    if (environment.HasVariable(id.name)) {
        return environment.GetVariable(id.name);
    }
    
    // If not found, it might be a module name
    if (environment.HasModule(id.name)) {
        // Return the module name as a string
        return id.name;
    }
    
    // Not found, return null
    return nullptr;
}

// Initialize the standard library with built-in modules and functions
void Interpreter::InitializeStandardLibrary() {
    InitializeClipboardModule();
    InitializeTextModule();
    InitializeWindowModule();
    InitializeSystemModule();
}

// Initialize the clipboard module
void Interpreter::InitializeClipboardModule() {
    auto clipboardModule = std::make_shared<Module>("clipboard");
    
    // clipboard.get() - Get text from clipboard
    clipboardModule->AddFunction("get", [this](const std::vector<HavelValue>& args) -> HavelValue {
        return clipboard->GetText();
    });
    
    // clipboard.set(text) - Set text to clipboard
    clipboardModule->AddFunction("set", [this](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return nullptr;
        std::string text = ValueToString(args[0]);
        clipboard->SetText(text);
        return text;
    });
    
    // clipboard.clear() - Clear clipboard
    clipboardModule->AddFunction("clear", [this](const std::vector<HavelValue>& args) -> HavelValue {
        clipboard->Clear();
        return nullptr;
    });
    
    environment.AddModule(clipboardModule);
}

// Initialize the text module
void Interpreter::InitializeTextModule() {
    auto textModule = std::make_shared<Module>("text");
    
    // text.upper(str) - Convert text to uppercase
    textModule->AddFunction("upper", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return "";
        std::string text = ValueToString(args[0]);
        std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        return text;
    });
    
    // text.lower(str) - Convert text to lowercase
    textModule->AddFunction("lower", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return "";
        std::string text = ValueToString(args[0]);
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        return text;
    });
    
    // text.replace(str, search, replace) - Replace text
    textModule->AddFunction("replace", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.size() < 3) return args.empty() ? "" : ValueToString(args[0]);
        
        std::string text = ValueToString(args[0]);
        std::string search = ValueToString(args[1]);
        std::string replace = ValueToString(args[2]);
        
        size_t pos = 0;
        while ((pos = text.find(search, pos)) != std::string::npos) {
            text.replace(pos, search.length(), replace);
            pos += replace.length();
        }
        
        return text;
    });
    
    // text.contains(str, search) - Check if text contains substring
    textModule->AddFunction("contains", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.size() < 2) return false;
        
        std::string text = ValueToString(args[0]);
        std::string search = ValueToString(args[1]);
        
        return text.find(search) != std::string::npos;
    });
    
    // text.trim(str) - Trim whitespace
    textModule->AddFunction("trim", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return "";
        
        std::string text = ValueToString(args[0]);
        // Trim leading whitespace
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        // Trim trailing whitespace
        text.erase(std::find_if(text.rbegin(), text.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), text.end());
        
        return text;
    });
    
    // text.sanitize(str) - Sanitize text (remove control characters)
    textModule->AddFunction("sanitize", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return "";
        
        std::string text = ValueToString(args[0]);
        text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
            return std::iscntrl(ch) && ch != '\n' && ch != '\t';
        }), text.end());
        
        return text;
    });
    
    environment.AddModule(textModule);
}

// Initialize the window module
void Interpreter::InitializeWindowModule() {
    auto windowModule = std::make_shared<Module>("window");
    
    // window.focus(title) - Focus window by title
    windowModule->AddFunction("focus", [this](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return false;
        
        std::string title = ValueToString(args[0]);
        auto window = windowManager->FindWindowByTitle(title);
        if (window) {
            window->Activate();
            return true;
        }
        
        return false;
    });
    
    // window.next() - Focus next window
    windowModule->AddFunction("next", [this](const std::vector<HavelValue>& args) -> HavelValue {
        windowManager->FocusNextWindow();
        return true;
    });
    
    // window.prev() - Focus previous window
    windowModule->AddFunction("prev", [this](const std::vector<HavelValue>& args) -> HavelValue {
        windowManager->FocusPreviousWindow();
        return true;
    });
    
    // window.list() - Get list of window titles
    windowModule->AddFunction("list", [this](const std::vector<HavelValue>& args) -> HavelValue {
        auto windows = windowManager->GetAllWindows();
        std::vector<std::string> titles;
        
        for (const auto& window : windows) {
            titles.push_back(window->GetTitle());
        }
        
        return titles;
    });
    
    environment.AddModule(windowModule);
}

// Initialize the system module
void Interpreter::InitializeSystemModule() {
    auto systemModule = std::make_shared<Module>("system");
    
    // system.sleep(ms) - Sleep for milliseconds
    systemModule->AddFunction("sleep", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return nullptr;
        
        int ms = static_cast<int>(ValueToNumber(args[0]));
        if (ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        
        return nullptr;
    });
    
    // system.log(message) - Log a message
    systemModule->AddFunction("log", [](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.empty()) return nullptr;
        
        std::string message = ValueToString(args[0]);
        havel::Logger::getInstance().info(message);
        
        return nullptr;
    });
    
    environment.AddModule(systemModule);
}

} // namespace havel
