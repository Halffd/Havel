#include "Interpreter.hpp"
#include "evaluator/ExprEvaluator.hpp"
#include "core/BrightnessManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/automation/AutomationManager.hpp"
#include "core/browser/BrowserModule.hpp"
#include "core/io/EventListener.hpp"
#include "core/io/KeyMap.hpp"
#include "core/io/KeyTap.hpp"
#include "core/io/MapManager.hpp"
#include "core/net/HttpModule.hpp"
#include "core/process/ProcessManager.hpp"
#include "fs/FileManager.hpp"
#include "gui/AltTab.hpp"
#include "gui/GUIManager.hpp"
#include "gui/HavelApp.hpp"
#include "gui/MapManagerWindow.hpp"
#include "gui/ScreenshotManager.hpp"
#include "media/AudioManager.hpp"
#include "process/Launcher.hpp"
#include "qt.hpp"
#include "window/WindowManagerDetector.hpp"
#include "Environment.hpp"  // For Environment and TraitRegistry
#include "stdlib/MathModule.hpp"  // For registerMathModule
#include "stdlib/TypeModule.hpp"  // For registerTypeModule
#include "stdlib/StringModule.hpp"  // For registerStringModule
#include "stdlib/ArrayModule.hpp"  // For registerArrayModule
#include "stdlib/FileModule.hpp"  // For registerFileModule
#include "stdlib/RegexModule.hpp"  // For registerRegexModule
#include "stdlib/ProcessModule.hpp"  // For registerProcessModule
#include "../../modules/hotkey/HotkeyModule.hpp"  // For SetHotkeyInterpreter
#include "../../modules/ModuleLoader.hpp"  // For havel::modules::loadHostModules
#include "semantic/SemanticAnalyzer.hpp"  // For semantic analysis
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <random>
#include <regex>
#include <signal.h>
#include <sstream>
#include <iomanip>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <fstream>
#include <unistd.h>
namespace havel {

// Module cache to avoid re-loading and re-executing files
static std::unordered_map<std::string, HavelObject> moduleCache;

// Helper to check for and extract error from HavelResult
static bool isError(const HavelResult &result) {
  return std::holds_alternative<HavelRuntimeError>(result);
}

// Helper to extract error message from HavelResult
static std::string getErrorMessage(const HavelResult &result) {
  if (auto *err = std::get_if<HavelRuntimeError>(&result)) {
    return err->what();
  }
  return "Unknown error";
}

static HavelValue unwrap(const HavelResult &result) {
  if (auto *val = std::get_if<HavelValue>(&result)) {
    return *val;
  }
  if (auto *ret = std::get_if<ReturnValue>(&result)) {
    return ret->value ? *ret->value : HavelValue();
  }
  if (auto *err = std::get_if<HavelRuntimeError>(&result)) {
    throw *err;
  }
  // This should not be called on break/continue.
  throw std::runtime_error("Cannot unwrap control flow result");
}

std::string Interpreter::ValueToString(const HavelValue &value) {
  // Helper to format numbers nicely (remove trailing zeros, preserve precision)
  auto formatNumber = [](double d) -> std::string {
    // Use ostringstream with high precision
    std::ostringstream oss;
    oss.precision(17);  // Max precision for double
    oss << d;
    std::string s = oss.str();
    
    // Remove trailing zeros after decimal point
    if (s.find('.') != std::string::npos) {
      size_t last = s.find_last_not_of('0');
      if (last != std::string::npos && s[last] == '.') {
        s = s.substr(0, last);  // Remove decimal point too if no decimals
      } else if (last != std::string::npos) {
        s = s.substr(0, last + 1);
      }
    }
    return s;
  };

  return std::visit(
      [&](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>)
          return "null";
        else if constexpr (std::is_same_v<T, bool>)
          return arg ? "true" : "false";
        else if constexpr (std::is_same_v<T, int>)
          return std::to_string(arg);
        else if constexpr (std::is_same_v<T, double>)
          return formatNumber(arg);
        else if constexpr (std::is_same_v<T, std::string>)
          return arg;
        else if constexpr (std::is_same_v<T, std::shared_ptr<HavelFunction>>)
          return "<function>";
        else if constexpr (std::is_same_v<T, BuiltinFunction>)
          return "<builtin_function>";
        else if constexpr (std::is_same_v<T, HavelArray>) {
          // Recursively format array in JSON style
          std::string result = "[";
          if (arg) {
            for (size_t i = 0; i < arg->size(); ++i) {
              result += ValueToString((*arg)[i]);
              if (i < arg->size() - 1)
                result += ", ";
            }
          }
          result += "]";
          return result;
        } else if constexpr (std::is_same_v<T, HavelObject>) {
          // Recursively format object in JSON style
          std::string result = "{";
          if (arg) {
            size_t i = 0;
            for (const auto &[key, val] : *arg) {
              result += key + ": " + ValueToString(val);
              if (i < arg->size() - 1)
                result += ", ";
              ++i;
            }
          }
          result += "}";
          return result;
        } else if constexpr (std::is_same_v<T, HavelSet>) {
          // Format set like array but with set() notation
          std::string result = "set(";
          if (arg.elements) {
            for (size_t i = 0; i < arg.elements->size(); ++i) {
              result += ValueToString((*arg.elements)[i]);
              if (i < arg.elements->size() - 1)
                result += ", ";
            }
          }
          result += ")";
          return result;
        } else
          return "unprintable";
      },
      value.data); // Use .data member for std::visit
}

std::string Interpreter::FormatValue(const HavelValue &value,
                                     const std::string &formatSpec) {
  // Parse format specifier: [.][precision][type]
  // Examples: .4f, .2, d, s
  std::string result;
  char type = 'g';    // Default: general number format
  int precision = -1; // Default: auto

  if (!formatSpec.empty()) {
    size_t pos = 0;

    // Check for type character at the end
    char lastChar = formatSpec.back();
    if (lastChar == 'f' || lastChar == 'd' || lastChar == 's' ||
        lastChar == 'g' || lastChar == 'e') {
      type = lastChar;
      if (formatSpec.length() > 1) {
        // Parse precision from remaining part
        std::string precStr = formatSpec.substr(0, formatSpec.length() - 1);
        if (!precStr.empty() && precStr[0] == '.') {
          if (precStr.length() > 1) {
            try {
              precision = std::stoi(precStr.substr(1));
            } catch (...) {
              precision = -1;
            }
          }
        }
      }
    } else if (formatSpec[0] == '.') {
      // Just precision like .4 or .2f handled above
      try {
        precision = std::stoi(formatSpec.substr(1));
      } catch (...) {
        precision = -1;
      }
    }
  }

  // Format based on value type
  if (value.is<double>()) {
    double num = value.get<double>();
    if (type == 'f' || precision >= 0) {
      int prec = precision >= 0 ? precision : 6;
      char buf[64];
      snprintf(buf, sizeof(buf), "%.*f", prec, num);
      result = buf;
    } else if (type == 'e') {
      char buf[64];
      snprintf(buf, sizeof(buf), "%e", num);
      result = buf;
    } else if (type == 'g') {
      // Default: nice formatting without trailing zeros
      result = ValueToString(value);
    } else {
      result = std::to_string(static_cast<long long>(num));
    }
  } else if (value.is<int>()) {
    int num = value.get<int>();
    if (type == 'f') {
      int prec = precision >= 0 ? precision : 6;
      char buf[64];
      snprintf(buf, sizeof(buf), "%.*f", prec, static_cast<double>(num));
      result = buf;
    } else {
      result = std::to_string(num);
    }
  } else {
    // For strings and other types, just use ValueToString
    result = ValueToString(value);
  }

  return result;
}

bool Interpreter::ExecResultToBool(const HavelResult &result) {
  if (auto *val = std::get_if<HavelValue>(&result)) {
    return ValueToBool(*val);
  }
  // For control flow types (return, break, continue) and errors, return false
  return false;
}

bool Interpreter::ValueToBool(const HavelValue &value) {
  return std::visit(
      [](auto &&arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>)
          return false;
        else if constexpr (std::is_same_v<T, bool>)
          return arg;
        else if constexpr (std::is_same_v<T, int>)
          return arg != 0;
        else if constexpr (std::is_same_v<T, double>)
          return arg != 0.0;
        else if constexpr (std::is_same_v<T, std::string>)
          return !arg.empty();
        else
          return true; // Functions, objects, arrays are truthy
      },
      value.data); // Use .data member for std::visit
}

double Interpreter::ValueToNumber(const HavelValue &value) {
  return std::visit(
      [](auto &&arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>)
          return 0.0;
        else if constexpr (std::is_same_v<T, bool>)
          return arg ? 1.0 : 0.0;
        else if constexpr (std::is_same_v<T, int>)
          return static_cast<double>(arg);
        else if constexpr (std::is_same_v<T, double>)
          return arg;
        else if constexpr (std::is_same_v<T, std::string>) {
          try {
            return std::stod(arg);
          } catch (...) {
            return 0.0;
          }
        }
        return 0.0;
      },
      value.data); // Use .data member for std::visit
}

// Constructor with HostContext
Interpreter::Interpreter(HostContext ctx, const std::vector<std::string> &cli_args)
    : hostContext(std::move(ctx)), lastResult(HavelValue(nullptr)), cliArgs(cli_args),
      m_destroyed(std::make_shared<std::atomic<bool>>(false)) {
  info("Interpreter constructor called");
  environment = std::make_shared<Environment>();
  environment->Define("constructor_called", HavelValue(true));

  // Initialize runtime services
  services.setInput(hostContext.io);
  services.createCallDispatcher(this);
  services.createMemberResolver(this);

  // Set global interpreter reference for hotkey callbacks
  havel::modules::SetHotkeyInterpreter(this);
  info("Set hotkey interpreter to {}", (void*)this);

  // Load all host modules (includes io module with keyTap, etc.)
  havel::modules::loadHostModules(*environment, this);
}

// Minimal interpreter for pure script execution (no IO/hotkeys/display)
Interpreter::Interpreter(const std::vector<std::string> &cli_args)
    : hostContext(), lastResult(HavelValue(nullptr)), cliArgs(cli_args),
      m_destroyed(std::make_shared<std::atomic<bool>>(false)) {
  KeyMap::Initialize();

  info("Minimal Interpreter created (pure mode - no IO/hotkeys)");
  environment = std::make_shared<Environment>();
  environment->Define("constructor_called", HavelValue(true));
  environment->Define("__pure_mode__", HavelValue(true));
  
  // Set global interpreter reference for hotkey callbacks
  havel::modules::SetHotkeyInterpreter(this);
  
  havel::modules::loadHostModules(*environment, this);
}

HavelResult Interpreter::Execute(const std::string &sourceCode) {
  std::lock_guard<std::mutex> lock(interpreterMutex);
  try {
    parser::DebugOptions parser_debug;
    parser_debug.lexer = debug.lexer;
    parser_debug.parser = debug.parser;
    parser_debug.ast = debug.ast;
    parser::Parser parser(parser_debug);
    auto program = parser.produceAST(sourceCode);

    // Check for lexer/parser errors
    if (parser.hasErrors()) {
      // Print all errors with source context
      std::ostringstream oss;
      oss << "\n  ╭─ Compilation Errors (" << parser.getErrors().size() << " errors found)\n";
      oss << "  │\n";
      for (const auto& err : parser.getErrors()) {
        std::string sev = (err.severity == ErrorSeverity::Warning) ? "WARNING" : "ERROR";
        oss << "  │ [" << sev << " line " << err.line << ":" << err.column << "] " << err.message << "\n";
        if (!err.sourceLine.empty()) {
          oss << "  │   " << err.sourceLine << "\n";
          oss << "  │   " << std::string(err.column - 1, ' ') << "↑\n";
        }
        oss << "  │\n";
      }
      oss << "  ╰─ Compilation failed\n";
      havel::error(oss.str());
      return HavelRuntimeError("Compilation failed with " + std::to_string(parser.getErrors().size()) + " errors");
    }

    auto *programPtr = program.get();
    // Keep the AST alive to avoid dangling pointers captured in
    // functions/closures
    loadedPrograms.push_back(std::move(program));

    if (debug.ast || showASTOnParse) {
      havel::info("AST: Parsed program:");
      if (programPtr) {
        parser.printAST(*programPtr);
      } else {
        havel::info("AST: (null program)");
      }
    }

    // =========================================================================
    // SEMANTIC ANALYSIS PHASE
    // =========================================================================
    // Now we actually USE the SemanticAnalyzer (it's not just for show anymore!)
    semantic::SemanticAnalyzer semanticAnalyzer;
    
    // Use Basic mode by default - checks variables/functions but NOT modules
    // This avoids false positives for dynamically-loaded modules
    // Set to Strict mode if you want full module checking
    semanticAnalyzer.setMode(semantic::SemanticMode::Basic);
    
    havel::info("Running semantic analysis (mode: Basic)...");
    bool semanticOk = semanticAnalyzer.analyze(*programPtr);
    
    if (!semanticOk) {
      // Print semantic errors
      std::ostringstream oss;
      oss << "\n  ╭─ Semantic Analysis Errors (" << semanticAnalyzer.getErrors().size() << " errors found)\n";
      oss << "  │\n";
      for (const auto& err : semanticAnalyzer.getErrors()) {
        oss << "  │ [ERROR line " << err.line << ":" << err.column << "] " << err.message << "\n";
        oss << "  │\n";
      }
      oss << "  ╰─ Semantic analysis failed\n";
      havel::error(oss.str());
      return HavelRuntimeError("Semantic analysis failed with " + 
                               std::to_string(semanticAnalyzer.getErrors().size()) + " errors");
    }
    
    havel::info("Semantic analysis passed! Symbol table has " + 
                std::to_string(semanticAnalyzer.getSymbolTable().getSymbolCount()) + " symbols");
    // =========================================================================

    // Mark as not first run after initial execution
    bool wasFirstRun = isFirstRun.load();
    auto result = Evaluate(*programPtr);

    // After first successful execution, mark as not first run
    if (wasFirstRun) {
      isFirstRun.store(false);
    }

    return result;
  } catch (const HavelRuntimeError &e) {
    auto& err = const_cast<HavelRuntimeError&>(e);
    if (err.hasLocation) {
      printError(err, sourceCode);
    } else {
      // Try to find the error location by parsing the error message
      havel::error("Runtime error: {}", e.what());
      // Print source code with line numbers for context
      printSourceWithContext(sourceCode, 0);
    }
    return err;
  } catch (const std::exception &e) {
    auto err = HavelRuntimeError(std::string(e.what()));
    havel::error("Runtime error: {}", e.what());
    printSourceWithContext(sourceCode, 0);
    return err;
  }
}

void Interpreter::printSourceWithContext(const std::string& sourceCode, size_t errorLine) {
  std::istringstream iss(sourceCode);
  std::string line;
  std::vector<std::string> lines;
  
  while (std::getline(iss, line)) {
    lines.push_back(line);
  }
  
  if (lines.empty()) return;
  
  // Calculate line number width
  size_t lineNumWidth = std::to_string(lines.size()).length();
  
  // Show context around error line (or all lines if no specific line)
  size_t startLine = (errorLine > 2 && errorLine <= lines.size()) ? errorLine - 2 : 0;
  size_t endLine = (errorLine > 0 && errorLine < lines.size()) ? errorLine + 2 : lines.size();
  
  havel::info("");
  havel::info("  ╭─ Source Code Context");
  for (size_t i = startLine; i < endLine && i < lines.size(); i++) {
    size_t displayLine = i + 1;
    std::string marker = (displayLine == errorLine) ? " >> " : "    ";
    std::string lineIndicator = (displayLine == errorLine) ? "│" : "│";
    
    havel::info("  {}{} {} │ {}", 
                marker, 
                lineIndicator,
                displayLine,
                lines[i]);
    
    if (displayLine == errorLine) {
      havel::info("  │   {} │ {}", 
                  std::string(lineNumWidth, ' '),
                  std::string(lines[i].length(), '^'));
    }
  }
  havel::info("  ╰─");
  havel::info("");
}

std::string Interpreter::formatErrorWithLocation(const std::string& message, size_t line, size_t column, const std::string& sourceCode) {
  std::ostringstream oss;
  
  // Split source into lines
  std::istringstream iss(sourceCode);
  std::string currentLine;
  std::vector<std::string> lines;
  
  while (std::getline(iss, currentLine)) {
    lines.push_back(currentLine);
  }
  
  // Handle edge cases
  if (line == 0 || line > lines.size()) {
    oss << "Error at unknown location: " << message;
    return oss.str();
  }
  
  size_t lineIndex = line - 1; // Convert to 0-based index
  const std::string& errorLine = lines[lineIndex];
  
  // Calculate display line number (handle large line numbers)
  size_t startLine = line > 2 ? line - 2 : 0;
  size_t endLine = std::min(line + 1, lines.size());
  
  // Calculate width for line numbers
  size_t lineNumWidth = std::to_string(endLine).length();
  
  oss << "\n";
  oss << "  ╭─ Error: " << message << "\n";
  oss << "  │\n";
  
  // Print context lines
  for (size_t i = startLine; i < endLine; i++) {
    size_t displayLineNum = i + 1;
    oss << "  │ " << std::setw(lineNumWidth) << displayLineNum << " │ ";
    
    // Show the line content (truncate if too long)
    std::string lineContent = errorLine;
    if (lineContent.length() > 100) {
      lineContent = lineContent.substr(0, 100) + "...";
    }
    oss << lineContent << "\n";
    
    // Add arrow pointer for error line
    if (i == lineIndex) {
      oss << "  │ " << std::string(lineNumWidth, ' ') << "   │ ";
      if (column > 0 && column <= errorLine.length()) {
        oss << std::string(column - 1, ' ') << "↑";
      } else {
        oss << "↑";
      }
      oss << "\n";
    }
  }
  
  oss << "  │\n";
  oss << "  ╰─ at line " << line << ", column " << column << "\n";
  
  return oss.str();
}

void Interpreter::printError(const HavelResult& error, const std::string& sourceCode) {
  if (auto* err = std::get_if<HavelRuntimeError>(&error)) {
    if (err->hasLocation && err->line > 0) {
      std::string formatted = formatErrorWithLocation(err->what(), err->line, err->column, sourceCode);
      std::cerr << formatted << std::endl;
    } else {
      havel::error("Runtime error: {}", err->what());
    }
  }
}

void Interpreter::RegisterHotkeys(const std::string &sourceCode) {
  Execute(sourceCode); // Evaluation now handles hotkey registration
}

HavelResult Interpreter::Evaluate(const ast::ASTNode &node) {
  const_cast<ast::ASTNode &>(node).accept(*this);
  return lastResult;
}

void Interpreter::visitProgram(const ast::Program &node) {
  // Single-pass execution with expression support in config blocks
  // Config sections are executed in order, allowing them to reference
  // variables defined earlier in the script
  HavelValue lastValue = nullptr;
  for (const auto &stmt : node.body) {
    auto result = Evaluate(*stmt);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    if (std::holds_alternative<ReturnValue>(result)) {
      auto ret = std::get<ReturnValue>(result);
      lastResult = ret.value ? *ret.value : HavelValue();
      return;
    }
    lastValue = unwrap(result);
  }
  lastResult = lastValue;
}

void Interpreter::visitLetDeclaration(const ast::LetDeclaration &node) {
  HavelValue value = nullptr;
  if (node.value) {
    auto result = Evaluate(*node.value);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    value = unwrap(result);
  }

  // Handle destructuring patterns
  if (auto *ident = dynamic_cast<const ast::Identifier *>(node.pattern.get())) {
    // Simple variable declaration: let x = value or const x = value
    environment->Define(ident->symbol, value, node.isConst);
    lastResult = value; // Set result for potential chaining
  } else if (auto *arrayPattern =
                 dynamic_cast<const ast::ArrayPattern *>(node.pattern.get())) {
    // Array destructuring: let [a, b] = arr
    if (!node.value) {
      lastResult =
          HavelRuntimeError("Array destructuring requires initialization");
      return;
    }

    if (auto *array = value.get_if<HavelArray>()) {
      if (*array) {
        for (size_t i = 0;
             i < arrayPattern->elements.size() && i < (*array)->size(); ++i) {
          const auto &element = (*array)->at(i);
          const auto &pattern = arrayPattern->elements[i];

          if (auto *ident =
                  dynamic_cast<const ast::Identifier *>(pattern.get())) {
            environment->Define(ident->symbol, element, node.isConst);
          }
          // TODO: Handle nested patterns
        }
      }
    } else {
      lastResult = HavelRuntimeError("Cannot destructure non-array value");
      return;
    }
  } else if (auto *objectPattern =
                 dynamic_cast<const ast::ObjectPattern *>(node.pattern.get())) {
    // Object destructuring: let {x, y} = obj
    if (!node.value) {
      lastResult =
          HavelRuntimeError("Object destructuring requires initialization");
      return;
    }

    if (auto *object = value.get_if<HavelObject>()) {
      if (*object) {
        for (const auto &[key, pattern] : objectPattern->properties) {
          auto it = (*object)->find(key);
          if (it != (*object)->end()) {
            if (auto *ident =
                    dynamic_cast<const ast::Identifier *>(pattern.get())) {
              environment->Define(ident->symbol, it->second);
            }
            // TODO: Handle renamed patterns and nested patterns
          }
        }
      }
    } else {
      lastResult = HavelRuntimeError("Cannot destructure non-object value");
      return;
    }
  }

  lastResult = value;
}

void Interpreter::visitFunctionDeclaration(
    const ast::FunctionDeclaration &node) {
  // Create function with current environment
  auto func = std::make_shared<HavelFunction>(
      HavelFunction{this->environment, // Capture closure
                    &node});
  // Define the function name in the current environment FIRST
  environment->Define(node.name->symbol, func);
  // Then update the function's closure to include itself for recursion
  func->closure = this->environment;
}

void Interpreter::visitFunctionParameter(const ast::FunctionParameter &node) {
  // Parameters are metadata for function construction.
  // Default values are evaluated at CALL time, not definition time.
  // This allows: let a = 5; let f = fn(x = a) {...}; a = 10; f() => x = 10
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitReturnStatement(const ast::ReturnStatement &node) {
  HavelValue value = nullptr;
  if (node.argument) {
    auto result = Evaluate(*node.argument);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    value = unwrap(result);
  }
  lastResult = ReturnValue{std::make_shared<HavelValue>(value)};
}

void Interpreter::visitIfStatement(const ast::IfStatement &node) {
  auto conditionResult = Evaluate(*node.condition);
  if (isError(conditionResult)) {
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

void Interpreter::visitBlockStatement(const ast::BlockStatement &node) {
  auto blockEnv = std::make_shared<Environment>(this->environment);
  auto originalEnv = this->environment;
  this->environment = blockEnv;

  HavelResult blockResult = HavelValue(nullptr);
  for (const auto &stmt : node.body) {
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

void Interpreter::visitBlockExpression(const ast::BlockExpression &node) {
  auto blockEnv = std::make_shared<Environment>(this->environment);
  auto originalEnv = this->environment;
  this->environment = blockEnv;

  // Execute statements
  for (const auto &stmt : node.body) {
    auto result = Evaluate(*stmt);
    if (isError(result) ||
        std::holds_alternative<ReturnValue>(result) ||
        std::holds_alternative<BreakValue>(result) ||
        std::holds_alternative<ContinueValue>(result)) {
      this->environment = originalEnv;
      lastResult = result;
      return;
    }
  }

  // Evaluate final expression (the value of the block)
  if (node.value) {
    auto valueResult = Evaluate(*node.value);
    lastResult = valueResult;
  } else {
    lastResult = HavelValue(nullptr);
  }

  this->environment = originalEnv;
}

void Interpreter::visitIfExpression(const ast::IfExpression &node) {
  auto conditionResult = Evaluate(*node.condition);
  if (isError(conditionResult)) {
    lastResult = conditionResult;
    return;
  }

  bool conditionMet = Interpreter::ValueToBool(unwrap(conditionResult));

  if (conditionMet) {
    lastResult = Evaluate(*node.thenBranch);
  } else if (node.elseBranch) {
    lastResult = Evaluate(*node.elseBranch);
  } else {
    lastResult = HavelValue(nullptr);  // No else branch, return null
  }
}

void Interpreter::visitHotkeyBinding(const ast::HotkeyBinding &node) {
  if (node.hotkeys.empty()) {
    lastResult = HavelRuntimeError("Hotkey binding has no hotkeys");
    return;
  }

  // Do NOT evaluate the action immediately - only register it for later
  // execution Keep the action node alive for runtime hotkey execution
  auto action = node.action.get();

  // Build condition lambdas from the conditions vector
  std::vector<std::function<bool()>> contextChecks;
  for (const auto &condition : node.conditions) {
    size_t spacePos = condition.find(' ');
    if (spacePos != std::string::npos) {
      std::string condType = condition.substr(0, spacePos);
      std::string condValue = condition.substr(spacePos + 1);

      if (condType == "mode") {
        contextChecks.push_back([this, condValue]() {
          // Check if the current mode matches the condition value
          auto modeVal = environment->Get("mode");
          if (modeVal && modeVal->isString()) {
            return modeVal->asString() == condValue;
          }
          // If mode is not set or is not a string, default to false
          return false;
        });
      } else if (condType == "title") {
        contextChecks.push_back([this, condValue]() {
          std::string activeTitle = hostContext.windowManager->GetActiveWindowTitle();
          return activeTitle.find(condValue) != std::string::npos;
        });
      } else if (condType == "class") {
        contextChecks.push_back([this, condValue]() {
          std::string activeClass = hostContext.windowManager->GetActiveWindowClass();
          return activeClass.find(condValue) != std::string::npos;
        });
      } else if (condType == "process") {
        contextChecks.push_back([this, condValue]() {
          pID pid = hostContext.windowManager->GetActiveWindowPID();
          std::string processName = WindowManager::getProcessName(pid);
          return processName.find(condValue) != std::string::npos;
        });
      }
    }
  }

  // Create shared action handler for all hotkeys
  auto actionHandler = [this, action, contextChecks]() {
    // Check all conditions before executing
    for (const auto &check : contextChecks) {
      if (!check()) {
        return; // Condition not met
      }
    }

    if (action) {
      // Lock interpreter mutex to protect environment and lastResult
      std::lock_guard<std::mutex> lock(this->interpreterMutex);
      
      // Evaluate the action (could be a lambda or expression)
      auto actionResult = this->Evaluate(*action);
      
      // Check for errors first
      if (isError(actionResult)) {
        std::cerr << "Runtime error in hotkey: " << getErrorMessage(actionResult)
                  << std::endl;
        return;
      }
      
      // Unwrap the result to get the actual value
      HavelValue funcValue = unwrap(actionResult);
      
      // If the result is a function, CALL it
      if (funcValue.isFunction()) {
        auto callResult = this->CallFunction(funcValue, {});
        if (isError(callResult)) {
          std::cerr << "Runtime error in hotkey: " << getErrorMessage(callResult)
                    << std::endl;
        }
      }
      // If not a function, the action was an expression - result is already computed
    }
  };

  // Register ALL hotkeys with the same action handler
  for (const auto &hotkeyExpr : node.hotkeys) {
    auto hotkeyLiteral =
        dynamic_cast<const ast::HotkeyLiteral *>(hotkeyExpr.get());
    if (!hotkeyLiteral) {
      std::cerr
          << "Warning: Skipping non-literal hotkey in multi-hotkey binding\n";
      continue;
    }

    std::string hotkey = hotkeyLiteral->combination;
    if (hostContext.io) {
      hostContext.io->Hotkey(hotkey, actionHandler);
    } else {
      // In pure mode, hotkeys cannot be registered
      std::cerr << "Warning: Hotkey '" << hotkey << "' registered but IO not available (pure mode)\n";
    }
  }

  // Return null after registering the hotkey
  lastResult = nullptr;
}
void Interpreter::visitExpressionStatement(
    const ast::ExpressionStatement &node) {
  lastResult = Evaluate(*node.expression);
}

void Interpreter::visitSleepStatement(const ast::SleepStatement &node) {
  // Parse duration using the same logic as sleep() builtin
  long long ms = 0;
  
  // Try to parse as number first
  try {
    ms = std::stoll(node.duration);
  } catch (...) {
    // Use the duration string parser
    ms = 0;
    
    // Try HH:MM:SS.mmm format
    std::regex timeRegex(R"((\d+):(\d+):(\d+)(?:\.(\d+))?)");
    std::smatch timeMatch;
    if (std::regex_match(node.duration, timeMatch, timeRegex)) {
      long long hours = std::stoll(timeMatch[1].str());
      long long minutes = std::stoll(timeMatch[2].str());
      long long seconds = std::stoll(timeMatch[3].str());
      long long millis = 0;
      if (timeMatch[4].matched) {
        std::string msStr = timeMatch[4].str();
        while (msStr.length() < 3) msStr += "0";
        millis = std::stoll(msStr.substr(0, 3));
      }
      ms = ((hours * 3600 + minutes * 60 + seconds) * 1000) + millis;
    } else {
      // Try unit-based format
      std::regex unitRegex(R"((\d+)(ms|s|m|h|d|w))", std::regex::icase);
      auto begin = std::sregex_iterator(node.duration.begin(), node.duration.end(), unitRegex);
      auto end = std::sregex_iterator();
      
      for (auto it = begin; it != end; ++it) {
        long long value = std::stoll((*it)[1].str());
        std::string unit = (*it)[2].str();
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
        
        if (unit == "ms") ms += value;
        else if (unit == "s") ms += value * 1000;
        else if (unit == "m" || unit == "min") ms += value * 60 * 1000;
        else if (unit == "h") ms += value * 3600 * 1000;
        else if (unit == "d") ms += value * 24 * 3600 * 1000;
        else if (unit == "w") ms += value * 7 * 24 * 3600 * 1000;
      }
    }
  }
  
  if (ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitRepeatStatement(const ast::RepeatStatement &node) {
  // Evaluate count expression
  auto countResult = Evaluate(*node.countExpr);
  if (isError(countResult)) {
    lastResult = countResult;
    return;
  }
  int count = static_cast<int>(ValueToNumber(unwrap(countResult)));
  
  // Execute body 'count' times
  for (int i = 0; i < count; i++) {
    if (node.body) {
      Evaluate(*node.body);
    }
  }
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitBacktickExpression(const ast::BacktickExpression &node) {
  // Execute shell command and capture output using Launcher
  havel::ProcessResult result = havel::Launcher::runShell(node.command);

  // Return structured ProcessResult as an object
  auto resultObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  (*resultObj)["stdout"] = HavelValue(result.stdout);
  (*resultObj)["stderr"] = HavelValue(result.stderr);
  (*resultObj)["exitCode"] = HavelValue(static_cast<double>(result.exitCode));
  (*resultObj)["success"] = HavelValue(result.success);
  (*resultObj)["error"] = HavelValue(result.error);

  lastResult = HavelValue(resultObj);
}

void Interpreter::visitShellCommandExpression(const ast::ShellCommandExpression &node) {
  // Evaluate command expression to support variables, arrays, etc.
  auto cmdResult = Evaluate(*node.commandExpr);
  if (isError(cmdResult)) {
    lastResult = cmdResult;
    return;
  }

  HavelValue cmdValue = unwrap(cmdResult);
  havel::ProcessResult result;

  // Check if command is an array (argument vector) or string
  if (cmdValue.isArray()) {
    // Array mode: ["cmd", "arg1", "arg2"] - execute without shell
    auto argsArray = cmdValue.asArray();
    if (argsArray && !argsArray->empty()) {
      std::vector<std::string> args;
      for (size_t i = 0; i < argsArray->size(); ++i) {
        args.push_back(ValueToString((*argsArray)[i]));
      }
      result = havel::Launcher::run(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    } else {
      lastResult = HavelRuntimeError("Shell command array is empty");
      return;
    }
  } else {
    // String mode: execute through shell
    std::string command = ValueToString(cmdValue);
    result = havel::Launcher::runShell(command);
  }

  // Return stdout (capture mode is implicit for expressions)
  lastResult = HavelValue(result.stdout);
}

void Interpreter::visitShellCommandStatement(const ast::ShellCommandStatement &node) {
  // Execute command chain (support pipes: $! cmd1 | cmd2 | cmd3)
  const ast::ShellCommandStatement* current = &node;
  std::string inputStdin;  // Stdin from previous command in pipe
  int pipeStage = 0;  // Unique identifier for each stage in the pipe
  
  havel::ProcessResult result;
  
  while (current != nullptr) {
    // Evaluate command expression
    auto cmdResult = Evaluate(*current->commandExpr);
    if (isError(cmdResult)) {
      lastResult = cmdResult;
      return;
    }

    HavelValue cmdValue = unwrap(cmdResult);

    // Check if command is an array (argument vector) or string
    if (cmdValue.isArray()) {
      // Array mode: ["cmd", "arg1", "arg2"] - execute without shell
      auto argsArray = cmdValue.asArray();
      if (argsArray && !argsArray->empty()) {
        std::vector<std::string> args;
        for (size_t i = 0; i < argsArray->size(); ++i) {
          args.push_back(ValueToString((*argsArray)[i]));
        }
        
        // If there's stdin from previous command in pipe, use shell
        if (!inputStdin.empty()) {
          std::string cmd = args[0];
          for (size_t i = 1; i < args.size(); ++i) {
            cmd += " " + args[i];
          }
          result = havel::Launcher::runShell(cmd);
        } else {
          result = havel::Launcher::run(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
        }
      } else {
        lastResult = HavelRuntimeError("Shell command array is empty");
        return;
      }
    } else {
      // String mode: execute through shell
      std::string command = ValueToString(cmdValue);
      
      // If there's stdin from previous command, pipe it
      if (!inputStdin.empty()) {
        // Write stdin to a temp file with unique name
        std::string tempFile = "/tmp/havel_pipe_" + std::to_string(getpid()) + "_" + std::to_string(pipeStage++);
        std::ofstream ofs(tempFile);
        ofs << inputStdin;
        ofs.close();
        
        command = "cat " + tempFile + " | " + command;
        result = havel::Launcher::runShell(command);
        std::remove(tempFile.c_str());
      } else {
        result = havel::Launcher::runShell(command);
      }
    }
    
    // Prepare stdin for next command in chain
    inputStdin = result.stdout;
    
    // Move to next command in pipe chain
    current = current->next.get();
  }

  // Return exit code (shell semantics)
  // 0 = success, non-zero = failure
  // Also forward stdout/stderr
  if (!result.stdout.empty()) {
    std::cout << result.stdout;
  }
  if (!result.stderr.empty()) {
    std::cerr << result.stderr;
  }
  lastResult = HavelValue(static_cast<double>(result.exitCode));
}

void Interpreter::visitInputStatement(const ast::InputStatement &node) {
  for (const auto &cmd : node.commands) {
    switch (cmd.type) {
      case ast::InputCommand::SendText:
        hostContext.io->Send(cmd.text.c_str());
        break;

      case ast::InputCommand::SendKey:
        hostContext.io->Send(("{ " + cmd.key + " }").c_str());
        break;

      case ast::InputCommand::MouseClick:
        if (cmd.text == "left" || cmd.text == "lmb") {
          hostContext.io->MouseClick(1);
        } else if (cmd.text == "right" || cmd.text == "rmb") {
          hostContext.io->MouseClick(2);
        } else if (cmd.text == "middle" || cmd.text == "mmb") {
          hostContext.io->MouseClick(3);
        } else if (cmd.text == "side1" || cmd.text == "btn4") {
          hostContext.io->MouseClick(4);
        } else if (cmd.text == "side2" || cmd.text == "btn5") {
          hostContext.io->MouseClick(5);
        }
        break;

      case ast::InputCommand::MouseMove: {
        double x = 0, y = 0, speed = 1.0, accel = 1.0;
        if (!cmd.xExprStr.empty()) {
          try { x = std::stod(cmd.xExprStr); } catch (...) {}
        }
        if (!cmd.yExprStr.empty()) {
          try { y = std::stod(cmd.yExprStr); } catch (...) {}
        }
        if (!cmd.speedExprStr.empty()) {
          try { speed = std::stod(cmd.speedExprStr); } catch (...) {}
        }
        if (!cmd.accelExprStr.empty()) {
          try { accel = std::stod(cmd.accelExprStr); } catch (...) {}
        }
        hostContext.io->MouseMoveTo(static_cast<int>(x), static_cast<int>(y), speed, accel);
        break;
      }

      case ast::InputCommand::MouseRelative: {
        double x = 0, y = 0, speed = 1.0, accel = 1.0;
        if (!cmd.xExprStr.empty()) {
          try { x = std::stod(cmd.xExprStr); } catch (...) {}
        }
        if (!cmd.yExprStr.empty()) {
          try { y = std::stod(cmd.yExprStr); } catch (...) {}
        }
        if (!cmd.speedExprStr.empty()) {
          try { speed = std::stod(cmd.speedExprStr); } catch (...) {}
        }
        if (!cmd.accelExprStr.empty()) {
          try { accel = std::stod(cmd.accelExprStr); } catch (...) {}
        }
        hostContext.io->MouseMove(static_cast<int>(x), static_cast<int>(y), speed, accel);
        break;
      }

      case ast::InputCommand::MouseWheel: {
        double x = 0, y = 0;
        if (!cmd.xExprStr.empty()) {
          try { x = std::stod(cmd.xExprStr); } catch (...) {}
        }
        if (!cmd.yExprStr.empty()) {
          try { y = std::stod(cmd.yExprStr); } catch (...) {}
        }
        // Use IO::Scroll for wheel events (dy, dx)
        hostContext.io->Scroll(y, x);
        break;
      }

      case ast::InputCommand::MouseClickAt: {
        double x = 0, y = 0, speed = 1.0, accel = 1.0;
        int button = 1; // Default to left click
        if (!cmd.xExprStr.empty()) {
          try { x = std::stod(cmd.xExprStr); } catch (...) {}
        }
        if (!cmd.yExprStr.empty()) {
          try { y = std::stod(cmd.yExprStr); } catch (...) {}
        }
        if (!cmd.speedExprStr.empty()) {
          try { speed = std::stod(cmd.speedExprStr); } catch (...) {}
        }
        if (!cmd.accelExprStr.empty()) {
          try { accel = std::stod(cmd.accelExprStr); } catch (...) {}
        }
        // Parse button string
        if (!cmd.buttonExprStr.empty()) {
          std::string btn = cmd.buttonExprStr;
          if (btn == "left" || btn == "lmb" || btn == "1") button = 1;
          else if (btn == "right" || btn == "rmb" || btn == "2") button = 2;
          else if (btn == "middle" || btn == "mmb" || btn == "3") button = 3;
          else if (btn == "side1" || btn == "btn4" || btn == "4") button = 4;
          else if (btn == "side2" || btn == "btn5" || btn == "5") button = 5;
          else {
            try { button = std::stoi(btn); } catch (...) {}
          }
        }
        // Move to position and click
        hostContext.io->MouseMoveTo(static_cast<int>(x), static_cast<int>(y), speed, accel);
        hostContext.io->MouseClick(button);
        break;
      }

      case ast::InputCommand::Sleep: {
        long long ms = 0;
        try {
          ms = std::stoll(cmd.duration);
        } catch (...) {
          // Use duration parser (simplified)
          ms = 0;
          std::regex unitRegex(R"((\d+)(ms|s|m|h))", std::regex::icase);
          auto begin = std::sregex_iterator(cmd.duration.begin(), cmd.duration.end(), unitRegex);
          auto end = std::sregex_iterator();
          for (auto it = begin; it != end; ++it) {
            long long value = std::stoll((*it)[1].str());
            std::string unit = (*it)[2].str();
            std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
            if (unit == "ms") ms += value;
            else if (unit == "s") ms += value * 1000;
            else if (unit == "m" || unit == "min") ms += value * 60 * 1000;
            else if (unit == "h") ms += value * 3600 * 1000;
          }
        }
        if (ms > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        break;
      }
    }
  }
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitBinaryExpression(const ast::BinaryExpression &node) {
  auto leftRes = Evaluate(*node.left);
  if (isError(leftRes)) {
    lastResult = leftRes;
    return;
  }
  auto rightRes = Evaluate(*node.right);
  if (isError(rightRes)) {
    lastResult = rightRes;
    return;
  }

  HavelValue left = unwrap(leftRes);
  HavelValue right = unwrap(rightRes);

  switch (node.operator_) {
  case ast::BinaryOperator::Add:
    if (left.isString() || right.isString()) {
      std::string result = ValueToString(left) + ValueToString(right);
      lastResult = HavelValue(result);
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
    if (ValueToNumber(right) == 0.0) {
      lastResult = HavelRuntimeError("Division by zero", node.line, node.column);
      return;
    }
    lastResult = ValueToNumber(left) / ValueToNumber(right);
    break;
  case ast::BinaryOperator::Mod:
    if (ValueToNumber(right) == 0.0) {
      lastResult = HavelRuntimeError("Modulo by zero", node.line, node.column);
      return;
    }
    lastResult = static_cast<int>(ValueToNumber(left)) %
                 static_cast<int>(ValueToNumber(right));
    break;
  case ast::BinaryOperator::Equal:
    lastResult = HavelValue(ValueToString(left) == ValueToString(right));
    break;
  case ast::BinaryOperator::NotEqual:
    lastResult = HavelValue(ValueToString(left) != ValueToString(right));
    break;
  case ast::BinaryOperator::Less:
    lastResult = HavelValue(ValueToNumber(left) < ValueToNumber(right));
    break;
  case ast::BinaryOperator::Greater:
    lastResult = HavelValue(ValueToNumber(left) > ValueToNumber(right));
    break;
  case ast::BinaryOperator::LessEqual:
    lastResult = HavelValue(ValueToNumber(left) <= ValueToNumber(right));
    break;
  case ast::BinaryOperator::GreaterEqual:
    lastResult = HavelValue(ValueToNumber(left) >= ValueToNumber(right));
    break;
  case ast::BinaryOperator::And:
    lastResult = HavelValue(ValueToBool(left) && ValueToBool(right));
    break;
  case ast::BinaryOperator::Or:
    lastResult = HavelValue(ValueToBool(left) || ValueToBool(right));
    break;
  case ast::BinaryOperator::ConfigAppend: {
    // >> config operator
    // value >> config.path - SET config value
    // config.path >> variable - GET config value
    // { block } >> config - SET config from object
    auto& config = Configs::Get();

    if (right.isObject()) {
      // value >> {config} - merge object into config
      auto rightObj = right.asObject();
      if (rightObj) {
        for (const auto& [key, val] : *rightObj) {
          if (val.isString()) {
            config.Set("General." + key, val.asString(), false);
          } else if (val.isNumber()) {
            config.Set("General." + key, val.asNumber(), false);
          } else if (val.isBool()) {
            config.Set("General." + key, val.asBool(), false);
          }
        }
        config.EndBatch();  // Save all changes
        lastResult = right;
      } else {
        lastResult = HavelRuntimeError("Config append requires valid object");
      }
    } else if (right.isString()) {
      // value >> "config.key" - SET config value
      std::string configKey = right.asString();

      if (left.isString()) {
        config.Set(configKey, left.asString(), true);
        lastResult = left;
      } else if (left.isNumber()) {
        config.Set(configKey, left.asNumber(), true);
        lastResult = left;
      } else if (left.isBool()) {
        config.Set(configKey, left.asBool(), true);
        lastResult = left;
      } else if (left.isObject()) {
        // Object >> "config" - merge object into config
        auto leftObj = left.asObject();
        if (leftObj) {
          for (const auto& [key, val] : *leftObj) {
            std::string fullKey = configKey.empty() ? key : configKey + "." + key;
            if (val.isString()) {
              config.Set(fullKey, val.asString(), false);
            } else if (val.isNumber()) {
              config.Set(fullKey, val.asNumber(), false);
            } else if (val.isBool()) {
              config.Set(fullKey, val.asBool(), false);
            }
          }
          config.EndBatch();
        }
        lastResult = left;
      } else {
        lastResult = HavelRuntimeError("Config value must be string, number, bool, or object");
      }
    } else {
      // config.path >> variable - GET config value (reverse direction)
      // This handles: brightness.defaultBrightness >> var
      std::string configKey;
      if (right.isString()) {
        configKey = right.asString();
      } else {
        lastResult = HavelRuntimeError("Config get requires string key on right side");
        break;
      }

      // Get value from config
      auto configVal = config.Get<std::string>(configKey, "");
      if (!configVal.empty()) {
        lastResult = HavelValue(configVal);
      } else {
        auto configNum = config.Get<double>(configKey, 0.0);
        if (configNum != 0.0) {
          lastResult = HavelValue(configNum);
        } else {
          lastResult = HavelValue(nullptr);  // Config key not found
        }
      }
    }
    break;
  }
  default:
    lastResult = HavelRuntimeError("Unsupported binary operator");
  }
}

void Interpreter::visitUnaryExpression(const ast::UnaryExpression &node) {
  auto operandRes = Evaluate(*node.operand);
  if (isError(operandRes)) {
    lastResult = operandRes;
    return;
  }
  HavelValue operand = unwrap(operandRes);

  switch (node.operator_) {
  case ast::UnaryExpression::UnaryOperator::Not:
    lastResult = !ValueToBool(operand);
    break;
  case ast::UnaryExpression::UnaryOperator::Minus:
    lastResult = -ValueToNumber(operand);
    break;
  case ast::UnaryExpression::UnaryOperator::Plus:
    lastResult = ValueToNumber(operand);
    break;
  default:
    lastResult = HavelRuntimeError("Unsupported unary operator");
  }
}

void Interpreter::visitUpdateExpression(const ast::UpdateExpression &node) {
  // Determine the variable or property being updated
  if (auto *id = dynamic_cast<const ast::Identifier *>(node.argument.get())) {
    auto currentValOpt = environment->Get(id->symbol);
    if (!currentValOpt) {
      lastResult = HavelRuntimeError("Undefined variable: " + id->symbol, node.line, node.column);
      return;
    }

    double currentNum = ValueToNumber(*currentValOpt);
    double newNum =
        (node.operator_ == ast::UpdateExpression::Operator::Increment)
            ? currentNum + 1.0
            : currentNum - 1.0;

    environment->Assign(id->symbol, newNum);

    lastResult = node.isPrefix ? newNum : currentNum;
    return;
  }

  // Member expression (obj.prop++)
  if (auto *member =
          dynamic_cast<const ast::MemberExpression *>(node.argument.get())) {
    auto objectResult = Evaluate(*member->object);
    if (isError(objectResult)) {
      lastResult = objectResult;
      return;
    }
    HavelValue objectValue = unwrap(objectResult);

    auto *propId =
        dynamic_cast<const ast::Identifier *>(member->property.get());
    if (!propId) {
      lastResult =
          HavelRuntimeError("Invalid property access in update expression");
      return;
    }
    std::string propName = propId->symbol;

    if (auto *objPtr = objectValue.get_if<HavelObject>()) {
      if (*objPtr) {
        auto &obj = **objPtr;
        auto it = obj.find(propName);
        double currentNum = 0.0;
        if (it != obj.end()) {
          currentNum = ValueToNumber(it->second);
        }

        double newNum =
            (node.operator_ == ast::UpdateExpression::Operator::Increment)
                ? currentNum + 1.0
                : currentNum - 1.0;

        obj[propName] = newNum;
        lastResult = node.isPrefix ? newNum : currentNum;
        return;
      }
    }
    lastResult = HavelRuntimeError("Cannot update property of non-object");
    return;
  }

  lastResult = HavelRuntimeError("Invalid update target");
}

void Interpreter::visitCallExpression(const ast::CallExpression &node) {
  auto calleeRes = Evaluate(*node.callee);
  if (isError(calleeRes)) {
    lastResult = calleeRes;
    return;
  }
  HavelValue callee = unwrap(calleeRes);

  std::vector<HavelValue> args;
  for (const auto &arg : node.args) {
    auto argRes = Evaluate(*arg);
    if (isError(argRes)) {
      lastResult = argRes;
      return;
    }

    // Check if this is a spread expression
    if (dynamic_cast<const ast::SpreadExpression *>(arg.get())) {
      auto value = unwrap(argRes);
      // Spread arrays - flatten elements into args
      if (auto *arrPtr = value.get_if<HavelArray>()) {
        if (*arrPtr) {
          for (const auto &item : **arrPtr) {
            args.push_back(item);
          }
        }
      }
      // Spread objects - use values sorted by key (for {x, y} style objects)
      else if (auto *objPtr = value.get_if<HavelObject>()) {
        if (*objPtr) {
          // Sort keys to ensure consistent order (x before y, etc.)
          std::vector<std::pair<std::string, HavelValue>> sortedPairs((*objPtr)->begin(), (*objPtr)->end());
          std::sort(sortedPairs.begin(), sortedPairs.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
          for (const auto &pair : sortedPairs) {
            args.push_back(pair.second);
          }
        }
      }
      else {
        // Non-array/object spread - add as single arg
        args.push_back(value);
      }
    } else {
      args.push_back(unwrap(argRes));
    }
  }

  if (auto *builtin = callee.get_if<BuiltinFunction>()) {
    lastResult = (*builtin)(args);
  } else if (auto *userFunc = callee.get_if<std::shared_ptr<HavelFunction>>()) {
    auto &func = *userFunc;
    if (args.size() != func->declaration->parameters.size()) {
      lastResult = HavelRuntimeError("Mismatched argument count for function " +
                                     func->declaration->name->symbol);
      return;
    }

    auto funcEnv = std::make_shared<Environment>(func->closure);
    for (size_t i = 0; i < args.size(); ++i) {
      funcEnv->Define(func->declaration->parameters[i]->paramName->symbol, args[i]);
    }

    auto originalEnv = this->environment;
    this->environment = funcEnv;
    auto bodyResult = Evaluate(*func->declaration->body);
    this->environment = originalEnv;

    if (std::holds_alternative<ReturnValue>(bodyResult)) {
      auto ret = std::get<ReturnValue>(bodyResult);
      lastResult = ret.value ? *ret.value : HavelValue();
    } else if (isError(bodyResult)) {
      lastResult = bodyResult;
    } else {
      lastResult = unwrap(bodyResult); // Implicit return - use body result
    }
  } else if (auto *objPtr = callee.get_if<HavelObject>()) {
    // Check for __call__ method (for struct type constructors)
    if (*objPtr) {
      auto it = (*objPtr)->find("__call__");
      if (it != (*objPtr)->end() && it->second.is<BuiltinFunction>()) {
        auto callFunc = it->second.get<BuiltinFunction>();
        lastResult = callFunc(args);
        return;
      }
    }
    lastResult = HavelRuntimeError("Attempted to call a non-callable value: " +
                                   ValueToString(callee),
                                   node.line, node.column);
  } else {
    lastResult = HavelRuntimeError("Attempted to call a non-callable value: " +
                                   ValueToString(callee),
                                   node.line, node.column);
  }
}

// CallFunction - Execute a function value (for hotkey callbacks)
HavelResult Interpreter::CallFunction(const HavelValue& func, const std::vector<HavelValue>& args) {
    if (auto* builtin = func.get_if<BuiltinFunction>()) {
        return (*builtin)(args);
    } else if (auto* userFunc = func.get_if<std::shared_ptr<HavelFunction>>()) {
        auto& f = **userFunc;
        
        // Create new environment with function's closure
        auto funcEnv = std::make_shared<Environment>(f.closure);
        for (size_t i = 0; i < args.size() && i < f.declaration->parameters.size(); ++i) {
            funcEnv->Define(f.declaration->parameters[i]->paramName->symbol, args[i]);
        }
        
        // Save and switch environment
        auto originalEnv = this->environment;
        this->environment = funcEnv;
        
        // Execute function body
        auto bodyResult = Evaluate(*f.declaration->body);
        
        // Restore environment
        this->environment = originalEnv;
        
        // Handle return value
        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            auto ret = std::get<ReturnValue>(bodyResult);
            return ret.value ? *ret.value : HavelValue();
        } else if (isError(bodyResult)) {
            return bodyResult;
        } else {
            return unwrap(bodyResult); // Implicit return
        }
    } else {
        return HavelRuntimeError("Attempted to call a non-callable value: " + ValueToString(func));
    }
}

void Interpreter::visitMemberExpression(const ast::MemberExpression &node) {
  auto objectResult = Evaluate(*node.object);
  if (isError(objectResult)) {
    lastResult = objectResult;
    return;
  }
  HavelValue objectValue = unwrap(objectResult);

  auto *propId = dynamic_cast<const ast::Identifier *>(node.property.get());
  if (!propId) {
    lastResult = HavelRuntimeError("Invalid property access");
    return;
  }
  std::string propName = propId->symbol;

  // Objects: o.b
  if (auto *objPtr = objectValue.get_if<HavelObject>()) {
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

  // Arrays: special properties like length and methods
  if (auto *arrPtr = objectValue.get_if<HavelArray>()) {
    if (propName == "length") {
      lastResult = static_cast<double>((*arrPtr) ? (*arrPtr)->size() : 0);
      return;
    }
    // Check for array methods (push, pop, etc.)
    std::optional<HavelValue> methodValOpt = environment->Get(propName);
    if (methodValOpt && methodValOpt->is<BuiltinFunction>()) {
      auto builtin = methodValOpt->get<BuiltinFunction>();
      // Create a bound function that captures the array as first argument
      auto array = objectValue;  // Capture the array value
      lastResult = HavelValue(BuiltinFunction([array, builtin](const std::vector<HavelValue> &args) -> HavelResult {
        std::vector<HavelValue> boundArgs;
        boundArgs.push_back(array);
        boundArgs.insert(boundArgs.end(), args.begin(), args.end());
        return builtin(boundArgs);
      }));
      return;
    }
  }

  // Strings: methods like lower, upper, replace, etc.
  if (auto *strPtr = objectValue.get_if<std::string>()) {
    std::optional<HavelValue> methodValOpt = environment->Get(propName);
    if (methodValOpt && methodValOpt->is<BuiltinFunction>()) {
      auto builtin = methodValOpt->get<BuiltinFunction>();
      // Create a bound function that captures the string as first argument
      auto str = objectValue;  // Capture the string value
      lastResult = HavelValue(BuiltinFunction([str, builtin](const std::vector<HavelValue> &args) -> HavelResult {
        std::vector<HavelValue> boundArgs;
        boundArgs.push_back(str);
        boundArgs.insert(boundArgs.end(), args.begin(), args.end());
        return builtin(boundArgs);
      }));
      return;
    }
  }

  // Struct instances: field access and method binding
  if (auto *structPtr = objectValue.get_if<HavelStructInstance>()) {
    // First check fields
    if (structPtr && structPtr->fields) {
      auto it = structPtr->fields->find(propName);
      if (it != structPtr->fields->end()) {
        lastResult = it->second;
        return;
      }
    }
    // Then check methods
    if (structPtr && structPtr->structType) {
      auto method = structPtr->structType->getMethod(propName);
      if (method) {
        // Create a bound method that captures the struct instance as 'this'
        auto instance = objectValue;
        const ast::StructMethodDef* methodPtr = method;  // Capture raw pointer
        lastResult = HavelValue(BuiltinFunction([this, instance, methodPtr](const std::vector<HavelValue> &args) -> HavelResult {
          // Check argument count
          if (args.size() != methodPtr->parameters.size()) {
            return HavelRuntimeError("Method expects " + std::to_string(methodPtr->parameters.size()) + " args but got " + std::to_string(args.size()));
          }
          // Create method environment with 'this' bound
          auto methodEnv = std::make_shared<Environment>(this->environment);
          methodEnv->Define("this", instance);
          // Bind parameters
          for (size_t i = 0; i < methodPtr->parameters.size() && i < args.size(); ++i) {
            methodEnv->Define(methodPtr->parameters[i]->paramName->symbol, args[i]);
          }
          // Execute method body
          auto originalEnv = this->environment;
          this->environment = methodEnv;
          auto res = Evaluate(*methodPtr->body);
          this->environment = originalEnv;
          if (std::holds_alternative<ReturnValue>(res)) {
            auto ret = std::get<ReturnValue>(res);
            return ret.value ? *ret.value : HavelValue();
          }
          return res;
        }));
        return;
      }
      
      // Check trait impls for this type
      auto typeName = structPtr->typeName;
      auto traitImpls = TraitRegistry::getInstance().getImplsForType(typeName);
      for (const auto* impl : traitImpls) {
        auto methodIt = impl->methods.find(propName);
        if (methodIt != impl->methods.end()) {
          // Found trait method - create bound version
          auto instance = objectValue;
          auto traitMethod = methodIt->second.get<BuiltinFunction>();
          lastResult = HavelValue(BuiltinFunction([this, instance, traitMethod](const std::vector<HavelValue> &args) -> HavelResult {
            // Create method environment with 'this' bound
            auto methodEnv = std::make_shared<Environment>(this->environment);
            methodEnv->Define("this", instance);
            auto originalEnv = this->environment;
            this->environment = methodEnv;
            // Call the trait method with instance as first arg
            std::vector<HavelValue> callArgs = args;
            auto res = traitMethod(callArgs);
            this->environment = originalEnv;
            return res;
          }));
          return;
        }
      }
    }
    lastResult = HavelValue(nullptr);
    return;
  }

  lastResult = HavelRuntimeError("Member access not supported for this type");
}

void Interpreter::visitLambdaExpression(const ast::LambdaExpression &node) {
  // Capture current environment (closure)
  auto closureEnv = this->environment;

  // Store parameter info (names and default values)
  struct ParamInfo {
    std::string name;
    const ast::Expression* defaultValue;
  };
  std::vector<ParamInfo> paramInfos;
  for (const auto& param : node.parameters) {
    ParamInfo info;
    info.name = param->paramName->symbol;
    info.defaultValue = param->defaultValue ? param->defaultValue->get() : nullptr;
    paramInfos.push_back(info);
  }

  // Store raw pointer to body - AST lives for entire script execution
  const ast::Statement* bodyPtr = node.body.get();

  // Build a callable that binds args to parameter names and evaluates body
  BuiltinFunction lambda =
      [this, closureEnv, paramInfos, bodyPtr](const std::vector<HavelValue> &args) -> HavelResult {
    // Check argument count (allow fewer args if defaults exist)
    if (args.size() > paramInfos.size()) {
      return HavelRuntimeError("Too many arguments for lambda");
    }
    
    auto funcEnv = std::make_shared<Environment>(closureEnv);
    
    // Bind arguments and apply defaults
    for (size_t i = 0; i < paramInfos.size(); ++i) {
      HavelValue value;
      if (i < args.size()) {
        // Argument provided
        value = args[i];
      } else if (paramInfos[i].defaultValue) {
        // Use default value (evaluated at call time in current environment)
        auto defaultRes = this->Evaluate(*paramInfos[i].defaultValue);
        if (isError(defaultRes)) {
          return defaultRes;
        }
        value = unwrap(defaultRes);
      } else {
        // No default and no argument
        return HavelRuntimeError("Missing argument for parameter '" + paramInfos[i].name + "'");
      }
      funcEnv->Define(paramInfos[i].name, value);
    }
    
    auto originalEnv = this->environment;
    this->environment = funcEnv;
    auto res = Evaluate(*bodyPtr);
    this->environment = originalEnv;
    if (std::holds_alternative<ReturnValue>(res)) {
      auto ret = std::get<ReturnValue>(res);
      return ret.value ? *ret.value : HavelValue();
    }
    return res;
  };
  lastResult = HavelValue(lambda);
}

void Interpreter::visitSetExpression(const ast::SetExpression &node) {
  auto set = std::make_shared<std::vector<HavelValue>>();

  for (const auto &element : node.elements) {
    auto result = Evaluate(*element);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    set->push_back(unwrap(result));
  }

  lastResult = HavelValue(HavelSet(set));
}

void Interpreter::visitArrayPattern(const ast::ArrayPattern &node) {
  // TODO: Implement array pattern matching
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitPipelineExpression(const ast::PipelineExpression &node) {
  if (node.stages.empty()) {
    lastResult = HavelValue(nullptr);
    return;
  }

  HavelResult currentResult = Evaluate(*node.stages[0]);
  if (isError(currentResult)) {
    lastResult = currentResult;
    return;
  }

  for (size_t i = 1; i < node.stages.size(); ++i) {
    const auto &stage = node.stages[i];

    HavelValue currentValue = unwrap(currentResult);
    std::vector<HavelValue> args = {currentValue};

    const ast::Expression *calleeExpr = stage.get();
    if (const auto *call =
            dynamic_cast<const ast::CallExpression *>(stage.get())) {
      calleeExpr = call->callee.get();
      for (const auto &arg : call->args) {
        auto argRes = Evaluate(*arg);
        if (isError(argRes)) {
          lastResult = argRes;
          return;
        }
        args.push_back(unwrap(argRes));
      }
    }

    auto calleeRes = Evaluate(*calleeExpr);
    if (isError(calleeRes)) {
      lastResult = calleeRes;
      return;
    }

    HavelValue callee = unwrap(calleeRes);
    if (auto *builtin = callee.get_if<BuiltinFunction>()) {
      currentResult = (*builtin)(args);
    } else if (auto *userFunc =
                   callee.get_if<std::shared_ptr<HavelFunction>>()) {
      // This logic is duplicated from visitCallExpression, could be refactored
      auto &func = *userFunc;
      if (args.size() != func->declaration->parameters.size()) {
        lastResult = HavelRuntimeError(
            "Mismatched argument count for function in pipeline");
        return;
      }
      auto funcEnv = std::make_shared<Environment>(func->closure);
      for (size_t i = 0; i < args.size(); ++i) {
        funcEnv->Define(func->declaration->parameters[i]->paramName->symbol, args[i]);
      }
      auto originalEnv = this->environment;
      this->environment = funcEnv;
      currentResult = Evaluate(*func->declaration->body);
      this->environment = originalEnv;
      if (std::holds_alternative<ReturnValue>(currentResult)) {
        auto ret = std::get<ReturnValue>(currentResult);
        currentResult = ret.value ? *ret.value : HavelValue();
      }

    } else {
      lastResult =
          HavelRuntimeError("Pipeline stage must be a callable function");
      return;
    }

    if (isError(currentResult)) {
      lastResult = currentResult;
      return;
    }
  }
  lastResult = currentResult;
}

void Interpreter::visitImportStatement(const ast::ImportStatement &node) {
  std::string path = node.modulePath;
  HavelObject exports;

  // Special case: no path provided -> import built-in modules by name
  if (path.empty()) {
    for (const auto &item : node.importedItems) {
      const std::string &moduleName = item.first;
      const std::string &alias = item.second;
      auto val = environment->Get(moduleName);
      if (!val || !val->isObject()) {
        lastResult = HavelRuntimeError(
            "Built-in module not found or not an object: " + moduleName);
        return;
      }
      environment->Define(alias, val->asObject());
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
    if (path.rfind("havel:", 0) == 0)
      moduleName = path.substr(6);
    auto moduleVal = environment->Get(moduleName);
    if (moduleVal && moduleVal->isObject()) {
      exports = moduleVal->asObject();
    } else if (!moduleVal) {
      // Load from file
      std::ifstream file(path);
      if (!file) {
        lastResult = HavelRuntimeError("Cannot open module file: " + path);
        return;
      }
      std::string source((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

      // Execute module in a new environment
      // Note: For now, modules use the same IO/windowManager as parent
      Interpreter moduleInterpreter(hostContext);
      auto moduleResult = moduleInterpreter.Execute(source);
      if (isError(moduleResult)) {
        lastResult = moduleResult;
        return;
      }

      HavelValue exportedValue = unwrap(moduleResult);
      if (!exportedValue.isObject()) {
        lastResult = HavelRuntimeError(
            "Module must return an object of exports: " + path);
        return;
      }
      exports = exportedValue.asObject();
    } else {
      lastResult =
          HavelRuntimeError("Built-in module not found: " + moduleName);
      return;
    }
    // Cache the result
    moduleCache[path] = exports;
  }

  // Wildcard import: import * from module
  if (node.importedItems.size() == 1 && node.importedItems[0].first == "*") {
    if (exports) {
      for (const auto &[k, v] : *exports) {
        environment->Define(k, v);
      }
      lastResult = nullptr;
      return;
    }
  }

  // Import symbols into the current environment
  for (const auto &item : node.importedItems) {
    const std::string &originalName = item.first;
    const std::string &alias = item.second;

    if (exports && exports->count(originalName)) {
      environment->Define(alias, exports->at(originalName));
    } else {
      lastResult = HavelRuntimeError(
          "Module '" + path + "' does not export symbol: " + originalName);
      return;
    }
  }

  lastResult = nullptr;
}

void Interpreter::visitUseStatement(const ast::UseStatement &node) {
  // Implementation of use statement for module flattening
  // For each module name, flatten all its functions into the current scope

  for (const std::string &moduleName : node.moduleNames) {
    // Get the module from the environment
    auto moduleVal = environment->Get(moduleName);
    if (!moduleVal) {
      lastResult = HavelRuntimeError("Module not found: " + moduleName);
      return;
    }

    // Check if it's an object (module)
    if (!moduleVal->isObject()) {
      lastResult = HavelRuntimeError("Not a module/object: " + moduleName);
      return;
    }

    auto moduleObj = moduleVal->asObject();
    if (!moduleObj) {
      lastResult = HavelRuntimeError("Module is null: " + moduleName);
      return;
    }

    // Flatten all module functions into the current environment
    for (const auto &[functionName, functionValue] : *moduleObj) {
      environment->Define(functionName, functionValue);
    }
  }

  lastResult = nullptr;
}

void Interpreter::visitWithStatement(const ast::WithStatement &node) {
  // Get the object from the environment
  auto objectVal = environment->Get(node.objectName);
  if (!objectVal) {
    lastResult = HavelRuntimeError("Object not found: " + node.objectName);
    return;
  }

  // Check if it's an object
  if (!objectVal->isObject()) {
    lastResult = HavelRuntimeError("Not an object: " + node.objectName);
    return;
  }

  auto withObject = objectVal->asObject();
  if (!withObject) {
    lastResult = HavelRuntimeError("Object is null: " + node.objectName);
    return;
  }

  // Create a new environment with the object's members available
  auto withEnvironment = std::make_shared<Environment>(environment);

  // Add all object members to the new environment
  for (const auto &[name, value] : *withObject) {
    withEnvironment->Define(name, value);
  }

  // Push the new environment and execute the body
  auto originalEnv = this->environment;
  this->environment = withEnvironment;

  for (const auto &stmt : node.body) {
    if (stmt) {
      stmt->accept(*this);
      if (isError(lastResult)) {
        // Pop environment on error
        this->environment = originalEnv;
        return;
      }
    }
  }

  // Pop the environment
  this->environment = originalEnv;
  lastResult = nullptr;
}

void Interpreter::visitStringLiteral(const ast::StringLiteral &node) {
  lastResult = HavelValue(node.value);
}

void Interpreter::visitInterpolatedStringExpression(
    const ast::InterpolatedStringExpression &node) {
  std::string result;

  for (const auto &segment : node.segments) {
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

  lastResult = HavelValue(result);
}

void Interpreter::visitNumberLiteral(const ast::NumberLiteral &node) {
  lastResult = node.value;
}

void Interpreter::visitBooleanLiteral(const ast::BooleanLiteral &node) {
  lastResult = node.value;
}

void Interpreter::visitHotkeyLiteral(const ast::HotkeyLiteral &node) {
  lastResult = HavelValue(node.combination);
}

void Interpreter::visitAsyncExpression(const ast::AsyncExpression &node) {
  // For now, just execute the expression synchronously
  // TODO: Implement proper async/await support
  if (node.body) {
    node.body->accept(*this);
  } else {
    lastResult = nullptr;
  }
}

void Interpreter::visitAwaitExpression(const ast::AwaitExpression &node) {
  // For now, just return the awaited value
  // TODO: Implement proper async/await support
  if (node.argument) {
    node.argument->accept(*this);
  } else {
    lastResult = nullptr;
  }
}

void Interpreter::visitArrayLiteral(const ast::ArrayLiteral &node) {
  auto array = std::make_shared<std::vector<HavelValue>>();

  for (const auto &element : node.elements) {
    auto result = Evaluate(*element);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    
    // Check if this is a spread expression
    if (dynamic_cast<const ast::SpreadExpression *>(element.get())) {
      auto value = unwrap(result);
      // Spread arrays - flatten one level
      if (auto *arrPtr = value.get_if<HavelArray>()) {
        if (*arrPtr) {
          for (const auto &item : **arrPtr) {
            array->push_back(item);
          }
        }
      }
      // Spread objects in array context - just add the object
      else if (auto *objPtr = value.get_if<HavelObject>()) {
        array->push_back(value);
      }
      else {
        // Non-array, non-object spread - add as-is
        array->push_back(value);
      }
    } else {
      array->push_back(unwrap(result));
    }
  }

  lastResult = HavelValue(array);
}

void Interpreter::visitSpreadExpression(const ast::SpreadExpression &node) {
  // Spread expressions are handled by their container (array/object literal)
  // This is just a fallback - should not normally be reached
  auto result = Evaluate(*node.target);
  if (isError(result)) {
    lastResult = result;
    return;
  }
  lastResult = unwrap(result);
}

void Interpreter::visitObjectLiteral(const ast::ObjectLiteral &node) {
  auto object = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  for (const auto &[key, valueExpr] : node.pairs) {
    auto result = Evaluate(*valueExpr);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    
    // Check if this is a spread expression (marked with "__spread__" key)
    if (key == "__spread__") {
      auto value = unwrap(result);
      // Spread objects - merge properties
      if (auto *objPtr = value.get_if<HavelObject>()) {
        if (*objPtr) {
          for (const auto &[k, v] : **objPtr) {
            (*object)[k] = v;  // Later keys override earlier ones
          }
        }
      }
    } else {
      (*object)[key] = unwrap(result);
    }
  }

  lastResult = HavelValue(object);
}

void Interpreter::visitConfigBlock(const ast::ConfigBlock &node) {
  auto configObject =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  auto &config = Configs::Get();

  // Special handling for "file" key - if present, load that config file
  for (const auto &[key, valueExpr] : node.pairs) {
    if (key == "file") {
      auto result = Evaluate(*valueExpr);
      if (isError(result)) {
        lastResult = result;
        return;
      }
      std::string filePath = ValueToString(unwrap(result));

      // Expand ~ to home directory
      if (!filePath.empty() && filePath[0] == '~') {
        const char *home = std::getenv("HOME");
        if (home) {
          filePath = std::string(home) + filePath.substr(1);
        }
      }

      config.Load(filePath);
    }
  }

  // Process all config key-value pairs (including nested blocks)
  processConfigPairs(node.pairs, config, "");

  lastResult = nullptr;
}

// Helper to process config pairs recursively
void Interpreter::processConfigPairs(
    const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs,
    Configs& config,
    const std::string& prefix) {

  for (const auto &[key, valueExpr] : pairs) {
    // Skip "file" key (already processed)
    if (key == "file") continue;

    // Check if value is a nested ObjectLiteral (nested config block)
    if (auto* objLit = dynamic_cast<const ast::ObjectLiteral*>(valueExpr.get())) {
      // Recursively process nested block with updated prefix
      processConfigPairs(objLit->pairs, config, prefix + key + ".");
    } else {
      // Regular value - evaluate and store
      auto result = Evaluate(*valueExpr);
      if (isError(result)) {
        lastResult = result;
        return;
      }

      HavelValue value = unwrap(result);
      std::string configKey = "Havel." + prefix + key;
      std::string strValue = ValueToString(value);

      if (value.isBool()) {
        config.Set(configKey, value.get<bool>() ? "true" : "false");
      } else if (value.isInt()) {
        config.Set(configKey, value.get<int>());
      } else if (value.isDouble()) {
        config.Set(configKey, value.get<double>());
      } else {
        config.Set(configKey, strValue);
      }
    }
  }
}

void Interpreter::visitDevicesBlock(const ast::DevicesBlock &node) {
  auto devicesObject =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  auto &config = Configs::Get();

  // Device configuration mappings
  std::unordered_map<std::string, std::string> deviceKeyMap = {
      {"keyboard", "Device.Keyboard"},
      {"mouse", "Device.Mouse"},
      {"joystick", "Device.Joystick"},
      {"mouseSensitivity", "Mouse.Sensitivity"},
      {"ignoreMouse", "Device.IgnoreMouse"}};

  for (const auto &[key, valueExpr] : node.pairs) {
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
      if (value.isBool()) {
        config.Set(configKey, value.get<bool>() ? "true" : "false");
      } else if (value.isInt()) {
        config.Set(configKey, value.get<int>());
      } else if (value.isDouble()) {
        config.Set(configKey, value.get<double>());
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

void Interpreter::visitModesBlock(const ast::ModesBlock &node) {
  auto modesObject =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Process mode definitions
  for (const auto &[modeName, valueExpr] : node.pairs) {
    auto result = Evaluate(*valueExpr);
    if (isError(result)) {
      lastResult = result;
      return;
    }

    HavelValue value = unwrap(result);
    (*modesObject)[modeName] = value;

    // If value is an object with class/title/ignore arrays, register with
    // condition system
    if (value.isObject()) {
      auto modeConfig = value.asObject();

      // Store mode configuration for condition checking
      // The mode config will be checked later when evaluating conditions
      // Format: modes.gaming.class = ["steam", "lutris", ...]
      if (modeConfig)
        for (const auto &[configKey, configValue] : *modeConfig) {
          std::string fullKey = "__mode_" + modeName + "_" + configKey;
          environment->Define(fullKey, configValue);
        }
    }
  }

  // Initialize current mode (default to first mode or "default")
  if (modesObject && !modesObject->empty()) {
    std::string initialMode = modesObject->begin()->first;
    environment->Define("__current_mode__", HavelValue(initialMode));
    environment->Define("current_mode",
                        HavelValue(initialMode)); // Use different variable name
    environment->Define("__previous_mode__",
                        HavelValue(std::string("default")));
  } else {
    environment->Define("__current_mode__", HavelValue(std::string("default")));
    environment->Define(
        "current_mode",
        HavelValue(std::string("default"))); // Use different variable name
    environment->Define("__previous_mode__",
                        HavelValue(std::string("default")));
  }

  // Store the modes block as a special variable for script access
  environment->Define("__modes__", HavelValue(modesObject));

  lastResult = nullptr; // Modes blocks don't return a value
}

void Interpreter::visitConfigSection(const ast::ConfigSection &node) {
  // Create object for this config section
  auto configObject =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Process key-value pairs with expression evaluation
  for (const auto &[key, valueExpr] : node.pairs) {
    auto result = Evaluate(*valueExpr);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    (*configObject)[key] = unwrap(result);
  }

  // Store under config namespace: config.sectionName
  auto configContainer = environment->Get("config");
  std::shared_ptr<std::unordered_map<std::string, HavelValue>> container;
  
  if (configContainer && configContainer->isObject()) {
    container = configContainer->asObject();
  } else {
    container = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  }
  
  (*container)[node.name] = HavelValue(configObject);
  environment->Define("config", HavelValue(container));
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitIndexExpression(const ast::IndexExpression &node) {
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
  if (objectValue.isArray()) {
    auto arrayPtr = objectValue.get_if<HavelArray>();
    // Convert index to integer
    int index = static_cast<int>(ValueToNumber(indexValue));

    if (!arrayPtr || !*arrayPtr || index < 0 ||
        index >= static_cast<int>((*arrayPtr)->size())) {
      lastResult = HavelRuntimeError("Array index out of bounds: " +
                                     std::to_string(index));
      return;
    }

    lastResult = (**arrayPtr)[index];
    return;
  }

  // Handle object property access
  if (objectValue.isObject()) {
    auto objectPtr = objectValue.get_if<HavelObject>();
    std::string key = ValueToString(indexValue);

    if (objectPtr && *objectPtr) {
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

void Interpreter::visitTernaryExpression(const ast::TernaryExpression &node) {
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

void Interpreter::visitWhileStatement(const ast::WhileStatement &node) {
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

void Interpreter::visitDoWhileStatement(const ast::DoWhileStatement &node) {
  // Execute body first, then check condition
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

    // Handle continue - skip to condition check
    if (std::holds_alternative<ContinueValue>(bodyResult)) {
      // Continue with condition check
    }

    // Evaluate condition
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
      lastResult = conditionResult;
      return;
    }

    if (!ValueToBool(unwrap(conditionResult))) {
      break; // Exit loop when condition is false
    }
  }

  lastResult = nullptr;
}

void Interpreter::visitSwitchStatement(const ast::SwitchStatement &node) {
  // Evaluate the switch expression
  auto expressionResult = Evaluate(*node.expression);
  if (isError(expressionResult)) {
    lastResult = expressionResult;
    return;
  }
  auto switchValue = unwrap(expressionResult);

  // Find matching case (first match wins)
  for (const auto &caseNode : node.cases) {
    if (!caseNode)
      continue;

    // Check if this is an else case (test is nullptr)
    if (!caseNode->test) {
      // Execute else case
      auto caseResult = Evaluate(*caseNode->body);

      // Handle control flow from switch else case
      if (isError(caseResult)) {
        lastResult = caseResult;
        return;
      }

      if (std::holds_alternative<ReturnValue>(caseResult)) {
        lastResult = caseResult;
        return;
      }

      if (std::holds_alternative<BreakValue>(caseResult)) {
        // Break exits the switch
        lastResult = caseResult;
        return;
      }

      // Continue is not meaningful in switch, treat as normal completion
      if (std::holds_alternative<ContinueValue>(caseResult)) {
        lastResult = caseResult;
        return;
      }

      lastResult = caseResult;
      return;
    }

    // Evaluate case test
    auto testResult = Evaluate(*caseNode->test);
    if (isError(testResult)) {
      lastResult = testResult;
      return;
    }
    auto testValue = unwrap(testResult);

    // Check for match (using equality)
    bool matches = false;
    if (switchValue.isDouble() && testValue.isDouble()) {
      matches = (switchValue.asNumber() == testValue.asNumber());
    } else if (switchValue.isString() && testValue.isString()) {
      matches = (switchValue.asString() == testValue.asString());
    } else if (switchValue.isBool() && testValue.isBool()) {
      matches = (switchValue.asBool() == testValue.asBool());
    }

    if (matches) {
      // Execute matching case
      auto caseResult = Evaluate(*caseNode->body);

      // Handle control flow from switch case
      if (isError(caseResult)) {
        lastResult = caseResult;
        return;
      }

      if (std::holds_alternative<ReturnValue>(caseResult)) {
        lastResult = caseResult;
        return;
      }

      if (std::holds_alternative<BreakValue>(caseResult)) {
        // Break exits the switch
        lastResult = caseResult;
        return;
      }

      // Continue is not meaningful in switch, treat as normal completion
      if (std::holds_alternative<ContinueValue>(caseResult)) {
        lastResult = caseResult;
        return;
      }

      lastResult = caseResult;
      return;
    }
  }

  // No case matched and no else case
  lastResult = nullptr;
}

void Interpreter::visitSwitchCase(const ast::SwitchCase &node) {
  // This should not be called directly - switch cases are handled by
  // visitSwitchStatement
  lastResult = HavelRuntimeError("SwitchCase should not be visited directly");
}
void Interpreter::visitRangeExpression(const ast::RangeExpression &node) {
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
  
  // Handle optional step value
  int step = 1;
  if (node.step) {
    auto stepResult = Evaluate(*node.step);
    if (isError(stepResult)) {
      lastResult = stepResult;
      return;
    }
    step = static_cast<int>(ValueToNumber(unwrap(stepResult)));
    if (step == 0) {
      lastResult = HavelRuntimeError("Range step cannot be zero", node.line, node.column);
      return;
    }
  }

  // Create an array from start to end (inclusive) with step
  auto rangeArray = std::make_shared<std::vector<HavelValue>>();
  if (step > 0) {
    for (int i = start; i <= end; i += step) {
      rangeArray->push_back(HavelValue(i));
    }
  } else {
    for (int i = start; i >= end; i += step) {
      rangeArray->push_back(HavelValue(i));
    }
  }

  lastResult = rangeArray;
}

void Interpreter::visitAssignmentExpression(
    const ast::AssignmentExpression &node) {
  // Evaluate the right-hand side
  auto valueResult = Evaluate(*node.value);
  if (isError(valueResult)) {
    lastResult = valueResult;
    return;
  }
  HavelValue value = unwrap(valueResult);

  auto applyCompound = [](const std::string &op, const HavelValue &lhs,
                          const HavelValue &rhs) -> HavelValue {
    if (op == "=")
      return rhs;
    if (op == "+=")
      return HavelValue(ValueToNumber(lhs) + ValueToNumber(rhs));
    if (op == "-")
      return HavelValue(ValueToNumber(lhs) - ValueToNumber(rhs)); // not used
    if (op == "-=")
      return HavelValue(ValueToNumber(lhs) - ValueToNumber(rhs));
    if (op == "*=")
      return HavelValue(ValueToNumber(lhs) * ValueToNumber(rhs));
    if (op == "/=") {
      double denom = ValueToNumber(rhs);
      if (denom == 0.0)
        throw HavelRuntimeError("Division by zero");
      return HavelValue(ValueToNumber(lhs) / denom);
    }
    return rhs; // fallback
  };

  const std::string &op = node.operator_;

  // Determine what we're assigning to
  if (auto *identifier =
          dynamic_cast<const ast::Identifier *>(node.target.get())) {
    // Simple variable assignment (may be compound)
    auto current = environment->Get(identifier->symbol);
    if (!current.has_value()) {
      lastResult =
          HavelRuntimeError("Undefined variable: " + identifier->symbol, identifier->line, identifier->column);
      return;
    }
    
    // Check if this is a const variable
    if (environment->IsConst(identifier->symbol)) {
      lastResult = HavelRuntimeError("Cannot assign to const variable: " + identifier->symbol,
                                     identifier->line, identifier->column);
      return;
    }
    
    HavelValue newValue = applyCompound(op, *current, value);
    if (!environment->Assign(identifier->symbol, newValue)) {
      lastResult =
          HavelRuntimeError("Undefined variable: " + identifier->symbol, identifier->line, identifier->column);
      return;
    }
    value = newValue;
  } else if (auto *index = dynamic_cast<const ast::IndexExpression *>(
                 node.target.get())) {
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

    if (auto *arrayPtr = objectValue.get_if<HavelArray>()) {
      int idx = static_cast<int>(ValueToNumber(indexValue));
      if (!*arrayPtr || idx < 0 ||
          idx >= static_cast<int>((*arrayPtr)->size())) {
        lastResult = HavelRuntimeError("Array index out of bounds");
        return;
      }
      // Apply compound operator to existing value
      HavelValue newValue = applyCompound(op, (**arrayPtr)[idx], value);
      (**arrayPtr)[idx] = newValue;
      value = newValue;
    } else if (auto *objectPtr = objectValue.get_if<HavelObject>()) {
      std::string key = ValueToString(indexValue);
      if (!*objectPtr) {
        *objectPtr =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
      }
      // If property exists, apply compound operator; otherwise treat as simple
      // assignment
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
  } else if (auto *member = dynamic_cast<const ast::MemberExpression *>(
                 node.target.get())) {
    // Member expression assignment (obj.prop = value)
    auto objectResult = Evaluate(*member->object);
    if (isError(objectResult)) {
      lastResult = objectResult;
      return;
    }
    HavelValue objectValue = unwrap(objectResult);
    
    // Get property name
    std::string propName;
    if (auto *propId = dynamic_cast<const ast::Identifier *>(member->property.get())) {
      propName = propId->symbol;
    } else {
      lastResult = HavelRuntimeError("Invalid property name", node.line, node.column);
      return;
    }
    
    // Check if object supports property assignment
    if (auto *objectPtr = objectValue.get_if<HavelObject>()) {
      if (!*objectPtr) {
        *objectPtr = std::make_shared<std::unordered_map<std::string, HavelValue>>();
      }
      (**objectPtr)[propName] = value;
    } else {
      lastResult = HavelRuntimeError("Cannot set property on non-object value", node.line, node.column);
      return;
    }
  } else {
    lastResult = HavelRuntimeError("Invalid assignment target", node.line, node.column);
    return;
  }

  lastResult = value; // Assignment expressions return the assigned value
}

void Interpreter::visitCastExpression(const ast::CastExpression& node) {
  // Delegate to ExprEvaluator
  ExprEvaluator eval(this);
  eval.visitCastExpression(node);
}

void Interpreter::visitMatchExpression(const ast::MatchExpression& node) {
  // Delegate to ExprEvaluator
  ExprEvaluator eval(this);
  eval.visitMatchExpression(node);
}

void Interpreter::visitObjectPattern(const ast::ObjectPattern &node) {
  // This is typically handled during assignment/let declaration
  // For now, just evaluate pattern properties (identifiers)
  for (const auto &[key, pattern] : node.properties) {
    if (pattern) {
      auto result = Evaluate(*pattern);
      if (isError(result)) {
        lastResult = result;
        return;
      }
    }
  }
  lastResult = HavelValue(nullptr); // Patterns don't produce values
}

void Interpreter::visitTryExpression(const ast::TryExpression &node) {
  // Execute try body
  auto tryResult = Evaluate(*node.tryBody);

  // Check if try body threw an error
  if (auto *err = std::get_if<HavelRuntimeError>(&tryResult)) {
    // If we have a catch block, execute it
    if (node.catchBody) {
      // Create new environment for catch block with catch variable
      auto catchEnv = std::make_shared<Environment>(environment);
      auto originalEnv = environment;
      environment = catchEnv;
      
      // If catch variable is specified, create it in catch scope
      if (node.catchVariable) {
        // Store the error message as string in the catch variable
        std::string errorMsg = err->what();
        environment->Define(node.catchVariable->symbol, HavelValue(errorMsg));
      }

      auto catchResult = Evaluate(*node.catchBody);
      
      // Restore original environment
      environment = originalEnv;
      
      // Execute finally block if present (always runs, even after catch)
      if (node.finallyBlock) {
        auto finallyResult = Evaluate(*node.finallyBlock);
        if (isError(finallyResult)) {
          lastResult = finallyResult;
          return;
        }
      }
      
      if (isError(catchResult)) {
        lastResult = catchResult;
        return;
      }
      lastResult = catchResult;
      return;
    }

    // No catch handler, execute finally if present
    if (node.finallyBlock) {
      auto finallyResult = Evaluate(*node.finallyBlock);
      if (isError(finallyResult)) {
        lastResult = finallyResult;
        return;
      }
    }
    
    // Re-throw the original error
    lastResult = *err;
    return;
  }

  // Try body succeeded, execute finally if present
  if (node.finallyBlock) {
    auto finallyResult = Evaluate(*node.finallyBlock);
    if (isError(finallyResult)) {
      lastResult = finallyResult;
      return;
    }
  }

  // Return try body result
  lastResult = tryResult;
}

void Interpreter::visitThrowStatement(const ast::ThrowStatement &node) {
  if (!node.value) {
    lastResult = HavelRuntimeError("Thrown value is null");
    return;
  }

  auto valueResult = Evaluate(*node.value);
  if (isError(valueResult)) {
    lastResult = valueResult;
    return;
  }

  HavelValue thrownValue = unwrap(valueResult);
  
  // Store the thrown value - preserve the original type
  // If it's a string, use it directly; otherwise convert to string
  if (thrownValue.isString()) {
    lastResult = HavelRuntimeError(thrownValue.asString());
  } else {
    lastResult = HavelRuntimeError(ValueToString(thrownValue));
  }
}

// Type system - struct/enum support (stub implementations for now)
void Interpreter::visitStructFieldDef(const ast::StructFieldDef &node) {
  lastResult = HavelRuntimeError("StructFieldDef evaluation not implemented");
}

void Interpreter::visitStructMethodDef(const ast::StructMethodDef &node) {
  // Methods are handled when accessed on struct instances
  lastResult = HavelValue(nullptr);
}

void Interpreter::visitStructDefinition(const ast::StructDefinition &node) {
  lastResult = HavelRuntimeError("StructDefinition evaluation not implemented");
}

void Interpreter::visitStructDeclaration(const ast::StructDeclaration &node) {
  // Register struct type in TypeRegistry
  auto structType = std::make_shared<HavelStructType>(node.name);
  for (const auto& field : node.definition.fields) {
    // Convert ast::StructFieldDef to StructField
    StructField havelField;
    havelField.name = field.name;
    // For now, just store field names without type validation
    structType->addField(havelField);
  }
  // Store methods in the struct type
  for (const auto& method : node.definition.methods) {
    if (method) {
      structType->addMethod(method->name, method.get());
    }
  }
  TypeRegistry::getInstance().registerStructType(structType);

  // Create a constructor function for this struct type
  // Usage: MousePos.new() or MousePos.new(x=10, y=20) or MousePos(100, 200)
  auto structTypeName = node.name;

  // Create MousePos.new function
  auto newFunc = BuiltinFunction([this, structType](const std::vector<HavelValue> &args) -> HavelResult {
    // Create new struct instance
    HavelStructInstance instance(structType->getName(), structType);

    // Initialize fields from arguments (positional or named)
    size_t argIdx = 0;
    for (const auto& field : structType->getFields()) {
      if (argIdx < args.size()) {
        instance.fields->insert({field.name, args[argIdx]});
        argIdx++;
      } else {
        instance.fields->insert({field.name, HavelValue(nullptr)});
      }
    }

    // Call init method if it exists
    auto initMethod = structType->getMethod("init");
    if (initMethod && initMethod->body) {
      auto initEnv = std::make_shared<Environment>(this->environment);
      initEnv->Define("this", HavelValue(instance));
      auto originalEnv = this->environment;
      this->environment = initEnv;
      Evaluate(*initMethod->body);
      this->environment = originalEnv;
    }

    return HavelValue(instance);
  });

  // Create MousePos object with .new method and __call__ for direct invocation
  auto structObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  (*structObj)["new"] = HavelValue(newFunc);
  (*structObj)["__call__"] = HavelValue(newFunc);  // Enable MousePos(100, 200) syntax
  environment->Define(structTypeName, HavelValue(structObj));

  lastResult = nullptr;
}

void Interpreter::visitEnumVariantDef(const ast::EnumVariantDef &node) {
  lastResult = HavelRuntimeError("EnumVariantDef evaluation not implemented");
}

void Interpreter::visitEnumDefinition(const ast::EnumDefinition &node) {
  lastResult = HavelRuntimeError("EnumDefinition evaluation not implemented");
}

void Interpreter::visitEnumDeclaration(const ast::EnumDeclaration &node) {
  // Register enum type in TypeRegistry
  auto enumType = std::make_shared<HavelEnumType>(node.name);
  for (const auto& variant : node.definition.variants) {
    // Convert ast::EnumVariantDef to EnumVariant
    EnumVariant havelVariant;
    havelVariant.name = variant.name;
    havelVariant.hasPayload = variant.payloadType.has_value();
    enumType->addVariant(havelVariant);
  }
  TypeRegistry::getInstance().registerEnumType(enumType);
  lastResult = nullptr;
}

void Interpreter::visitTraitDeclaration(const ast::TraitDeclaration &node) {
  // Register trait in trait registry (stub - full implementation later)
  // For now, just acknowledge the trait definition
  info("Trait defined: " + node.name->symbol);
  lastResult = nullptr;
}

void Interpreter::visitTraitMethod(const ast::TraitMethod &node) {
  // Trait methods are handled as part of trait/impl declarations
  lastResult = nullptr;
}

void Interpreter::visitImplDeclaration(const ast::ImplDeclaration &node) {
  // Register impl for trait - inject methods into type
  std::string traitName = node.traitName ? node.traitName->symbol : "";
  std::string typeName = node.typeName ? node.typeName->symbol : "";
  
  // Build method map for this impl
  std::unordered_map<std::string, HavelValue> methodMap;

  for (const auto& methodDecl : node.funcs) {
    // Capture raw pointer for lambda (AST lives for program lifetime)
    const ast::FunctionDeclaration* methodPtr = methodDecl.get();
    
    // Create a bound function for this method
    auto methodFunc = BuiltinFunction([this, methodPtr](const std::vector<HavelValue> &args) -> HavelResult {
      // Create method environment
      auto methodEnv = std::make_shared<Environment>(this->environment);

      // Bind parameters (first arg is 'this' if method expects it)
      size_t paramIdx = 0;
      for (size_t i = 0; i < methodPtr->parameters.size() && paramIdx < args.size(); ++i) {
        methodEnv->Define(methodPtr->parameters[i]->paramName->symbol, args[paramIdx]);
        paramIdx++;
      }

      auto originalEnv = this->environment;
      this->environment = methodEnv;
      auto res = Evaluate(*methodPtr->body);
      this->environment = originalEnv;

      if (std::holds_alternative<ReturnValue>(res)) {
        auto ret = std::get<ReturnValue>(res);
        return ret.value ? *ret.value : HavelValue();
      }
      return res;
    });

    methodMap[methodDecl->name->symbol] = HavelValue(methodFunc);
  }
  
  // Register with trait registry
  TraitRegistry::getInstance().registerImpl(traitName, typeName, methodMap);
  
  info("Impl registered: " + traitName + " for " + typeName);
  lastResult = nullptr;
}


void Interpreter::visitForStatement(const ast::ForStatement &node) {
  // Evaluate the iterable expression
  auto iterableResult = Evaluate(*node.iterable);
  if (isError(iterableResult)) {
    lastResult = iterableResult;
    return;
  }

  // Unwrap the result to get the HavelValue
  auto iterableValue = unwrap(iterableResult);

  // Create a new environment for the for loop
  auto loopEnv = std::make_shared<Environment>(this->environment);
  auto originalEnv = this->environment;
  this->environment = loopEnv;

  // Handle different types of iterables
  if (iterableValue.is<HavelArray>()) {
    // Array iteration
    auto array = iterableValue.get<HavelArray>();
    if (!array) {
      lastResult = HavelRuntimeError("Cannot iterate over null array");
      this->environment = originalEnv;
      return;
    }

    for (const auto &element : *array) {
      // Set loop variable (use first iterator if available)
      if (!node.iterators.empty()) {
        environment->Define(node.iterators[0]->symbol, element);
      }

      // Execute loop body
      node.body->accept(*this);

      // Check for break/continue
      if (std::holds_alternative<BreakValue>(lastResult)) {
        lastResult = nullptr;
        break;
      }
      if (std::holds_alternative<ContinueValue>(lastResult)) {
        lastResult = nullptr;
        continue;
      }
      if (isError(lastResult)) {
        break;
      }
    }
  } else if (iterableValue.is<HavelObject>()) {
    // Object iteration
    auto object = iterableValue.get<HavelObject>();
    if (!object) {
      lastResult = HavelRuntimeError("Cannot iterate over null object");
      this->environment = originalEnv;
      return;
    }

    for (const auto &[key, value] : *object) {
      // Set loop variable to the key (use first iterator if available)
      if (!node.iterators.empty()) {
        environment->Define(node.iterators[0]->symbol, HavelValue(key));
      }

      // Execute loop body
      node.body->accept(*this);

      // Check for break/continue
      if (std::holds_alternative<BreakValue>(lastResult)) {
        lastResult = nullptr;
        break;
      }
      if (std::holds_alternative<ContinueValue>(lastResult)) {
        lastResult = nullptr;
        continue;
      }
      if (isError(lastResult)) {
        break;
      }
    }
  } else {
    lastResult = HavelRuntimeError("Cannot iterate over value");
  }

  // Restore original environment
  this->environment = originalEnv;
}

void Interpreter::visitLoopStatement(const ast::LoopStatement &node) {
  // Loop with optional condition
  while (true) {
    // Check condition if present (loop while condition {})
    if (node.condition) {
      auto condResult = Evaluate(*node.condition);
      if (isError(condResult)) {
        lastResult = condResult;
        return;
      }
      if (!ExecResultToBool(condResult)) {
        break;  // Condition is false, exit loop
      }
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

void Interpreter::visitBreakStatement(const ast::BreakStatement &node) {
  lastResult = BreakValue{};
}

void Interpreter::visitContinueStatement(const ast::ContinueStatement &node) {
  lastResult = ContinueValue{};
}

void Interpreter::visitOnModeStatement(const ast::OnModeStatement &node) {
  // Get current mode
  auto currentModeOpt = environment->Get("__current_mode__");
  std::string currentMode = "default";

  if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
    currentMode = (*currentModeOpt).get<std::string>();
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

void Interpreter::visitOffModeStatement(const ast::OffModeStatement &node) {
  // Get previous mode (we'll track this when mode changes)
  auto prevModeOpt = environment->Get("__previous_mode__");
  auto currentModeOpt = environment->Get("__current_mode__");

  std::string previousMode = "default";
  std::string currentMode = "default";

  if (prevModeOpt && (*prevModeOpt).is<std::string>()) {
    previousMode = (*prevModeOpt).get<std::string>();
  }
  if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
    currentMode = (*currentModeOpt).get<std::string>();
  }

  // Check if we're leaving the specified mode
  if (previousMode == node.modeName && currentMode != node.modeName) {
    // Execute the off-mode body
    lastResult = Evaluate(*node.body);
  } else {
    lastResult = nullptr;
  }
}

void Interpreter::visitOnReloadStatement(const ast::OnReloadStatement &node) {
  // Store the body as the on reload handler
  onReloadHandler = [this, body = node.body.get()]() {
    auto originalEnv = environment;
    environment = std::make_shared<Environment>(originalEnv);
    Evaluate(*body);
    environment = originalEnv;
  };
  lastResult = nullptr;
}

void Interpreter::visitOnStartStatement(const ast::OnStartStatement &node) {
  // Execute immediately ONLY on first run, NOT on reload
  if (isFirstRun.load()) {
    auto originalEnv = environment;
    environment = std::make_shared<Environment>(originalEnv);
    Evaluate(*node.body);
    environment = originalEnv;
  }
  // Don't store as handler - on start should NOT run on reload
  lastResult = nullptr;
}

// ============================================================================
// Type System Implementation
// ============================================================================

// Helper to resolve AST TypeDefinition to HavelType
std::shared_ptr<HavelType> Interpreter::resolveType(const ast::TypeDefinition& typeDef) {
    switch (typeDef.kind) {
        case ast::NodeType::TypeAnnotation: {
            const auto& typeRef = static_cast<const ast::TypeReference&>(typeDef);
            const std::string& name = typeRef.name;
            
            // Check builtin types
            if (name == "Num" || name == "Number") return HavelType::num();
            if (name == "Str" || name == "String") return HavelType::str();
            if (name == "Bool" || name == "Boolean") return HavelType::boolean();
            if (name == "Any") return HavelType::any();
            if (name == "Null") return HavelType::null();
            
            // Check registered types
            auto& registry = TypeRegistry::getInstance();
            if (auto structType = registry.getStructType(name)) return structType;
            if (auto enumType = registry.getEnumType(name)) return enumType;
            if (auto aliasType = registry.getTypeAlias(name)) return aliasType;
            
            // Unknown type - return Any
            return HavelType::any();
        }
        
        case ast::NodeType::UnionType: {
            const auto& unionType = static_cast<const ast::UnionType&>(typeDef);
            // For now, just return Any - full union type support needs more work
            return HavelType::any();
        }
        
        case ast::NodeType::RecordExpression: {
            const auto& recordType = static_cast<const ast::RecordType&>(typeDef);
            auto havelRecord = std::make_shared<HavelRecordType>();
            for (const auto& [name, typeDefPtr] : recordType.fields) {
                if (typeDefPtr) {
                    havelRecord->addField(name, resolveType(*typeDefPtr));
                }
            }
            return havelRecord;
        }
        
        case ast::NodeType::FunctionDeclaration: {
            const auto& funcType = static_cast<const ast::FunctionType&>(typeDef);
            std::vector<std::shared_ptr<HavelType>> paramTypes;
            for (const auto& paramDef : funcType.paramTypes) {
                if (paramDef) {
                    paramTypes.push_back(resolveType(*paramDef));
                }
            }
            std::optional<std::shared_ptr<HavelType>> returnType;
            if (funcType.returnType) {
                returnType = resolveType(*funcType.returnType);
            }
            return std::make_shared<HavelFunctionType>(paramTypes, returnType);
        }
        
        default:
            return HavelType::any();
    }
}

void Interpreter::visitTypeDeclaration(const ast::TypeDeclaration &node) {
    // type Name = Definition
    auto typeDef = resolveType(*node.definition);
    
    // Register as type alias
    TypeRegistry::getInstance().registerTypeAlias(node.name, typeDef);
    
    lastResult = HavelValue(nullptr);
}

void Interpreter::visitTypeAnnotation(const ast::TypeAnnotation &node) {
    // Type annotations are handled during variable/function declaration
    // This is called when annotation is used standalone
    if (node.type) {
        auto type = resolveType(*node.type);
        // Store in lastResult for potential use
        lastResult = HavelValue(nullptr);  // Type annotations don't produce values
    } else {
        lastResult = HavelValue(nullptr);
    }
}

void Interpreter::visitUnionType(const ast::UnionType &node) {
    // Union types are resolved in resolveType()
    // This visitor is for when UnionType appears in expression context
    lastResult = HavelValue(nullptr);
}

void Interpreter::visitRecordType(const ast::RecordType &node) {
    // Record types are resolved in resolveType()
    lastResult = HavelValue(nullptr);
}

void Interpreter::visitFunctionType(const ast::FunctionType &node) {
    // Function types are resolved in resolveType()
    lastResult = HavelValue(nullptr);
}

void Interpreter::visitTypeReference(const ast::TypeReference &node) {
    // Type references are resolved in resolveType()
    auto type = resolveType(node);
    lastResult = HavelValue(nullptr);
}



// Reload and debug helper methods (stub implementations)
void Interpreter::enableReload() {
    reloadEnabled.store(true);
}

void Interpreter::disableReload() {
    reloadEnabled.store(false);
}

void Interpreter::toggleReload() {
    reloadEnabled.store(!reloadEnabled.load());
}

void Interpreter::setShowAST(bool show) {
    showASTOnParse = show;
}

void Interpreter::setStopOnError(bool stop) {
    stopOnError = stop;
}

std::string Interpreter::getInterpreterState() const {
    return "Interpreter running";
}

// ============================================================================
// Conditional Hotkey Implementation
// ============================================================================

void Interpreter::visitConditionalHotkey(const ast::ConditionalHotkey& node) {
    // Need HotkeyManager to register conditional hotkey
    if (!hostContext.hotkeyManager) {
        lastResult = HavelRuntimeError("Conditional hotkeys require HotkeyManager", 
                                       node.line, node.column);
        return;
    }
    
    // Evaluate the binding to get the hotkey details
    if (!node.binding) {
        lastResult = HavelRuntimeError("Conditional hotkey requires a binding",
                                       node.line, node.column);
        return;
    }
    
    // Extract key string from binding
    std::string keyStr;
    if (!node.binding->hotkeys.empty()) {
        // Get first hotkey as string
        auto& hkExpr = node.binding->hotkeys[0];
        if (hkExpr && hkExpr->kind == ast::NodeType::HotkeyLiteral) {
            const auto& hkLit = static_cast<const ast::HotkeyLiteral&>(*hkExpr);
            keyStr = hkLit.combination;
        }
    }
    
    if (keyStr.empty()) {
        lastResult = HavelRuntimeError("Conditional hotkey requires a valid key",
                                       node.line, node.column);
        return;
    }
    
    // Evaluate condition expression to get a function
    if (!node.condition) {
        lastResult = HavelRuntimeError("Conditional hotkey requires a condition",
                                       node.line, node.column);
        return;
    }
    
    // Create condition function that evaluates the condition expression
    auto conditionFunc = [this, condExpr = node.condition.get()]() -> bool {
        auto result = Evaluate(*condExpr);
        if (isError(result)) {
            return false;
        }
        HavelValue condValue = unwrap(result);
        
        // Check if true (bool or non-zero number)
        if (condValue.isBool()) {
            return condValue.get<bool>();
        } else if (condValue.isNumber()) {
            return (condValue.asNumber() != 0);
        }
        return true;
    };
    
    // Create action function from binding
    auto actionFunc = [this, binding = node.binding.get()]() {
        // Execute the binding's action
        if (binding->action) {
            Evaluate(*binding->action);
        }
    };
    
    // Register with HotkeyManager using AddContextualHotkey
    int id = hostContext.hotkeyManager->AddContextualHotkey(
        keyStr, 
        std::move(conditionFunc),
        std::move(actionFunc)
    );
    
    if (id > 0) {
        lastResult = HavelValue(static_cast<double>(id));
    } else {
        lastResult = HavelRuntimeError("Failed to register conditional hotkey",
                                       node.line, node.column);
    }
}

void Interpreter::visitWhenBlock(const ast::WhenBlock& node) {
    // Evaluate the condition
    if (!node.condition) {
        lastResult = HavelRuntimeError("When block requires a condition",
                                       node.line, node.column);
        return;
    }
    
    auto condResult = Evaluate(*node.condition);
    if (isError(condResult)) {
        lastResult = condResult;
        return;
    }
    
    HavelValue condValue = unwrap(condResult);
    
    // Check if condition is true
    bool isTrue = false;
    if (condValue.isBool()) {
        isTrue = condValue.get<bool>();
    } else if (condValue.isNumber()) {
        isTrue = (condValue.asNumber() != 0);
    } else {
        isTrue = true;
    }
    
    if (isTrue) {
        // Execute all statements in the when block
        for (const auto& stmt : node.statements) {
            if (stmt) {
                Evaluate(*stmt);
                if (isError(lastResult)) {
                    return;
                }
            }
        }
    }
    
    lastResult = HavelValue(nullptr);
}


// Delegate implementations for visitors moved to evaluators
void Interpreter::visitIdentifier(const ast::Identifier& node) {
    if (auto val = environment->Get(node.symbol)) {
        lastResult = *val;
    } else {
        lastResult = HavelRuntimeError("Undefined variable: " + node.symbol, node.line, node.column);
    }
}

} // namespace havel
