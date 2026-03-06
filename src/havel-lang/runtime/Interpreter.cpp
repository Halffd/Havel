#include "Interpreter.hpp"
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
#include "../../modules/ModuleLoader.hpp"  // For loadHostModules
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

static HavelValue unwrap(HavelResult &result) {
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

// Constructor with Dependency Injection
Interpreter::Interpreter(IO &io_system, WindowManager &window_mgr,
                         HotkeyManager *hotkey_mgr,
                         BrightnessManager *brightness_mgr,
                         AudioManager *audio_mgr, GUIManager *gui_mgr,
                         ScreenshotManager *screenshot_mgr,
                         ClipboardManager *clipboard_mgr,
                         PixelAutomation *pixel_automation,
                         const std::vector<std::string> &cli_args)
    : io(&io_system), windowManager(&window_mgr), hotkeyManager(hotkey_mgr),
      brightnessManager(brightness_mgr), audioManager(audio_mgr),
      guiManager(gui_mgr), screenshotManager(screenshot_mgr),
      clipboardManager(clipboard_mgr), pixelAutomation(pixel_automation),
      lastResult(HavelValue(nullptr)), cliArgs(cli_args),
      m_destroyed(std::make_shared<std::atomic<bool>>(false)) {
  info("Interpreter constructor called");
  environment = std::make_shared<Environment>();
  environment->Define("constructor_called", HavelValue(true));
  InitializeStandardLibrary();
}

// Minimal interpreter for pure script execution (no IO/hotkeys/display)
Interpreter::Interpreter(const std::vector<std::string> &cli_args)
    : io(nullptr), windowManager(nullptr),
      hotkeyManager(nullptr), brightnessManager(nullptr), audioManager(nullptr),
      guiManager(nullptr), screenshotManager(nullptr),
      clipboardManager(nullptr), pixelAutomation(nullptr),
      lastResult(HavelValue(nullptr)), cliArgs(cli_args),
      m_destroyed(std::make_shared<std::atomic<bool>>(false)) {
  // Initialize KeyMap for key name lookups (even in pure mode)
  KeyMap::Initialize();
  
  info("Minimal Interpreter created (pure mode - no IO/hotkeys)");
  environment = std::make_shared<Environment>();
  environment->Define("constructor_called", HavelValue(true));
  environment->Define("__pure_mode__", HavelValue(true));
  InitializeStandardLibrary();
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
          std::string activeTitle = this->windowManager->GetActiveWindowTitle();
          return activeTitle.find(condValue) != std::string::npos;
        });
      } else if (condType == "class") {
        contextChecks.push_back([this, condValue]() {
          std::string activeClass = this->windowManager->GetActiveWindowClass();
          return activeClass.find(condValue) != std::string::npos;
        });
      } else if (condType == "process") {
        contextChecks.push_back([this, condValue]() {
          pID pid = this->windowManager->GetActiveWindowPID();
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
      auto result = this->Evaluate(*action);
      if (isError(result)) {
        std::cerr << "Runtime error in hotkey: " << getErrorMessage(result)
                  << std::endl;
      }
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
    if (io) {
      io->Hotkey(hotkey, actionHandler);
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
        io->Send(cmd.text.c_str());
        break;

      case ast::InputCommand::SendKey:
        io->Send(("{ " + cmd.key + " }").c_str());
        break;

      case ast::InputCommand::MouseClick:
        if (cmd.text == "left" || cmd.text == "lmb") {
          io->MouseClick(1);
        } else if (cmd.text == "right" || cmd.text == "rmb") {
          io->MouseClick(2);
        } else if (cmd.text == "middle" || cmd.text == "mmb") {
          io->MouseClick(3);
        } else if (cmd.text == "side1" || cmd.text == "btn4") {
          io->MouseClick(4);
        } else if (cmd.text == "side2" || cmd.text == "btn5") {
          io->MouseClick(5);
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
        io->MouseMoveTo(static_cast<int>(x), static_cast<int>(y), speed, accel);
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
        io->MouseMove(static_cast<int>(x), static_cast<int>(y), speed, accel);
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
        io->Scroll(y, x);
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
        io->MouseMoveTo(static_cast<int>(x), static_cast<int>(y), speed, accel);
        io->MouseClick(button);
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
  case ast::BinaryOperator::ConfigAppend:
    // >> config operator
    // value >> config.path - SET config value
    // config.path >> variable - GET config value (not yet implemented)
    if (right.isObject()) {
      // value >> {config} - merge into config object
      auto rightObj = right.asObject();
      if (rightObj && left.isString()) {
        // Simple case: "value" >> configObj
        // For now, just store in a special config variable
        environment->Define("__config_value__", left);
        lastResult = left;
      } else {
        lastResult = HavelRuntimeError("Config append requires string value");
      }
    } else if (right.isString()) {
      // value >> "config.key" - SET config value
      std::string configKey = right.asString();
      if (left.isString()) {
        // Store in config
        auto &config = Configs::Get();
        config.Set(configKey, left.asString(), false);
        lastResult = left;
      } else {
        lastResult = HavelRuntimeError("Config value must be a string");
      }
    } else {
      lastResult = HavelRuntimeError("Config append requires string or object on right side");
    }
    break;
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
    } else {
      lastResult = nullptr; // Implicit return
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
      Interpreter moduleInterpreter(*io, *windowManager);
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

void Interpreter::visitIdentifier(const ast::Identifier &node) {
  if (auto val = environment->Get(node.symbol)) {
    lastResult = *val;
  } else {
    lastResult = HavelRuntimeError("Undefined variable: " + node.symbol, node.line, node.column);
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

// Stubs for unimplemented visit methods
void Interpreter::visitTypeDeclaration(const ast::TypeDeclaration &node) {
  lastResult = HavelRuntimeError("Type declarations not implemented.");
}
void Interpreter::visitTypeAnnotation(const ast::TypeAnnotation &node) {
  lastResult = HavelRuntimeError("Type annotations not implemented.");
}
void Interpreter::visitUnionType(const ast::UnionType &node) {
  lastResult = HavelRuntimeError("Union types not implemented.");
}
void Interpreter::visitRecordType(const ast::RecordType &node) {
  lastResult = HavelRuntimeError("Record types not implemented.");
}
void Interpreter::visitFunctionType(const ast::FunctionType &node) {
  lastResult = HavelRuntimeError("Function types not implemented.");
}
void Interpreter::visitTypeReference(const ast::TypeReference &node) {
  lastResult = HavelRuntimeError("Type references not implemented.");
}

void Interpreter::InitializeStandardLibrary() {
  // Expose CLI arguments via app.args
  auto argsArray = std::make_shared<std::vector<HavelValue>>();
  for (const auto &s : cliArgs) {
    argsArray->push_back(HavelValue(s));
  }

  auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  (*appObj)["args"] = HavelValue(argsArray);
  
  // Script auto-reload control
  (*appObj)["enableReload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        this->enableReload();
        return HavelValue(true);
      }));
  
  (*appObj)["disableReload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        this->disableReload();
        return HavelValue(false);
      }));
  
  (*appObj)["toggleReload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        this->toggleReload();
        return HavelValue(this->isReloadEnabled());
      }));
  
  (*appObj)["reload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() >= 1) {
          if (auto b = args[0].get_if<bool>()) {
            if (*b) {
              this->enableReload();
            } else {
              this->disableReload();
            }
          }
        }
        return HavelValue(this->isReloadEnabled());
      }));
  
  // runOnce function - execute shell command only once, not on reload
  environment->Define("runOnce", HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("runOnce requires an id and a command string");
        }

        std::string id;
        if (args[0].is<std::string>()) {
          id = args[0].get<std::string>();
        } else {
          return HavelRuntimeError("runOnce: first argument must be a string id");
        }

        // Check if already executed
        if (this->hasRunOnce(id)) {
          return HavelValue(true);  // Already executed, return success
        }

        // Mark as executed BEFORE running (prevents re-entry)
        this->markRunOnce(id);

        // If there's a command string argument, execute it with Launcher::runShell
        if (args.size() >= 2 && args[1].is<std::string>()) {
          std::string cmd = args[1].get<std::string>();
          auto result = Launcher::runShell(cmd);
          if (result.success) {
            info("runOnce('{}'): Command executed successfully", id);
            return HavelValue(true);
          } else {
            error("runOnce('{}'): Command failed: {}", id, result.error);
            return HavelValue(false);
          }
        }

        return HavelValue(true);
      })));

  environment->Define("app", HavelValue(appObj));

  // Register type conversion module
  havel::stdlib::registerTypeModule(environment.get());

  // Debug control object
  auto debugObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // New debug control functions
  (*debugObj)["showAST"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = args[0].get_if<bool>()) {
            this->showASTOnParse = *b;
          }
        }
        return HavelValue(this->showASTOnParse);
      }));

  (*debugObj)["stopOnError"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = args[0].get_if<bool>()) {
            this->stopOnError = *b;
          }
        }
        return HavelValue(this->stopOnError);
      }));

  (*debugObj)["interpreterState"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        (void)args;
        return HavelValue(getInterpreterState());
      }));

  environment->Define("debug", HavelValue(debugObj));

  // Initialize all builtin modules
  InitializeSystemBuiltins();
  InitializeWindowBuiltins();
  InitializeClipboardBuiltins();
  havel::stdlib::registerStringModule(environment.get());
  havel::stdlib::registerFileModule(environment.get());
  havel::stdlib::registerArrayModule(environment.get());
  havel::stdlib::registerRegexModule(environment.get());
  havel::stdlib::registerProcessModule(environment.get());
  InitializeIOBuiltins();
  InitializeBrightnessBuiltins();
  InitializeMathBuiltins();
  InitializeHelpBuiltin();
  InitializeAudioBuiltins();
  InitializeMediaBuiltins();
  InitializeFileManagerBuiltins();
  InitializeLauncherBuiltins();
  InitializeGUIBuiltins();
  InitializeScreenshotBuiltins();
  InitializePixelBuiltins();
  InitializeTimerBuiltins();
  InitializeAutomationBuiltins();
  InitializeAsyncBuiltins();
  InitializePhysicsBuiltins();

  // Debug flag
  environment->Define("debug", HavelValue(false));

  // Debug print with conditional execution
  environment->Define(
      "debug.print",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            auto debugFlag = environment->Get("debug");
            bool isDebug = debugFlag && ValueToBool(*debugFlag);

            if (isDebug) {
              std::cout << "[DEBUG] ";
              for (const auto &arg : args) {
                std::cout << this->ValueToString(arg) << " ";
              }
              std::cout << std::endl;
            }
            return HavelValue(nullptr);
          }));

  // Assert function
  environment->Define(
      "assert",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("assert() requires condition");
        if (!ValueToBool(args[0])) {
          std::string msg =
              args.size() > 1 ? ValueToString(args[1]) : "Assertion failed";
          return HavelRuntimeError(msg);
        }
        return HavelValue(nullptr);
      }));

  // Create io module at the end after all io functions are defined
  auto ioMod = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("io->mouseMove"))
    (*ioMod)["mouseMove"] = *v;
  if (auto v = environment->Get("io->mouseMoveTo"))
    (*ioMod)["mouseMoveTo"] = *v;
  if (auto v = environment->Get("io->mouseClick"))
    (*ioMod)["mouseClick"] = *v;
  if (auto v = environment->Get("io->mouseDown"))
    (*ioMod)["mouseDown"] = *v;
  if (auto v = environment->Get("io->mouseUp"))
    (*ioMod)["mouseUp"] = *v;
  if (auto v = environment->Get("io->mouseWheel"))
    (*ioMod)["mouseWheel"] = *v;
  if (auto v = environment->Get("io->getKeyState"))
    (*ioMod)["getKeyState"] = *v;
  if (auto v = environment->Get("io->isShiftPressed"))
    (*ioMod)["isShiftPressed"] = *v;
  if (auto v = environment->Get("io->isCtrlPressed"))
    (*ioMod)["isCtrlPressed"] = *v;
  if (auto v = environment->Get("io->isAltPressed"))
    (*ioMod)["isAltPressed"] = *v;
  if (auto v = environment->Get("io->isWinPressed"))
    (*ioMod)["isWinPressed"] = *v;
  if (auto v = environment->Get("io->scroll"))
    (*ioMod)["scroll"] = *v;
  if (auto v = environment->Get("io->getMouseSensitivity"))
    (*ioMod)["getMouseSensitivity"] = *v;
  if (auto v = environment->Get("io->setMouseSensitivity"))
    (*ioMod)["setMouseSensitivity"] = *v;
  if (auto v = environment->Get("io->emergencyReleaseAllKeys"))
    (*ioMod)["emergencyReleaseAllKeys"] = *v;

  if (auto v = environment->Get("io.map"))
    (*ioMod)["map"] = *v;
  if (auto v = environment->Get("io.remap"))
    (*ioMod)["remap"] = *v;

  environment->Define("io", HavelValue(ioMod));

  // Create audio module
  auto audioMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("audio.setVolume"))
    (*audioMod)["setVolume"] = *v;
  if (auto v = environment->Get("audio.getVolume"))
    (*audioMod)["getVolume"] = *v;
  if (auto v = environment->Get("audio.increaseVolume"))
    (*audioMod)["increaseVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseVolume"))
    (*audioMod)["decreaseVolume"] = *v;
  if (auto v = environment->Get("audio.toggleMute"))
    (*audioMod)["toggleMute"] = *v;
  if (auto v = environment->Get("audio.setMute"))
    (*audioMod)["setMute"] = *v;
  if (auto v = environment->Get("audio.isMuted"))
    (*audioMod)["isMuted"] = *v;
  if (auto v = environment->Get("audio.getApps"))
    (*audioMod)["getApps"] = *v;
  if (auto v = environment->Get("audio.getDefaultOutput"))
    (*audioMod)["getDefaultOutput"] = *v;
  if (auto v = environment->Get("audio.playTestSound"))
    (*audioMod)["playTestSound"] = *v;
  environment->Define("audio", HavelValue(audioMod));

  // Expose KeyTap constructor to script environment
  environment->Define(
      "createKeyTap",
      HavelValue(BuiltinFunction([this](const std::vector<HavelValue> &args)
                                     -> HavelResult {
        if (args.size() < 1) {
          return HavelRuntimeError("createKeyTap requires keyName");
        }

        std::string keyName = ValueToString(args[0]);
        havel::debug("createKeyTap builtin called with keyName: '{}'", keyName);

        // Optional parameters with defaults
        std::function<void()> onTap = []() { /* Default empty tap action */ };
        std::variant<std::string, std::function<bool()>> tapCondition = {};
        std::variant<std::string, std::function<bool()>> comboCondition = {};
        std::function<void()> onCombo = nullptr;
        bool grabDown = true;
        bool grabUp = true;

        // Handle onTap parameter (can be lambda function or string)
        if (args.size() >= 2) {
          auto tapAction = args[1];
          if (tapAction.is<BuiltinFunction>()) {
            auto fn = tapAction.get<BuiltinFunction>();
            onTap = std::function<void()>([this, fn]() {
              auto result = fn({});
              if (isError(result)) {
                std::cerr << "Error in tap action: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
              }
            });
          } else if (tapAction.is<std::string>()) {
            std::string cmd = tapAction.asString();
            onTap = [this, cmd]() { io->Send(cmd); };
          }
        }

        // Handle tapCondition parameter (string or lambda function)
        if (args.size() >= 3) {
          auto condition = args[2];
          if (condition.is<std::string>()) {
            tapCondition = condition.asString();
          } else if (condition.is<BuiltinFunction>()) {
            auto func = condition.get<BuiltinFunction>();
            tapCondition = [this, func]() -> bool {
              auto result = func({});
              if (isError(result)) {
                std::cerr << "Error in tap condition: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
                return false;
              }
              return ExecResultToBool(result);
            };
          }
        }

        // Handle onCombo parameter (lambda function)
        if (args.size() >= 4) {
          auto comboAction = args[3];
          if (comboAction.is<BuiltinFunction>()) {
            auto func = comboAction.get<BuiltinFunction>();
            onCombo = [this, func]() {
              auto result = func({});
              if (isError(result)) {
                std::cerr << "Error in combo action: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
              }
            };
          }
        }

        auto keyTap = createKeyTap(keyName, onTap, tapCondition, comboCondition,
                                   onCombo, grabDown, grabUp);
        if (!keyTap) {
          return HavelRuntimeError("Failed to create KeyTap for: " + keyName);
        }
        return HavelValue(keyName + " KeyTap created successfully");
      })));
  
  // Load host modules (window, hotkey, audio, etc.)
  // This bridges the pure language runtime with the host system
  modules::loadHostModules(*environment, this);
}

void Interpreter::InitializeSystemBuiltins() {
  // Define boolean constants
  environment->Define("true", HavelValue(true));
  environment->Define("false", HavelValue(false));
  environment->Define("null", HavelValue(nullptr));

  environment->Define(
      "print", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     for (const auto &arg : args) {
                       std::cout << this->ValueToString(arg) << " ";
                     }
                     std::cout << std::endl;
                     std::cout.flush();
                     return HavelValue(nullptr);
                   }));

  // Helper function to parse duration string (e.g., "30s", "1h30m20s", "3:10:25.250")
  auto parseDuration = [](const std::string &durationStr) -> long long {
    long long totalMs = 0;
    
    // Try HH:MM:SS.mmm format first
    std::regex timeRegex(R"((\d+):(\d+):(\d+)(?:\.(\d+))?)");
    std::smatch timeMatch;
    if (std::regex_match(durationStr, timeMatch, timeRegex)) {
      long long hours = std::stoll(timeMatch[1].str());
      long long minutes = std::stoll(timeMatch[2].str());
      long long seconds = std::stoll(timeMatch[3].str());
      long long millis = 0;
      if (timeMatch[4].matched) {
        std::string msStr = timeMatch[4].str();
        while (msStr.length() < 3) msStr += "0";
        millis = std::stoll(msStr.substr(0, 3));
      }
      return ((hours * 3600 + minutes * 60 + seconds) * 1000) + millis;
    }
    
    // Try HH:MM format
    std::regex shortTimeRegex(R"((\d+):(\d+))");
    std::smatch shortMatch;
    if (std::regex_match(durationStr, shortMatch, shortTimeRegex)) {
      long long hours = std::stoll(shortMatch[1].str());
      long long minutes = std::stoll(shortMatch[2].str());
      return (hours * 3600 + minutes * 60) * 1000;
    }
    
    // Try unit-based format (e.g., "1h30m20s", "30s", "5m")
    std::regex unitRegex(R"((\d+)(ms|s|m|h|d|w))", std::regex::icase);
    auto begin = std::sregex_iterator(durationStr.begin(), durationStr.end(), unitRegex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it) {
      long long value = std::stoll((*it)[1].str());
      std::string unit = (*it)[2].str();
      
      // Convert to lowercase for comparison
      std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
      
      if (unit == "ms") {
        totalMs += value;
      } else if (unit == "s") {
        totalMs += value * 1000;
      } else if (unit == "m" || unit == "min") {
        totalMs += value * 60 * 1000;
      } else if (unit == "h") {
        totalMs += value * 3600 * 1000;
      } else if (unit == "d") {
        totalMs += value * 24 * 3600 * 1000;
      } else if (unit == "w") {
        totalMs += value * 7 * 24 * 3600 * 1000;
      }
    }
    
    // If no units found, assume milliseconds
    if (totalMs == 0) {
      try {
        totalMs = std::stoll(durationStr);
      } catch (...) {
        // Return 0 if parsing fails
      }
    }
    
    return totalMs;
  };

  // Helper function to parse time string and calculate milliseconds until that time
  auto parseTimeUntil = [parseDuration](const std::string &timeStr) -> long long {
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localNow;
    localtime_r(&nowTime, &localNow);
    
    // Try "HH:MM" or "HH:MM:SS" format
    std::regex timeRegex(R"((\d{1,2}):(\d{2})(?::(\d{2}))?)");
    std::smatch timeMatch;
    if (std::regex_match(timeStr, timeMatch, timeRegex)) {
      int targetHour = std::stoi(timeMatch[1].str());
      int targetMinute = std::stoi(timeMatch[2].str());
      int targetSecond = timeMatch[3].matched ? std::stoi(timeMatch[3].str()) : 0;
      
      std::tm targetTime = localNow;
      targetTime.tm_hour = targetHour;
      targetTime.tm_min = targetMinute;
      targetTime.tm_sec = targetSecond;
      
      auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
      
      // If target time has passed today, add 24 hours
      if (duration.count() < 0) {
        duration += std::chrono::hours(24);
      }
      
      return duration.count();
    }
    
    // Try day name format: "thursday 8:00", "monday 14:30"
    std::regex dayTimeRegex(R"((\w+)\s+(\d{1,2}):(\d{2})(?::(\d{2}))?)");
    std::smatch dayMatch;
    if (std::regex_match(timeStr, dayMatch, dayTimeRegex)) {
      std::string dayName = dayMatch[1].str();
      int targetHour = std::stoi(dayMatch[2].str());
      int targetMinute = std::stoi(dayMatch[3].str());
      int targetSecond = dayMatch[4].matched ? std::stoi(dayMatch[4].str()) : 0;
      
      // Convert day name to day of week (0=Sunday, 1=Monday, etc.)
      std::transform(dayName.begin(), dayName.end(), dayName.begin(), ::tolower);
      int targetDay = -1;
      if (dayName == "sunday" || dayName == "sun") targetDay = 0;
      else if (dayName == "monday" || dayName == "mon") targetDay = 1;
      else if (dayName == "tuesday" || dayName == "tue") targetDay = 2;
      else if (dayName == "wednesday" || dayName == "wed") targetDay = 3;
      else if (dayName == "thursday" || dayName == "thu") targetDay = 4;
      else if (dayName == "friday" || dayName == "fri") targetDay = 5;
      else if (dayName == "saturday" || dayName == "sat") targetDay = 6;
      
      if (targetDay >= 0) {
        std::tm targetTime = localNow;
        targetTime.tm_hour = targetHour;
        targetTime.tm_min = targetMinute;
        targetTime.tm_sec = targetSecond;
        targetTime.tm_mday += (targetDay - localNow.tm_wday + 7) % 7;
        
        // If today and time has passed, or future day, adjust
        if (targetDay == localNow.tm_wday) {
          auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
          if (duration.count() < 0) {
            targetTime.tm_mday += 7;
          }
        }
        
        auto targetTimestamp = std::chrono::system_clock::from_time_t(mktime(&targetTime));
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(targetTimestamp - now);
        return duration.count();
      }
    }
    
    // Fall back to duration parsing
    return parseDuration(timeStr);
  };

  // Core verb functions - global for fast typing
  // sleep(ms) or sleep("30s") or sleep("1h30m") or sleep("3:10:25")
  environment->Define(
      "sleep",
      BuiltinFunction(
          [this, parseDuration](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("sleep() requires duration");
            
            long long ms = 0;
            if (args[0].isString()) {
              ms = parseDuration(args[0].asString());
            } else {
              ms = static_cast<long long>(ValueToNumber(args[0]));
            }
            
            if (ms < 0) {
              return HavelRuntimeError("sleep() duration must be non-negative");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return HavelValue(nullptr);
          }));

  // sleepUntil("HH:MM") or sleepUntil("thursday 8:00")
  environment->Define(
      "sleepUntil",
      BuiltinFunction(
          [this, parseTimeUntil](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("sleepUntil() requires time string");
            
            std::string timeStr = args[0].isString() ? args[0].asString() : ValueToString(args[0]);
            long long ms = parseTimeUntil(timeStr);
            
            if (ms < 0) {
              return HavelRuntimeError("sleepUntil(): invalid time format");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return HavelValue(nullptr);
          }));

  // Global send function (alias to io.send)
  environment->Define(
      "send", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("send() requires keys string");
                    std::string keys = ValueToString(args[0]);
                    io->Send(keys.c_str());
                    return HavelValue(nullptr);
                  }));

  // Global play function (alias to media.play)
  environment->Define(
      "play", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (auto app = HavelApp::instance) {
                      if (app->mpv) {
                        app->mpv->PlayPause();
                        return HavelValue(true);
                      }
                    }
                    return HavelRuntimeError("MPVController not available");
                  }));

  // Global exit function
  environment->Define(
      "exit", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (App::instance()) {
                      App::quit();
                    }
                    return HavelValue(nullptr);
                  }));

  // Global file operations
  environment->Define(
      "read", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("read() requires file path");
                    std::string path = ValueToString(args[0]);
                    try {
                      FileManager file(path);
                      return HavelValue(file.read());
                    } catch (const std::exception &e) {
                      return HavelRuntimeError("Failed to read file: " +
                                               std::string(e.what()));
                    }
                  }));

  environment->Define(
      "write", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.size() < 2)
                       return HavelRuntimeError(
                           "write() requires file path and content");
                     std::string path = ValueToString(args[0]);
                     std::string content = ValueToString(args[1]);
                     try {
                       FileManager file(path);
                       file.write(content);
                       return HavelValue(true);
                     } catch (const std::exception &e) {
                       return HavelRuntimeError("Failed to write file: " +
                                                std::string(e.what()));
                     }
                   }));

  // repeat(n, fn)
  environment->Define(
      "repeat",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("repeat() requires (count, function)");
        int count = static_cast<int>(ValueToNumber(args[0]));
        const HavelValue &fn = args[1];
        for (int i = 0; i < count; ++i) {
          std::vector<HavelValue> fnArgs = {HavelValue(static_cast<double>(i))};
          HavelResult res;
          if (auto *builtin = fn.get_if<BuiltinFunction>()) {
            res = (*builtin)(fnArgs);
          } else if (auto *userFunc =
                         fn.get_if<std::shared_ptr<HavelFunction>>()) {
            auto &func = *userFunc;
            auto funcEnv = std::make_shared<Environment>(func->closure);
            for (size_t p = 0;
                 p < func->declaration->parameters.size() && p < fnArgs.size();
                 ++p) {
              funcEnv->Define(func->declaration->parameters[p]->paramName->symbol,
                              fnArgs[p]);
            }
            auto originalEnv = this->environment;
            this->environment = funcEnv;
            res = Evaluate(*func->declaration->body);
            this->environment = originalEnv;
            if (isError(res))
              return res;
          } else {
            return HavelRuntimeError("repeat() requires callable function");
          }
          if (isError(res))
            return res;
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "log", BuiltinFunction(
                 [this](const std::vector<HavelValue> &args) -> HavelResult {
                   std::cout << "[LOG] ";
                   for (const auto &arg : args) {
                     std::cout << this->ValueToString(arg) << " ";
                   }
                   std::cerr << std::endl;
                   std::cerr.flush();
                   return HavelValue(nullptr);
                 }));

  // === MODE SYSTEM FUNCTIONS ===
  // Create mode object with proper namespace
  auto modeObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Get current mode
  (*modeObj)["get"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        // Try HotkeyManager first for authoritative mode
        if (hotkeyManager) {
          return HavelValue(hotkeyManager->getMode());
        }
        // Fallback to environment variable
        auto currentModeOpt = environment->Get("__current_mode__");
        std::string currentMode = "default";
        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          currentMode = (*currentModeOpt).get<std::string>();
        }
        return HavelValue(currentMode);
      });

  // Set current mode
  (*modeObj)["set"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("mode.set() requires mode name");
        std::string newMode = this->ValueToString(args[0]);

        // Store previous mode
        auto currentModeOpt = environment->Get("__current_mode__");
        if (currentModeOpt) {
          environment->Define("__previous_mode__", *currentModeOpt);
        } else {
          environment->Define("__previous_mode__",
                              HavelValue(std::string("default")));
        }

        // Set new current mode in environment
        environment->Define("__current_mode__", HavelValue(newMode));

        // Sync with HotkeyManager
        if (hotkeyManager) {
          hotkeyManager->setMode(newMode);
          // Trigger conditional hotkey update
          hotkeyManager->updateAllConditionalHotkeys();
        }

        return HavelValue(nullptr);
      });

  // Switch to previous mode
  (*modeObj)["toggle"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        auto currentModeOpt = environment->Get("__current_mode__");
        auto previousModeOpt = environment->Get("__previous_mode__");

        std::string currentMode = "default";
        std::string previousMode = "default";

        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          currentMode = (*currentModeOpt).get<std::string>();
        }
        if (previousModeOpt && (*previousModeOpt).is<std::string>()) {
          previousMode = (*previousModeOpt).get<std::string>();
        }

        // Swap modes in environment
        environment->Define("__previous_mode__", HavelValue(currentMode));
        environment->Define("__current_mode__", HavelValue(previousMode));
        
        // Sync with HotkeyManager
        if (hotkeyManager) {
          hotkeyManager->setMode(previousMode);
          // Trigger conditional hotkey update
          hotkeyManager->updateAllConditionalHotkeys();
        }
        
        return HavelValue(nullptr);
      });

  // Check if in specific mode
  (*modeObj)["is"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("mode.is() requires mode name");
        std::string checkMode = this->ValueToString(args[0]);

        auto currentModeOpt = environment->Get("__current_mode__");
        if (currentModeOpt && (*currentModeOpt).is<std::string>()) {
          std::string currentMode = (*currentModeOpt).get<std::string>();
          return HavelValue(currentMode == checkMode);
        }
        return HavelValue(false);
      });

  // Define the mode object
  environment->Define("mode", HavelValue(modeObj));

  environment->Define(
      "error", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     std::cerr << "[ERROR] ";
                     for (const auto &arg : args) {
                       std::cerr << this->ValueToString(arg) << " ";
                     }
                     std::cerr << std::endl;
                     std::cerr.flush();
                     return HavelValue(nullptr);
                   }));

  environment->Define(
      "fatal", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     std::cerr << "[FATAL] ";
                     for (const auto &arg : args) {
                       std::cerr << this->ValueToString(arg) << " ";
                     }
                     std::cerr << std::endl;
                     std::cerr.flush();
                     std::exit(1);
                     return HavelValue(nullptr);
                   }));

  environment->Define(
      "sleep",
      BuiltinFunction([this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("sleep() requires milliseconds");
        double ms = ValueToNumber(args[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds((int)ms));
        
        // Return chainable sleep result with send() method
        auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*result)["send"] = BuiltinFunction([this](const std::vector<HavelValue> &args) -> HavelResult {
          if (args.empty())
            return HavelRuntimeError("send() requires text");
          std::string text = this->ValueToString(args[0]);
          this->io->Send(text.c_str());
          return HavelValue(nullptr);
        });
        return HavelValue(result);
      }));

  environment->Define(
      "exit",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        int code = args.empty() ? 0 : (int)ValueToNumber(args[0]);
        std::exit(code);
        return HavelValue(nullptr);
      }));
  environment->Define(
      "type",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("type() requires an argument");

        // Check type using variant index
        switch (args[0].data.index()) {
          case 0: return HavelValue(std::string("null"));       // nullptr_t
          case 1: return HavelValue(std::string("boolean"));    // bool
          case 2: return HavelValue(std::string("number"));     // int
          case 3: return HavelValue(std::string("number"));     // double
          case 4: return HavelValue(std::string("string"));     // std::string
          case 5: return HavelValue(std::string("array"));      // HavelArray
          case 6: return HavelValue(std::string("object"));     // HavelObject
          case 7: return HavelValue(std::string("set"));        // HavelSet
          case 8: return HavelValue(std::string("function"));   // shared_ptr<HavelFunction>
          case 9: return HavelValue(std::string("channel"));    // shared_ptr<Channel>
          case 10: return HavelValue(std::string("builtin"));   // BuiltinFunction
          default: return HavelValue(std::string("unknown"));
        }
      }));

  // implements(obj, traitName) - check if object's type implements a trait
  environment->Define(
      "implements",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("implements() requires (object, traitName)");
        
        // Get type name from object
        std::string typeName;
        if (args[0].isStructInstance()) {
          typeName = args[0].asStructInstance().typeName;
        } else {
          return HavelValue(false);  // Non-struct types don't implement traits
        }
        
        std::string traitName = args[1].isString() ? args[1].asString() : "";
        
        // Check trait registry
        bool result = TraitRegistry::getInstance().implements(typeName, traitName);
        return HavelValue(result);
      }));

  // approx(a, b, epsilon) - fuzzy comparison for floating point (relative tolerance)
  environment->Define(
      "approx",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("approx() requires at least 2 arguments");
        
        double a = args[0].isNumber() ? args[0].asNumber() : 0.0;
        double b = args[1].isNumber() ? args[1].asNumber() : 0.0;
        double eps = (args.size() >= 3 && args[2].isNumber()) ? args[2].asNumber() : 1e-9;
        
        // Use relative tolerance for large magnitude values
        double diff = std::abs(a - b);
        double maxVal = std::max({1.0, std::abs(a), std::abs(b)});
        
        return HavelValue(diff <= eps * maxVal);
      }));

  // Send text/keys to the system
  environment->Define(
      "send", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("send() requires text");
                    std::string text = this->ValueToString(args[0]);
                    this->io->Send(text.c_str());
                    return HavelValue(nullptr);
                  }));

  // POSIX signal constants (used by some hotkey conversions)
  environment->Define("SIGSTOP", HavelValue(static_cast<double>(SIGSTOP)));
  environment->Define("SIGCONT", HavelValue(static_cast<double>(SIGCONT)));
  environment->Define("SIGKILL", HavelValue(static_cast<double>(SIGKILL)));

  environment->Define(
      "hotkey.toggleOverlay",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->toggleFakeDesktopOverlay();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.showBlackOverlay",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->showBlackOverlay();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.printActiveWindowInfo",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->printActiveWindowInfo();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.toggleWindowFocusTracking",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->toggleWindowFocusTracking();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "hotkey.updateConditional",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->updateAllConditionalHotkeys();
            return HavelValue(nullptr);
          }));

  // hotkey.clearAll() - Clear all registered hotkeys
  environment->Define(
      "hotkey.clearAll",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            hotkeyManager->clearAllHotkeys();
            return HavelValue(nullptr);
          }));

  // hotkey.list() - List all registered hotkeys
  environment->Define(
      "hotkey.list",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");

            auto list = hotkeyManager->getHotkeyList();
            auto resultArray = std::make_shared<std::vector<HavelValue>>();
            for (const auto& hk : list) {
              auto hkObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
              (*hkObj)["alias"] = HavelValue(hk.alias);
              (*hkObj)["id"] = HavelValue(static_cast<double>(hk.id));
              (*hkObj)["enabled"] = HavelValue(hk.enabled);
              resultArray->push_back(HavelValue(hkObj));
            }
            return HavelValue(resultArray);
          }));

  // hotkey.getConditional() - Get all conditional hotkeys
  environment->Define(
      "hotkey.getConditional",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");

            auto list = hotkeyManager->getConditionalHotkeyList();
            auto resultArray = std::make_shared<std::vector<HavelValue>>();
            for (const auto& ch : list) {
              auto chObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
              (*chObj)["key"] = HavelValue(ch.key);
              (*chObj)["condition"] = HavelValue(ch.condition);
              (*chObj)["enabled"] = HavelValue(ch.enabled);
              (*chObj)["active"] = HavelValue(ch.active);
              resultArray->push_back(HavelValue(chObj));
            }
            return HavelValue(resultArray);
          }));

  // hotkey.monitor() - Enable/disable conditional hotkey monitoring
  environment->Define(
      "hotkey.monitor",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            
            bool enable = args.empty() || (args[0].isBool() && args[0].asBool());
            hotkeyManager->setConditionalHotkeysEnabled(enable);
            return HavelValue(enable);
          }));

  // hotkey.isMonitoring() - Check if conditional hotkey monitoring is enabled
  environment->Define(
      "hotkey.isMonitoring",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!hotkeyManager)
              return HavelRuntimeError("HotkeyManager not available");
            
            return HavelValue(hotkeyManager->getConditionalHotkeysEnabled());
          }));

  auto hotkeyObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("hotkey.toggleOverlay"))
    (*hotkeyObj)["toggleOverlay"] = *v;
  if (auto v = environment->Get("hotkey.showBlackOverlay"))
    (*hotkeyObj)["showBlackOverlay"] = *v;
  if (auto v = environment->Get("hotkey.printActiveWindowInfo"))
    (*hotkeyObj)["printActiveWindowInfo"] = *v;
  if (auto v = environment->Get("hotkey.toggleWindowFocusTracking"))
    (*hotkeyObj)["toggleWindowFocusTracking"] = *v;
  if (auto v = environment->Get("hotkey.updateConditional"))
    (*hotkeyObj)["updateConditional"] = *v;
  if (auto v = environment->Get("hotkey.clearAll"))
    (*hotkeyObj)["clearAll"] = *v;
  if (auto v = environment->Get("hotkey.list"))
    (*hotkeyObj)["list"] = *v;
  if (auto v = environment->Get("hotkey.getConditional"))
    (*hotkeyObj)["getConditional"] = *v;
  if (auto v = environment->Get("hotkey.monitor"))
    (*hotkeyObj)["monitor"] = *v;
  if (auto v = environment->Get("hotkey.isMonitoring"))
    (*hotkeyObj)["isMonitoring"] = *v;
  environment->Define("hotkey", HavelValue(hotkeyObj));

  environment->Define(
      "Hotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2 || args.size() > 4)
          return HavelRuntimeError("Hotkey requires 2–4 arguments");

        if (!hotkeyManager)
          return HavelValue(nullptr);

        std::string key = ValueToString(args[0]);

        // --- helper: convert value → void() ---
        auto toVoidAction =
            [this](const HavelValue &v) -> std::function<void()> {
          if (v.is<std::string>()) {
            std::string cmd = v.get<std::string>();
            return [cmd]() { Launcher::runShellDetached(cmd.c_str()); };
          }

          if (v.is<std::shared_ptr<HavelFunction>>()) {
            auto fn = v.get<std::shared_ptr<HavelFunction>>();
            return [this, fn]() { this->Evaluate(*fn->declaration); };
          }

          if (v.is<BuiltinFunction>()) {
            auto fn = v.get<BuiltinFunction>();
            return [fn]() { fn({}); };
          }

          throw HavelRuntimeError("Invalid action type");
        };

        // --- helper: convert value → bool() ---
        auto toBoolCondition = [this](const HavelValue &v)
            -> std::variant<std::string, std::function<bool()>> {
          if (v.is<std::string>()) {
            return v.asString();
          }

          if (v.is<std::shared_ptr<HavelFunction>>()) {
            auto fn = v.get<std::shared_ptr<HavelFunction>>();
            return std::function<bool()>([this, fn]() {
              auto result = this->Evaluate(*fn->declaration);
              if (isError(result)) {
                std::cerr << "Error in condition: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
                return false;
              }
              return ExecResultToBool(result);
            });
          }

          if (v.is<BuiltinFunction>()) {
            auto fn = v.get<BuiltinFunction>();
            return std::function<bool()>([fn]() {
              auto result = fn({});
              if (isError(result)) {
                std::cerr << "Error in condition: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
                return false;
              }
              return ExecResultToBool(result);
            });
          }

          throw HavelRuntimeError("Invalid condition type");
        };

        // ---------- CASE 2 args ----------
        if (args.size() == 2) {
          hotkeyManager->AddHotkey(key, toVoidAction(args[1]));
          return HavelValue(nullptr);
        }

        // ---------- CASE 3 or 4 args ----------
        auto condition = toBoolCondition(args[2]);
        auto trueAction = toVoidAction(args[1]);

        std::function<void()> falseAction = nullptr;
        if (args.size() == 4)
          falseAction = toVoidAction(args[3]);

        // string condition
        if (condition.index() == 0) {
          return HavelValue(hotkeyManager->AddContextualHotkey(
              key, std::get<std::string>(condition), trueAction, falseAction));
        }

        // lambda condition
        return HavelValue(hotkeyManager->AddContextualHotkey(
            key, std::get<std::function<bool()>>(condition), trueAction,
            falseAction));
      }));
  // Process helpers
  environment->Define(
      "process.getState",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("process.getState() requires pid");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        auto state = havel::ProcessManager::getProcessState(pid);
        switch (state) {
        case havel::ProcessManager::ProcessState::RUNNING:
          return HavelValue(std::string("RUNNING"));
        case havel::ProcessManager::ProcessState::SLEEPING:
          return HavelValue(std::string("SLEEPING"));
        case havel::ProcessManager::ProcessState::ZOMBIE:
          return HavelValue(std::string("ZOMBIE"));
        case havel::ProcessManager::ProcessState::STOPPED:
          return HavelValue(std::string("STOPPED"));
        case havel::ProcessManager::ProcessState::NO_PERMISSION:
          return HavelValue(std::string("NO_PERMISSION"));
        case havel::ProcessManager::ProcessState::NOT_FOUND:
          return HavelValue(std::string("NOT_FOUND"));
        }
        return HavelValue(std::string("UNKNOWN"));
      }));

  environment->Define(
      "process.sendSignal",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "process.sendSignal() requires (pid, signal)");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int sig = static_cast<int>(ValueToNumber(args[1]));
        return HavelValue(havel::ProcessManager::sendSignal(pid, sig));
      }));

  environment->Define(
      "process.kill",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("process.kill() requires (pid, signal)");
        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        std::string signalStr = ValueToString(args[1]);

        // Convert signal string to number
        int signal = SIGTERM; // default
        if (signalStr == "SIGTERM") {
          signal = SIGTERM;
        } else if (signalStr == "SIGKILL") {
          signal = SIGKILL;
        } else if (signalStr == "SIGINT") {
          signal = SIGINT;
        } else {
          // Try to parse as number
          try {
            signal = std::stoi(signalStr);
          } catch (...) {
            return HavelRuntimeError("Invalid signal: " + signalStr);
          }
        }

        return HavelValue(havel::ProcessManager::sendSignal(pid, signal));
      }));

  environment->Define(
      "process.exists",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "process.exists() requires pid or process name");

        // Check if argument is a number (PID) or string (process
        // name)
        if (args[0].is<double>()) {
          pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
          return HavelValue(havel::ProcessManager::isProcessAlive(pid));
        } else {
          // Search by process name
          std::string name = ValueToString(args[0]);
          auto processes = havel::ProcessManager::findProcesses(name);
          return HavelValue(!processes.empty());
        }
      }));

  environment->Define(
      "process.find",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("process.find() requires process name");

        std::string name = ValueToString(args[0]);
        auto processes = havel::ProcessManager::findProcesses(name);

        // Convert to array of process info objects
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto &proc : processes) {
          auto procObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*procObj)["pid"] = HavelValue(static_cast<double>(proc.pid));
          (*procObj)["ppid"] = HavelValue(static_cast<double>(proc.ppid));
          (*procObj)["name"] = HavelValue(proc.name);
          (*procObj)["command"] = HavelValue(proc.command);
          (*procObj)["user"] = HavelValue(proc.user);
          (*procObj)["cpu_usage"] = HavelValue(proc.cpu_usage);
          (*procObj)["memory_usage"] =
              HavelValue(static_cast<double>(proc.memory_usage));
          resultArray->push_back(HavelValue(procObj));
        }

        return HavelValue(resultArray);
      }));

  environment->Define(
      "process.nice",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("process.nice() requires (pid, nice_value)");

        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int niceValue = static_cast<int>(ValueToNumber(args[1]));

        return HavelValue(
            havel::ProcessManager::setProcessNice(pid, niceValue));
      }));

  environment->Define(
      "process.ionice",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 3)
          return HavelRuntimeError(
              "process.ionice() requires (pid, class, data)");

        pid_t pid = static_cast<pid_t>(ValueToNumber(args[0]));
        int ioclass = static_cast<int>(ValueToNumber(args[1]));
        int iodata = static_cast<int>(ValueToNumber(args[2]));

        return HavelValue(
            havel::ProcessManager::setProcessIoPriority(pid, ioclass, iodata));
      }));

  auto processObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("process.getState"))
    (*processObj)["getState"] = *v;
  if (auto v = environment->Get("process.sendSignal"))
    (*processObj)["sendSignal"] = *v;
  if (auto v = environment->Get("process.kill"))
    (*processObj)["kill"] = *v;
  if (auto v = environment->Get("process.exists"))
    (*processObj)["exists"] = *v;
  if (auto v = environment->Get("process.find"))
    (*processObj)["find"] = *v;
  if (auto v = environment->Get("process.nice"))
    (*processObj)["nice"] = *v;
  if (auto v = environment->Get("process.ionice"))
    (*processObj)["ionice"] = *v;
  if (auto v = environment->Get("SIGSTOP"))
    (*processObj)["SIGSTOP"] = *v;
  if (auto v = environment->Get("SIGCONT"))
    (*processObj)["SIGCONT"] = *v;
  if (auto v = environment->Get("SIGKILL"))
    (*processObj)["SIGKILL"] = *v;
  environment->Define("process", HavelValue(processObj));

  // === CONFIG MODULE ===
  // Create config object with proper namespace
  auto configObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  (*configObj)["get"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("config.get() requires key");
        std::string key = ValueToString(args[0]);
        std::string def =
            args.size() >= 2 ? ValueToString(args[1]) : std::string("");
        auto &config = Configs::Get();
        return HavelValue(config.Get<std::string>(key, def));
      });

  (*configObj)["set"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("config.set() requires (key, value)");
        std::string key = ValueToString(args[0]);
        auto &config = Configs::Get();
        const HavelValue &value = args[1];

        if (value.is<bool>()) {
          config.Set(key, value.get<bool>());
        } else if (value.is<int>()) {
          config.Set(key, value.get<int>());
        } else if (value.is<double>()) {
          config.Set(key, value.get<double>());
        } else {
          config.Set(key, ValueToString(value));
        }
        return HavelValue(true);
      });
  // config.setPath
  (*configObj)["setPath"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 1)
          return HavelRuntimeError("config.setPath() requires (path)");
        std::string path = ValueToString(args[0]);
        auto &config = Configs::Get();
        config.SetPath(path);
        return HavelValue(true);
      });
  // Add load function to config object
  (*configObj)["load"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        try {
          auto &config = Configs::Get();
          if (args.empty()) {
            config.Reload();
          } else {
            config.Load(ValueToString(args[0]));
          }
          std::cout << "[INFO] Configuration loaded successfully" << std::endl;
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to load configuration: " +
                                   std::string(e.what()));
        }
      });

  // Add reload function to config object
  (*configObj)["reload"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        try {
          auto &config = Configs::Get();
          config.Reload();
          std::cout << "[INFO] Configuration reloaded successfully"
                    << std::endl;
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to reload configuration: " +
                                   std::string(e.what()));
        }
      });

  // Define the config object
  environment->Define("config", HavelValue(configObj));

  // === APP MODULE ===
  // Create app module object
  auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Add quit function to app module - hard exit to kill all threads
  (*appObj)["quit"] = BuiltinFunction(
    [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        info("Quit requested - performing hard exit");

        // Stop EventListener FIRST to prevent use-after-free in KeyMap access
        if (io->GetEventListener()) {
          info("Stopping EventListener before exit...");
          io->GetEventListener()->Stop();
          info("EventListener stopped");
        }

        std::exit(0);
      });

  // Add restart function to app module
  (*appObj)["restart"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;

        if (QApplication::instance()) {
          // Use Qt's proper restart mechanism
          QCoreApplication::exit(42); // Use special exit code for restart
          return HavelValue(true);
        }

        return HavelRuntimeError("App is not running");
      });

  // Add info function to app module
  (*appObj)["info"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;

        auto infoObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        auto pid = ProcessManager::getCurrentPid();
        (*infoObj)["pid"] = HavelValue(static_cast<double>(pid));

        std::string exec = ProcessManager::getProcessExecutablePath(pid);
        (*infoObj)["path"] = HavelValue(exec);

        (*infoObj)["version"] = HavelValue("2.0.0");
        (*infoObj)["name"] = HavelValue("Havel");

        return HavelValue(infoObj);
      });

  // Add args function to app module
  (*appObj)["args"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        // For now, return empty array - would need to store command line
        // args at startup
        auto arr = std::make_shared<std::vector<HavelValue>>();
        return HavelValue(arr);
      });

  // Define the app module
  environment->Define("app", HavelValue(appObj));

  // === IO METHODS ===
  // Key state methods
  environment->Define(
      "io->getKeyState",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("io->getKeyState() requires key name");
            std::string key = this->ValueToString(args[0]);
            return HavelValue(this->io->GetKeyState(key));
          }));

  environment->Define(
      "io->isShiftPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsShiftPressed());
          }));

  environment->Define(
      "io->isCtrlPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsCtrlPressed());
          }));

  environment->Define(
      "io->isAltPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsAltPressed());
          }));

  environment->Define(
      "io->isWinPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io->IsWinPressed());
          }));

  // === AUDIO MANAGER METHODS ===
  // Volume control
  environment->Define(
      "audio.setVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setVolume() requires volume (0.0-1.0)");
            if (args.size() >= 2) {
              std::string device = this->ValueToString(args[0]);
              double volume = ValueToNumber(args[1]);
              return HavelValue(this->audioManager->setVolume(device, volume));
            }
            double volume = ValueToNumber(args[0]);
            return HavelValue(this->audioManager->setVolume(volume));
          }));

  environment->Define(
      "audio.getVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!args.empty()) {
              std::string device = this->ValueToString(args[0]);
              return HavelValue(this->audioManager->getVolume(device));
            }
            return HavelValue(this->audioManager->getVolume());
          }));

  environment->Define(
      "audio.increaseVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() >= 2) {
          std::string device = this->ValueToString(args[0]);
          double amount = ValueToNumber(args[1]);
          return HavelValue(this->audioManager->increaseVolume(device, amount));
        }
        if (args.size() == 1) {
          std::string device = this->ValueToString(args[0]);
          return HavelValue(this->audioManager->increaseVolume(device, 0.05));
        }
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        return HavelValue(this->audioManager->increaseVolume(amount));
      }));

  environment->Define(
      "audio.decreaseVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() >= 2) {
          std::string device = this->ValueToString(args[0]);
          double amount = ValueToNumber(args[1]);
          return HavelValue(this->audioManager->decreaseVolume(device, amount));
        }
        if (args.size() == 1) {
          std::string device = this->ValueToString(args[0]);
          return HavelValue(this->audioManager->decreaseVolume(device, 0.05));
        }
        double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
        return HavelValue(this->audioManager->decreaseVolume(amount));
      }));

  // Mute control
  environment->Define(
      "audio.toggleMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->toggleMute());
          }));

  environment->Define(
      "audio.setMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("audio.setMute() requires boolean");
            bool muted = args[0].get<bool>();
            return HavelValue(this->audioManager->setMute(muted));
          }));

  environment->Define(
      "audio.isMuted",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->isMuted());
          }));

  environment->Define(
      "audio.getDevices",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto arr = std::make_shared<std::vector<HavelValue>>();
        const auto &devices = this->audioManager->getDevices();
        for (const auto &device : devices) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["name"] = HavelValue(device.name);
          (*obj)["description"] = HavelValue(device.description);
          (*obj)["index"] = HavelValue(static_cast<double>(device.index));
          (*obj)["isDefault"] = HavelValue(device.isDefault);
          (*obj)["isMuted"] = HavelValue(device.isMuted);
          (*obj)["volume"] = HavelValue(device.volume);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  environment->Define(
      "audio.findDeviceByIndex",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.findDeviceByIndex() requires index");
        uint32_t index = static_cast<uint32_t>(args[0].get<double>());
        const auto *device = this->audioManager->findDeviceByIndex(index);
        if (!device) {
          return HavelValue(nullptr);
        }
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["name"] = HavelValue(device->name);
        (*obj)["description"] = HavelValue(device->description);
        (*obj)["index"] = HavelValue(static_cast<double>(device->index));
        (*obj)["isDefault"] = HavelValue(device->isDefault);
        (*obj)["isMuted"] = HavelValue(device->isMuted);
        (*obj)["volume"] = HavelValue(device->volume);
        return HavelValue(obj);
      }));

  environment->Define(
      "audio.setDefaultOutputByIndex",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "audio.setDefaultOutputByIndex() requires index");
        uint32_t index = static_cast<uint32_t>(ValueToNumber(args[0]));
        const auto *device = this->audioManager->findDeviceByIndex(index);
        if (!device) {
          return HavelValue(false);
        }
        return HavelValue(this->audioManager->setDefaultOutput(device->name));
      }));

  environment->Define(
      "audio.findDeviceByName",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.findDeviceByName() requires name");
        std::string name = this->ValueToString(args[0]);
        const auto *device = this->audioManager->findDeviceByName(name);
        if (!device) {
          return HavelValue(nullptr);
        }
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["name"] = HavelValue(device->name);
        (*obj)["description"] = HavelValue(device->description);
        (*obj)["index"] = HavelValue(static_cast<double>(device->index));
        (*obj)["isDefault"] = HavelValue(device->isDefault);
        (*obj)["isMuted"] = HavelValue(device->isMuted);
        (*obj)["volume"] = HavelValue(device->volume);
        return HavelValue(obj);
      }));

  environment->Define(
      "audio.setDefaultOutput",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setDefaultOutput() requires device name");
            std::string device = this->ValueToString(args[0]);
            return HavelValue(this->audioManager->setDefaultOutput(device));
          }));

  environment->Define(
      "audio.getDefaultOutput",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(this->audioManager->getDefaultOutput());
          }));

  environment->Define(
      "audio.playTestSound",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(this->audioManager->playTestSound());
          }));

  // Application volume control
  environment->Define(
      "audio.setAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError(
                  "audio.setAppVolume() requires (appName, volume)");
            std::string appName = this->ValueToString(args[0]);
            double volume = args[1].get<double>();
            return HavelValue(
                this->audioManager->setApplicationVolume(appName, volume));
          }));

  environment->Define(
      "audio.getAppVolume",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("audio.getAppVolume() requires appName");
        std::string appName = this->ValueToString(args[0]);
        return HavelValue(this->audioManager->getApplicationVolume(appName));
      }));

  environment->Define(
      "audio.increaseAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.increaseAppVolume() requires appName");
            std::string appName = this->ValueToString(args[0]);
            double amount = args.size() > 1 ? args[1].get<double>() : 0.05;
            return HavelValue(
                this->audioManager->increaseApplicationVolume(appName, amount));
          }));

  environment->Define(
      "audio.decreaseAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.decreaseAppVolume() requires appName");
            std::string appName = this->ValueToString(args[0]);
            double amount = args.size() > 1 ? args[1].get<double>() : 0.05;
            return HavelValue(
                this->audioManager->decreaseApplicationVolume(appName, amount));
          }));

  // Active window application volume
  environment->Define(
      "audio.setActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "audio.setActiveAppVolume() requires volume");
            double volume = args[0].get<double>();
            return HavelValue(
                this->audioManager->setActiveApplicationVolume(volume));
          }));

  environment->Define(
      "audio.getActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->audioManager->getActiveApplicationVolume());
          }));

  environment->Define(
      "audio.increaseActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            double amount = args.empty() ? 0.05 : args[0].get<double>();
            return HavelValue(
                this->audioManager->increaseActiveApplicationVolume(amount));
          }));

  environment->Define(
      "audio.decreaseActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            double amount = args.empty() ? 0.05 : args[0].get<double>();
            return HavelValue(
                this->audioManager->decreaseActiveApplicationVolume(amount));
          }));

  // Get applications list
  environment->Define(
      "audio.getApplications",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto apps = this->audioManager->getApplications();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &app : apps) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["name"] = HavelValue(app.name);
          (*obj)["volume"] = HavelValue(app.volume);
          (*obj)["isMuted"] = HavelValue(app.isMuted);
          (*obj)["index"] = HavelValue(static_cast<double>(app.index));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  // Create audio module
  auto audioMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("audio.setVolume"))
    (*audioMod)["setVolume"] = *v;
  if (auto v = environment->Get("audio.getVolume"))
    (*audioMod)["getVolume"] = *v;
  if (auto v = environment->Get("audio.increaseVolume"))
    (*audioMod)["increaseVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseVolume"))
    (*audioMod)["decreaseVolume"] = *v;
  if (auto v = environment->Get("audio.toggleMute"))
    (*audioMod)["toggleMute"] = *v;
  if (auto v = environment->Get("audio.setMute"))
    (*audioMod)["setMute"] = *v;
  if (auto v = environment->Get("audio.isMuted"))
    (*audioMod)["isMuted"] = *v;
  if (auto v = environment->Get("audio.setAppVolume"))
    (*audioMod)["setAppVolume"] = *v;
  if (auto v = environment->Get("audio.getAppVolume"))
    (*audioMod)["getAppVolume"] = *v;
  if (auto v = environment->Get("audio.increaseAppVolume"))
    (*audioMod)["increaseAppVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseAppVolume"))
    (*audioMod)["decreaseAppVolume"] = *v;
  if (auto v = environment->Get("audio.setActiveAppVolume"))
    (*audioMod)["setActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.getActiveAppVolume"))
    (*audioMod)["getActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.increaseActiveAppVolume"))
    (*audioMod)["increaseActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseActiveAppVolume"))
    (*audioMod)["decreaseActiveAppVolume"] = *v;
  if (auto v = environment->Get("audio.getApplications"))
    (*audioMod)["getApplications"] = *v;
  if (auto v = environment->Get("audio.getDevices"))
    (*audioMod)["getDevices"] = *v;
  if (auto v = environment->Get("audio.findDeviceByIndex"))
    (*audioMod)["findDeviceByIndex"] = *v;
  if (auto v = environment->Get("audio.findDeviceByName"))
    (*audioMod)["findDeviceByName"] = *v;
  if (auto v = environment->Get("audio.setDefaultOutputByIndex"))
    (*audioMod)["setDefaultOutputByIndex"] = *v;
  if (auto v = environment->Get("audio.setDefaultOutput"))
    (*audioMod)["setDefaultOutput"] = *v;
  if (auto v = environment->Get("audio.getDefaultOutput"))
    (*audioMod)["getDefaultOutput"] = *v;
  if (auto v = environment->Get("audio.playTestSound"))
    (*audioMod)["playTestSound"] = *v;
  environment->Define("audio", HavelValue(audioMod));

  // === BROWSER MODULE ===
  // Browser automation via Chrome DevTools Protocol
  auto browserMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  (*browserMod)["connect"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url = args.empty() ? "http://localhost:9222"
                                       : this->ValueToString(args[0]);
        return HavelValue(getBrowser().connect(url));
      }));

  (*browserMod)["disconnect"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        getBrowser().disconnect();
        return HavelValue(true);
      }));

  (*browserMod)["isConnected"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().isConnected());
      }));

  (*browserMod)["open"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.open() requires URL");
        std::string url = this->ValueToString(args[0]);
        return HavelValue(getBrowser().open(url));
      }));

  (*browserMod)["newTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string url = args.empty() ? "" : this->ValueToString(args[0]);
        return HavelValue(getBrowser().newTab(url));
      }));

  (*browserMod)["goto"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.goto() requires URL");
        std::string url = this->ValueToString(args[0]);
        return HavelValue(getBrowser().gotoUrl(url));
      }));

  (*browserMod)["back"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().back());
      }));

  (*browserMod)["forward"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().forward());
      }));

  (*browserMod)["reload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        bool ignoreCache = !args.empty() && args[0].get<double>() != 0;
        return HavelValue(getBrowser().reload(ignoreCache));
      }));

  (*browserMod)["click"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.click() requires selector");
        std::string selector = this->ValueToString(args[0]);
        return HavelValue(getBrowser().click(selector));
      }));

  (*browserMod)["type"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("browser.type() requires (selector, text)");
        std::string selector = this->ValueToString(args[0]);
        std::string text = this->ValueToString(args[1]);
        return HavelValue(getBrowser().type(selector, text));
      }));

  (*browserMod)["setZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.setZoom() requires level (0.5-3.0)");
        double level = args[0].get<double>();
        return HavelValue(getBrowser().setZoom(level));
      }));

  (*browserMod)["getZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getZoom());
      }));

  (*browserMod)["resetZoom"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().resetZoom());
      }));

  (*browserMod)["eval"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.eval() requires JavaScript code");
        std::string js = this->ValueToString(args[0]);
        return HavelValue(getBrowser().eval(js));
      }));

  (*browserMod)["screenshot"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        std::string path = args.empty() ? "" : this->ValueToString(args[0]);
        return HavelValue(getBrowser().screenshot(path));
      }));

  (*browserMod)["getCurrentUrl"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getCurrentUrl());
      }));

  (*browserMod)["getTitle"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getTitle());
      }));

  (*browserMod)["listTabs"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto tabs = getBrowser().listTabs();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &tab : tabs) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(static_cast<double>(tab.id));
          (*obj)["idStr"] = HavelValue(tab.idStr);
          (*obj)["title"] = HavelValue(tab.title);
          (*obj)["url"] = HavelValue(tab.url);
          (*obj)["type"] = HavelValue(tab.type);
          (*obj)["webSocketUrl"] = HavelValue(tab.webSocketUrl);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["activate"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.activate() requires tabId");
        int tabId = static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().activate(tabId));
      }));

  (*browserMod)["close"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int tabId = args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().closeTab(tabId));
      }));

  (*browserMod)["closeAll"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().closeAll());
      }));

  (*browserMod)["getActiveTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        BrowserTab tab = getBrowser().getActiveTab();
        auto tabObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*tabObj)["id"] = HavelValue(tab.id);
        (*tabObj)["title"] = HavelValue(tab.title);
        (*tabObj)["url"] = HavelValue(tab.url);
        (*tabObj)["type"] = HavelValue(tab.type);
        return HavelValue(tabObj);
      }));

  (*browserMod)["getActiveTabTitle"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getActiveTabTitle());
      }));

  (*browserMod)["getActiveTabInfo"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getActiveTabInfo());
      }));

  (*browserMod)["setActiveTab"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.setActiveTab() requires tab index");
        int tabId = static_cast<int>(ValueToNumber(args[0]));
        getBrowser().setCurrentTabId(tabId);
        return HavelValue(true);
      }));

  // === New Browser Functions ===

  (*browserMod)["connectFirefox"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int port =
            args.empty() ? 2828 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().connectFirefox(port));
      }));

  (*browserMod)["setPort"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.setPort() requires port number");
        int port = static_cast<int>(args[0].get<double>());
        getBrowser().setPort(port);
        return HavelValue(true);
      }));

  (*browserMod)["getPort"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().getPort());
      }));

  (*browserMod)["getBrowserType"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto type = getBrowser().getBrowserType();
        std::string typeName = type == BrowserType::Firefox    ? "firefox"
                               : type == BrowserType::Chrome   ? "chrome"
                               : type == BrowserType::Chromium ? "chromium"
                               : type == BrowserType::Edge     ? "edge"
                               : type == BrowserType::Brave    ? "brave"
                                                               : "unknown";
        return HavelValue(typeName);
      }));

  (*browserMod)["getOpenBrowsers"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto browsers = getBrowser().getOpenBrowsers();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &b : browsers) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["type"] =
              HavelValue(b.type == BrowserType::Firefox  ? "firefox"
                         : b.type == BrowserType::Chrome ? "chrome"
                                                         : "chromium");
          (*obj)["name"] = HavelValue(b.name);
          (*obj)["pid"] = HavelValue(static_cast<double>(b.pid));
          (*obj)["cdpPort"] = HavelValue(static_cast<double>(b.cdpPort));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["getDefaultBrowser"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto browser = getBrowser().getDefaultBrowser();
        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["type"] =
            HavelValue(browser.type == BrowserType::Firefox  ? "firefox"
                       : browser.type == BrowserType::Chrome ? "chrome"
                                                             : "chromium");
        (*obj)["name"] = HavelValue(browser.name);
        (*obj)["path"] = HavelValue(browser.path);
        return HavelValue(obj);
      }));

  (*browserMod)["listWindows"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto windows = getBrowser().listWindows();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &w : windows) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(static_cast<double>(w.id));
          (*obj)["x"] = HavelValue(static_cast<double>(w.x));
          (*obj)["y"] = HavelValue(static_cast<double>(w.y));
          (*obj)["width"] = HavelValue(static_cast<double>(w.width));
          (*obj)["height"] = HavelValue(static_cast<double>(w.height));
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["listExtensions"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto extensions = getBrowser().listExtensions();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto &e : extensions) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(e.id);
          (*obj)["name"] = HavelValue(e.name);
          (*obj)["version"] = HavelValue(e.version);
          (*obj)["enabled"] = HavelValue(e.enabled);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["enableExtension"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.enableExtension() requires extensionId");
        std::string extId = this->ValueToString(args[0]);
        return HavelValue(getBrowser().enableExtension(extId));
      }));

  (*browserMod)["disableExtension"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "browser.disableExtension() requires extensionId");
        std::string extId = this->ValueToString(args[0]);
        return HavelValue(getBrowser().disableExtension(extId));
      }));

  (*browserMod)["setWindowSize"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "browser.setWindowSize() requires (width, height)");
        int width = static_cast<int>(args[0].get<double>());
        int height = static_cast<int>(args[1].get<double>());
        return HavelValue(getBrowser().setWindowSize(-1, width, height));
      }));

  (*browserMod)["setWindowPosition"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "browser.setWindowPosition() requires (x, y)");
        int x = static_cast<int>(args[0].get<double>());
        int y = static_cast<int>(args[1].get<double>());
        return HavelValue(getBrowser().setWindowPosition(-1, x, y));
      }));

  (*browserMod)["maximizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().maximizeWindow(windowId));
      }));

  (*browserMod)["minimizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().minimizeWindow(windowId));
      }));

  (*browserMod)["fullscreenWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId =
            args.empty() ? -1 : static_cast<int>(args[0].get<double>());
        return HavelValue(getBrowser().fullscreenWindow(windowId));
      }));

  environment->Define("browser", HavelValue(browserMod));

  // === HTTP MODULE ===
  // HTTP client for REST API calls
  auto httpMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  (*httpMod)["get"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.get() requires URL");
        std::string url = this->ValueToString(args[0]);
        auto response = getHttp().get(url);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["post"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.post() requires URL");
        std::string url = this->ValueToString(args[0]);
        std::string data = args.size() > 1 ? this->ValueToString(args[1]) : "";
        auto response = getHttp().post(url, data);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["put"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.put() requires URL");
        std::string url = this->ValueToString(args[0]);
        std::string data = args.size() > 1 ? this->ValueToString(args[1]) : "";
        auto response = getHttp().put(url, data);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["delete"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.delete() requires URL");
        std::string url = this->ValueToString(args[0]);
        auto response = getHttp().del(url);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["patch"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.patch() requires URL");
        std::string url = this->ValueToString(args[0]);
        std::string data = args.size() > 1 ? this->ValueToString(args[1]) : "";
        auto response = getHttp().patch(url, data);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["download"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("http.download() requires (url, path)");
        std::string url = this->ValueToString(args[0]);
        std::string path = this->ValueToString(args[1]);
        return HavelValue(getHttp().download(url, path));
      }));

  (*httpMod)["upload"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("http.upload() requires (url, path)");
        std::string url = this->ValueToString(args[0]);
        std::string path = this->ValueToString(args[1]);
        auto response = getHttp().upload(url, path);

        auto obj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*obj)["statusCode"] =
            HavelValue(static_cast<double>(response.statusCode));
        (*obj)["body"] = HavelValue(response.body);
        (*obj)["ok"] = HavelValue(response.ok());
        if (!response.error.empty()) {
          (*obj)["error"] = HavelValue(response.error);
        }
        return HavelValue(obj);
      }));

  (*httpMod)["setTimeout"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("http.setTimeout() requires timeout in ms");
        int timeout = static_cast<int>(args[0].asNumber());
        getHttp().setTimeout(timeout);
        return HavelValue(true);
      }));

  environment->Define("http", HavelValue(httpMod));
}

void Interpreter::InitializeWindowBuiltins() {
  environment->Define(
      "window.getTitle",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->windowManager->GetActiveWindowTitle());
          }));

  environment->Define(
      "window.getPID",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(
                static_cast<double>(this->windowManager->GetActiveWindowPID()));
          }));

  environment->Define(
      "window.maximize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin = Window(this->windowManager->GetActiveWindow());
            activeWin.Max();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.minimize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin = Window(this->windowManager->GetActiveWindow());
            activeWin.Min();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.next",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager->AltTab();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.previous",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager->AltTab();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.close",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window w(this->windowManager->GetActiveWindow());
            w.Close();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.center",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager->Center(this->windowManager->GetActiveWindow());
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.focus",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("window.focus() requires window title");
        std::string title = ValueToString(args[0]);
        wID winId = WindowManager::FindByTitle(title.c_str());
        if (winId != 0) {
          Window window("", winId);
          window.Activate(winId);
          return HavelValue(true);
        }
        return HavelValue(false);
      }));

  // Additional window methods
  environment->Define(
      "window.move",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("window.move() requires (x, y)");
            int x = static_cast<int>(args[0].asNumber());
            int y = static_cast<int>(args[1].asNumber());
            Window activeWin(this->windowManager->GetActiveWindow());
            return HavelValue(activeWin.Move(x, y));
          }));

  environment->Define(
      "window.resize",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("window.resize() requires (width, height)");
        int width = static_cast<int>(args[0].asNumber());
        int height = static_cast<int>(args[1].asNumber());
        Window activeWin(this->windowManager->GetActiveWindow());
        return HavelValue(activeWin.Resize(width, height));
      }));

  environment->Define(
      "window.moveResize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 4)
              return HavelRuntimeError(
                  "window.moveResize() requires (x, y, width, height)");
            int x = static_cast<int>(args[0].asNumber());
            int y = static_cast<int>(args[1].asNumber());
            int width = static_cast<int>(args[2].asNumber());
            int height = static_cast<int>(args[3].asNumber());
            Window activeWin(this->windowManager->GetActiveWindow());
            return HavelValue(activeWin.MoveResize(x, y, width, height));
          }));

  // Note: Hide/Show not implemented yet in Window class
  // environment->Define("window.hide", BuiltinFunction([this](const
  // std::vector<HavelValue>& args) -> HavelResult {
  //     Window activeWin(this->windowManager->GetActiveWindow());
  //     activeWin.Hide();
  //     return HavelValue(nullptr);
  // }));
  //
  // environment->Define("window.show", BuiltinFunction([this](const
  // std::vector<HavelValue>& args) -> HavelResult {
  //     Window activeWin(this->windowManager->GetActiveWindow());
  //     activeWin.Show();
  //     return HavelValue(nullptr);
  // }));

  environment->Define(
      "window.alwaysOnTop",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            bool top = args.empty() ? true : args[0].asBool();
            Window activeWin(this->windowManager->GetActiveWindow());
            activeWin.AlwaysOnTop(top);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.transparency",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        int alpha = args.empty() ? 255 : static_cast<int>(args[0].asNumber());
        Window activeWin(this->windowManager->GetActiveWindow());
        activeWin.Transparency(alpha);
        return HavelValue(nullptr);
      }));

  environment->Define(
      "window.toggleFullscreen",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin(this->windowManager->GetActiveWindow());
            activeWin.ToggleFullscreen();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.snap",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("window.snap() requires position (0-3)");
            int position = static_cast<int>(args[0].asNumber());
            Window activeWin(this->windowManager->GetActiveWindow());
            activeWin.Snap(position);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.moveToMonitor",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "window.moveToMonitor() requires monitor index");
            int monitor = static_cast<int>(args[0].asNumber());
            Window activeWin(this->windowManager->GetActiveWindow());
            return HavelValue(activeWin.MoveToMonitor(monitor));
          }));

  environment->Define(
      "window.moveToCorner",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "window.moveToCorner() requires corner name");
            std::string corner = this->ValueToString(args[0]);
            Window activeWin(this->windowManager->GetActiveWindow());
            return HavelValue(activeWin.MoveToCorner(corner));
          }));

  environment->Define(
      "window.getClass",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->windowManager->GetActiveWindowClass());
          }));

  environment->Define(
      "window.exists",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty()) {
              Window activeWin(this->windowManager->GetActiveWindow());
              return HavelValue(activeWin.Exists());
            }
            std::string title = this->ValueToString(args[0]);
            wID winId = WindowManager::FindByTitle(title.c_str());
            return HavelValue(winId != 0);
          }));

  environment->Define(
      "window.isActive",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin(this->windowManager->GetActiveWindow());
            return HavelValue(activeWin.Active());
          }));

  // Expose as module object: window
  auto win = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  if (auto v = environment->Get("window.getTitle"))
    (*win)["getTitle"] = *v;
  if (auto v = environment->Get("window.maximize"))
    (*win)["maximize"] = *v;
  if (auto v = environment->Get("window.minimize"))
    (*win)["minimize"] = *v;
  if (auto v = environment->Get("window.next"))
    (*win)["next"] = *v;
  if (auto v = environment->Get("window.previous"))
    (*win)["previous"] = *v;
  if (auto v = environment->Get("window.close"))
    (*win)["close"] = *v;
  if (auto v = environment->Get("window.center"))
    (*win)["center"] = *v;
  if (auto v = environment->Get("window.focus"))
    (*win)["focus"] = *v;
  if (auto v = environment->Get("window.move"))
    (*win)["move"] = *v;
  if (auto v = environment->Get("window.resize"))
    (*win)["resize"] = *v;
  if (auto v = environment->Get("window.moveResize"))
    (*win)["moveResize"] = *v;
  // if (auto v = environment->Get("window.hide")) (*win)["hide"] = *v;
  // if (auto v = environment->Get("window.show")) (*win)["show"] = *v;
  if (auto v = environment->Get("window.alwaysOnTop"))
    (*win)["alwaysOnTop"] = *v;
  if (auto v = environment->Get("window.transparency"))
    (*win)["transparency"] = *v;
  if (auto v = environment->Get("window.toggleFullscreen"))
    (*win)["toggleFullscreen"] = *v;
  if (auto v = environment->Get("window.snap"))
    (*win)["snap"] = *v;
  if (auto v = environment->Get("window.moveToMonitor"))
    (*win)["moveToMonitor"] = *v;
  if (auto v = environment->Get("window.moveToCorner"))
    (*win)["moveToCorner"] = *v;
  if (auto v = environment->Get("window.getClass"))
    (*win)["getClass"] = *v;
  if (auto v = environment->Get("window.exists"))
    (*win)["exists"] = *v;
  if (auto v = environment->Get("window.isActive"))
    (*win)["isActive"] = *v;
  if (auto v = environment->Get("window.setTransparency"))
    (*win)["setTransparency"] = *v;

  // Monitor methods
  environment->Define(
      "window.getMonitors",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            auto monitors = havel::DisplayManager::GetMonitors();
            auto result = std::make_shared<std::vector<HavelValue>>();
            
            for (const auto &mon : monitors) {
              auto monObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
              (*monObj)["name"] = HavelValue(mon.name);
              (*monObj)["x"] = HavelValue(static_cast<double>(mon.x));
              (*monObj)["y"] = HavelValue(static_cast<double>(mon.y));
              (*monObj)["width"] = HavelValue(static_cast<double>(mon.width));
              (*monObj)["height"] = HavelValue(static_cast<double>(mon.height));
              (*monObj)["isPrimary"] = HavelValue(mon.isPrimary);
              result->push_back(HavelValue(monObj));
            }
            
            return HavelValue(result);
          }));

  environment->Define(
      "window.getMonitorArea",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            auto monitors = havel::DisplayManager::GetMonitors();
            
            if (monitors.empty()) {
              return HavelValue(nullptr);
            }
            
            // Calculate total area of all monitors
            int minX = monitors[0].x, minY = monitors[0].y;
            int maxX = monitors[0].x + monitors[0].width;
            int maxY = monitors[0].y + monitors[0].height;
            
            for (const auto &mon : monitors) {
              minX = std::min(minX, mon.x);
              minY = std::min(minY, mon.y);
              maxX = std::max(maxX, mon.x + mon.width);
              maxY = std::max(maxY, mon.y + mon.height);
            }
            
            auto areaObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*areaObj)["x"] = HavelValue(static_cast<double>(minX));
            (*areaObj)["y"] = HavelValue(static_cast<double>(minY));
            (*areaObj)["width"] = HavelValue(static_cast<double>(maxX - minX));
            (*areaObj)["height"] = HavelValue(static_cast<double>(maxY - minY));
            
            return HavelValue(areaObj);
          }));

  if (auto v = environment->Get("window.getMonitors"))
    (*win)["getMonitors"] = *v;
  if (auto v = environment->Get("window.getMonitorArea"))
    (*win)["getMonitorArea"] = *v;

  // Add property-style accessors (no parentheses needed)
  (*win)["title"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        Window activeWin(this->windowManager->GetActiveWindow());
        return HavelValue(activeWin.Title());
      });

  (*win)["active"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        Window activeWin(this->windowManager->GetActiveWindow());
        return HavelValue(activeWin.Active());
      });

  // window.getActiveWindow() - returns window object with properties
  environment->Define(
      "window.getActiveWindow",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            wID winId = this->windowManager->GetActiveWindow();
            if (winId == 0) {
              return HavelValue(nullptr);
            }
            Window win("", winId);
            Rect pos = win.Pos();
            auto winObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*winObj)["id"] = HavelValue(static_cast<double>(winId));
            (*winObj)["title"] = HavelValue(win.Title());
            (*winObj)["x"] = HavelValue(static_cast<double>(pos.x));
            (*winObj)["y"] = HavelValue(static_cast<double>(pos.y));
            (*winObj)["w"] = HavelValue(static_cast<double>(pos.w));
            (*winObj)["h"] = HavelValue(static_cast<double>(pos.h));
            (*winObj)["active"] = HavelValue(win.Active());
            return HavelValue(winObj);
          }));

  // window.pos() - returns {x, y} position of active window
  environment->Define(
      "window.pos",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            Window activeWin(this->windowManager->GetActiveWindow());
            Rect pos = activeWin.Pos();
            auto posObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*posObj)["x"] = HavelValue(static_cast<double>(pos.x));
            (*posObj)["y"] = HavelValue(static_cast<double>(pos.y));
            return HavelValue(posObj);
          }));

  // window.moveToNextMonitor() - move active window to next monitor
  environment->Define(
      "window.moveToNextMonitor",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            this->windowManager->MoveWindowToNextMonitor();
            return HavelValue(nullptr);
          }));

  // Add to window module object
  if (auto v = environment->Get("window.getActiveWindow"))
    (*win)["getActiveWindow"] = *v;
  if (auto v = environment->Get("window.pos"))
    (*win)["pos"] = *v;
  if (auto v = environment->Get("window.moveToNextMonitor"))
    (*win)["moveToNextMonitor"] = *v;

  environment->Define("window", HavelValue(win));
}

void Interpreter::InitializeIOBuiltins() {
  // IO map function - map one key to another
  environment->Define(
      "io.map",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("io.map() requires (from, to)");
            std::string from = ValueToString(args[0]);
            std::string to = ValueToString(args[1]);
            this->io->Map(from, to);
            return HavelValue(nullptr);
          }));

  // IO remap function - remap two keys
  environment->Define(
      "io.remap",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("io.remap() requires (key1, key2)");
            std::string key1 = ValueToString(args[0]);
            std::string key2 = ValueToString(args[1]);
            this->io->Remap(key1, key2);
            return HavelValue(nullptr);
          }));

  // IO block - disable all input
  environment->Define(
      "io.block",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (hotkeyManager) {
              // TODO: Add actual block method to HotkeyManager when
              // available
              std::cout << "[INFO] IO input blocked" << std::endl;
            } else {
              std::cout << "[WARN] HotkeyManager not available" << std::endl;
            }
            return HavelValue(nullptr);
          }));

  // IO suspend - suspend/resume all hotkeys (mirrors IO::Suspend()
  // toggle)
  environment->Define(
      "io.suspend",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(this->io->Suspend());
          }));

  // IO resume - only resumes if currently suspended
  environment->Define(
      "io.resume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (this->io->isSuspended) {
              return HavelValue(this->io->Suspend());
            }
            return HavelValue(true);
          }));

  // IO unblock - enable all input
  environment->Define(
      "io.unblock",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (hotkeyManager) {
              // TODO: Add actual unblock method to HotkeyManager when
              // available
              std::cout << "[INFO] IO input unblocked" << std::endl;
            } else {
              std::cout << "[WARN] HotkeyManager not available" << std::endl;
            }
            return HavelValue(nullptr);
          }));

  // IO grab - grab exclusive input
  environment->Define(
      "io.grab",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (hotkeyManager) {
              // TODO: Add actual grab method to HotkeyManager when
              // available
              std::cout << "[INFO] IO input grabbed" << std::endl;
            } else {
              std::cout << "[WARN] HotkeyManager not available" << std::endl;
            }
            return HavelValue(nullptr);
          }));

  // IO ungrab - release exclusive input
  environment->Define(
      "io.ungrab",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (hotkeyManager) {
              // TODO: Add actual ungrab method to HotkeyManager when
              // available
              std::cout << "[INFO] IO input ungrabbed" << std::endl;
            } else {
              std::cout << "[WARN] HotkeyManager not available" << std::endl;
            }
            return HavelValue(nullptr);
          }));

  // IO test keycode - print keycode for next pressed key
  environment->Define(
      "io.testKeycode",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        std::cout << "[INFO] Press any key to see its keycode... (Not yet "
                     "implemented)"
                  << std::endl;
        // TODO: Implement keycode testing mode
        return HavelValue(nullptr);
      }));
  auto mouseObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  //
  // mouse.move(dx, dy)
  //
  (*mouseObj)["move"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 2)
          return HavelRuntimeError("mouse.move(dx, dy) requires 2 arguments");

        int dx = static_cast<int>(ValueToNumber(args[0]));
        int dy = static_cast<int>(ValueToNumber(args[1]));

        if (!io->MouseMove(dx, dy))
          return HavelRuntimeError("MouseMove failed");

        return HavelValue(true);
      });

  //
  // mouse.moveTo(x, y, speed, accel)
  //
  (*mouseObj)["moveTo"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2 || args.size() > 4)
          return HavelRuntimeError("mouse.moveTo(x, y, [speed], [accel]) requires 2-4 arguments");

        int x = static_cast<int>(ValueToNumber(args[0]));
        int y = static_cast<int>(ValueToNumber(args[1]));
        int speed = 1;
        float accel = 1.0f;

        if (args.size() >= 3)
          speed = static_cast<int>(ValueToNumber(args[2]));
        if (args.size() >= 4)
          accel = static_cast<float>(ValueToNumber(args[3]));

        if (!io->MouseMoveTo(x, y, speed, accel))
          return HavelRuntimeError("MouseMoveTo failed");

        return HavelValue(true);
      });

  //
  // mouse.clickAt(x, y, button, speed, accel)
  //
  (*mouseObj)["clickAt"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2 || args.size() > 5)
          return HavelRuntimeError("mouse.clickAt(x, y, [button], [speed], [accel]) requires 2-5 arguments");

        int x = static_cast<int>(ValueToNumber(args[0]));
        int y = static_cast<int>(ValueToNumber(args[1]));
        int button = 1;
        int speed = 1;
        float accel = 1.0f;

        if (args.size() >= 3)
          button = static_cast<int>(ValueToNumber(args[2]));
        if (args.size() >= 4)
          speed = static_cast<int>(ValueToNumber(args[3]));
        if (args.size() >= 5)
          accel = static_cast<float>(ValueToNumber(args[4]));

        if (!io->ClickAt(x, y, button, speed, accel))
          return HavelRuntimeError("ClickAt failed");

        return HavelValue(true);
      });

  //
  // mouse.getPos()
  //
  (*mouseObj)["getPos"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 0)
          return HavelRuntimeError("mouse.getPos() requires no arguments");

        auto pos = io->GetMousePosition();
        auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*result)["x"] = HavelValue(static_cast<double>(pos.first));
        (*result)["y"] = HavelValue(static_cast<double>(pos.second));
        
        return HavelValue(result);
      });

  //
  // mouse.down(button)
  //
  (*mouseObj)["down"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int button = args.empty() ? io->GetMouseButtonCode("left") : static_cast<int>(ValueToNumber(args[0]));

        if (!io->Click(button, MouseAction::Hold))
          return HavelRuntimeError("MouseDown failed");

        return HavelValue(true);
      });

  //
  // mouse.up(button)
  //
  (*mouseObj)["up"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int button = args.empty() ? io->GetMouseButtonCode("left") : static_cast<int>(ValueToNumber(args[0]));

        if (!io->Click(button, MouseAction::Release))
          return HavelRuntimeError("MouseUp failed");

        return HavelValue(true);
      });

  //
  // mouse.click(button?, down?)
  // button default = 1
  // down:
  //   - true  -> press only
  //   - false -> release only
  //   - null  -> full click (default)
  //
  (*mouseObj)["click"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int button = io->GetMouseButtonCode("left");
        bool doDown = true;
        bool doUp = true;

        if (!args.empty()) {
          button = io->GetMouseButtonCode(ValueToString(args[0]));
        }

        if (args.size() >= 2) {
          MouseAction action = io->GetMouseAction(ValueToString(args[1]));
          if (action == MouseAction::Hold) {
            doUp = false; // press only
          } else if (action == MouseAction::Release) {
            doDown = false; // release only
          }
        }

        bool ok = true;

        if(doUp && doDown){
          ok = io->Click(button, MouseAction::Click);
        } else {
          if (doDown)
            ok &= io->Click(button, MouseAction::Hold);

          if (doUp)
            ok &= io->Click(button, MouseAction::Release);
        }
        if (!ok)
          return HavelRuntimeError("MouseClick failed");

        return HavelValue(true);
      });

  //
  // mouse.scroll(dy, dx?)
  //
  (*mouseObj)["scroll"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "mouse.scroll(dy, dx?) requires at least dy");

        double dy = ValueToNumber(args[0]);
        double dx = args.size() >= 2 ? ValueToNumber(args[1]) : 0.0;

        if (!io->Scroll(dy, dx))
          return HavelRuntimeError("Scroll failed");

        return HavelValue(true);
      });

  (*mouseObj)["getSensitivity"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(static_cast<double>(this->io->mouseSensitivity));
      });

  (*mouseObj)["setSensitivity"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io->setMouseSensitivity() requires value");
        this->io->mouseSensitivity = ValueToNumber(args[0]);
        return HavelValue(static_cast<double>(this->io->mouseSensitivity));
      });
  environment->Define("mouse", mouseObj);
  environment->Define("click", (*mouseObj)["click"]);
  environment->Define(
      "io->emergencyReleaseAllKeys",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            this->io->EmergencyReleaseAllKeys();
            return HavelValue(nullptr);
          }));

  // Hotkey management builtins
  environment->Define(
      "io.enableHotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.enableHotkey() requires hotkey name");
        std::string hotkey = ValueToString(args[0]);
        return HavelValue(this->io->EnableHotkey(hotkey));
      }));

  environment->Define(
      "io.disableHotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.disableHotkey() requires hotkey name");
        std::string hotkey = ValueToString(args[0]);
        return HavelValue(this->io->DisableHotkey(hotkey));
      }));

  environment->Define(
      "io.toggleHotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.toggleHotkey() requires hotkey name");
        std::string hotkey = ValueToString(args[0]);
        return HavelValue(this->io->ToggleHotkey(hotkey));
      }));

  environment->Define(
      "io.removeHotkey",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError(
                  "io.removeHotkey() requires hotkey name or ID");

            // Try to parse as number first (ID), then as string (name)
            const HavelValue &arg = args[0];

            // Check if it's a number by trying to get it as a number
            double numVal = ValueToNumber(arg);

            // If it holds an int or double, use as ID
            bool isNumber = arg.isNumber();

            if (isNumber) {
              int id = static_cast<int>(numVal);
              return HavelValue(this->io->RemoveHotkey(id));
            } else {
              std::string name = ValueToString(args[0]);
              return HavelValue(this->io->RemoveHotkey(name));
            }
          }));

  // Expose as module object: audioManager
  auto am = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("audio.getVolume"))
    (*am)["getVolume"] = *v;
  if (auto v = environment->Get("audio.setVolume"))
    (*am)["setVolume"] = *v;
  if (auto v = environment->Get("audio.increaseVolume"))
    (*am)["increaseVolume"] = *v;
  if (auto v = environment->Get("audio.decreaseVolume"))
    (*am)["decreaseVolume"] = *v;
  if (auto v = environment->Get("audio.toggleMute"))
    (*am)["toggleMute"] = *v;
  if (auto v = environment->Get("audio.setMute"))
    (*am)["setMute"] = *v;
  if (auto v = environment->Get("audio.isMuted"))
    (*am)["isMuted"] = *v;
  environment->Define("audioManager", HavelValue(am));

  // Add comprehensive help function
  environment->Define(
      "help",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            std::stringstream help;

            if (args.empty()) {
              // Show general help
              help << "\n=== Havel Language Help ===\n\n";
              help << "Navigation:\n";
              help << "  - help()           : Show this main help page\n";
              help << "  - help(\"syntax\")   : Show syntax reference\n";
              help << "  - help(\"keywords\"): Show all keywords and usage\n";
              help << "  - help(\"hotkeys\")  : Show hotkey functionality\n";
              help << "  - help(\"modules\")  : Show available modules\n";
              help << "  - help(\"process\")  : Show process management\n\n";
              help << "Conditional Hotkeys:\n";
              help << "  - Basic: hotkey => action\n";
              help << "  - Postfix: hotkey => action if condition\n";
              help << "  - Prefix: hotkey if condition => action\n";
              help << "  - Grouped: when condition { hotkey => action }\n\n";
              help << "For detailed documentation, see Havel.md\n";
            } else {
              std::string topic = ValueToString(args[0]);

              if (topic == "syntax" || topic == "SYNTAX") {
                help << "\n=== Syntax Reference ===\n\n";
                help << "Basic Hotkey: hotkey => action\n";
                help << "Pipeline: data | transform1 | transform2\n";
                help << "Blocks: { statement1; statement2; }\n";
                help << "Variables: let name = value\n";
                help << "Conditionals: if condition { block } else { block "
                        "}\n";
                help << "Functions: fn name(param) => { block }\n";
              } else if (topic == "keywords" || topic == "KEYWORDS") {
                help << "\n=== Keywords ===\n\n";
                help << "let    : Variable declaration (let x = 5)\n";
                help << "if     : Conditional (if x > 0 { ... })\n";
                help << "else   : Alternative (if x > 0 { ... } else { ... "
                        "})\n";
                help << "when   : Conditional block (when condition { ... "
                        "})\n";
                help << "fn     : Function definition (fn name() => { ... "
                        "})\n";
                help << "return : Function return (return value)\n";
                help << "import : Module import (import module from "
                        "\"file\")\n";
                help << "config : Config block (config { ... })\n";
                help << "devices: Device config block (devices { ... })\n";
                help << "modes  : Modes config block (modes { ... })\n";
              } else if (topic == "hotkeys" || topic == "HOTKEYS") {
                help << "\n=== Conditional Hotkeys ===\n\n";
                help << "Postfix: F1 => send(\"hello\") if mode == "
                        "\"gaming\"\n";
                help << "Prefix:  F1 if mode == \"gaming\" => "
                        "send(\"hello\")\n";
                help << "Grouped: when mode == \"gaming\" { F1 => "
                        "send(\"hi\"); F2 "
                        "=> send(\"bye\"); }\n";
                help << "Nested:  when condition1 { F1 if condition2 => "
                        "action }\n";
                help << "All conditions are evaluated dynamically at "
                        "runtime!\n";
              } else if (topic == "modules" || topic == "MODULES") {
                help << "\n=== Available Modules ===\n\n";
                help << "clipboard : Clipboard operations (get, set, clear)\n";
                help << "window    : Window management (focus, move, "
                        "resize)\n";
                help << "io        : Input/output operations (mouse, "
                        "keyboard)\n";
                help << "audio     : Audio control (volume, mute, apps)\n";
                help << "text      : Text processing (upper, lower, trim, "
                        "etc.)\n";
                help << "file      : File I/O operations\n";
                help << "system    : System operations (run, notify, sleep)\n";
                help << "process   : Process management (find, kill, nice, "
                        "ionice)\n";
                help << "launcher  : Process execution (run, runShell, "
                        "runDetached)\n";
              } else if (topic == "process" || topic == "PROCESS") {
                help << "\n=== Process Management Module ===\n\n";
                help << "Process Discovery:\n";
                help << "  process.find(name)           : Find processes by "
                        "name\n";
                help << "  process.exists(pid|name)     : Check if process "
                        "exists\n\n";
                help << "Process Control:\n";
                help << "  process.kill(pid, signal)    : Send signal to "
                        "process\n";
                help << "  process.nice(pid, value)     : Set CPU priority "
                        "(-20 to "
                        "19)\n";
                help << "  process.ionice(pid, class, data) : Set I/O "
                        "priority\n\n";
                help << "Examples:\n";
                help << "  let procs = process.find(\"firefox\")\n";
                help << "  process.kill(procs[0].pid, \"SIGTERM\")\n";
                help << "  process.nice(1234, 10)           // Lower CPU "
                        "priority\n";
                help << "  process.ionice(1234, 2, 4)      // Best-effort "
                        "I/O\n\n";
                help << "Process Object Fields:\n";
                help << "  pid, ppid, name, command, user\n";
                help << "  cpu_usage, memory_usage\n";
              } else {
                help << "\nUnknown topic: " << topic << "\n";
                help << "Use help() to see available topics.\n";
              }
            }

            std::cout << help.str();
            return HavelValue(nullptr);
          }));
}

void Interpreter::InitializeMathBuiltins() {
  // Delegate to stdlib module
  havel::stdlib::registerMathModule(environment.get());
}

// KeyTap constructor implementation
KeyTap *Interpreter::createKeyTap(
    const std::string &keyName, std::function<void()> onTap,
    std::variant<std::string, std::function<bool()>> tapCondition,
    std::variant<std::string, std::function<bool()>> comboCondition,
    std::function<void()> onCombo, bool grabDown, bool grabUp) {
  if (!io) {
    havel::error("createKeyTap: IO is not available");
    return nullptr;
  }
  if (!hotkeyManager) {
    havel::error("createKeyTap: HotkeyManager is not available");
    return nullptr;
  }
  
  havel::debug("createKeyTap: keyName='{}', grabDown={}, grabUp={}", keyName, grabDown, grabUp);
  
  auto keyTap =
      std::make_unique<KeyTap>(*io, *hotkeyManager, keyName, onTap, tapCondition,
                               comboCondition, onCombo, grabDown, grabUp);

  KeyTap *rawPtr = keyTap.get();

  keyTaps.push_back(std::move(keyTap));
  rawPtr->setup();
  
  havel::debug("createKeyTap: KeyTap created and setup complete for '{}'", keyName);

  return rawPtr;
}

void Interpreter::InitializeMediaBuiltins() {
  // Debug test
  environment->Define("media_builtins_called", HavelValue(true));

  // === MEDIA CONTROLS ===
  // Create media module object
  auto mediaObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Play/Pause toggle
  (*mediaObj)["play"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->PlayPause();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mediaObj)["pause"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->PlayPause(); // MPV uses toggle for pause
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mediaObj)["stop"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->Stop();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mediaObj)["next"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->Next();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mediaObj)["previous"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->Previous();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Define the media object
  environment->Define("media", HavelValue(mediaObj));

  // === MPVCONTROLLER MODULE ===
  // Create mpvcontroller module object
  auto mpvcontrollerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Volume controls
  (*mpvcontrollerObj)["volumeUp"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->VolumeUp();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mpvcontrollerObj)["volumeDown"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->VolumeDown();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mpvcontrollerObj)["toggleMute"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->ToggleMute();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Seek controls
  (*mpvcontrollerObj)["seekForward"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SeekForward();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mpvcontrollerObj)["seekBackward"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SeekBackward();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Speed controls
  (*mpvcontrollerObj)["speedUp"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SpeedUp();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  (*mpvcontrollerObj)["slowDown"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SlowDown();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Subtitle controls
  (*mpvcontrollerObj)["toggleSubtitleVisibility"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->ToggleSubtitleVisibility();
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Loop control
  (*mpvcontrollerObj)["setLoop"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "mpvcontroller.setLoop() requires boolean argument");
        bool enable = ValueToBool(args[0]);
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SetLoop(enable);
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Raw command sending
  (*mpvcontrollerObj)["sendRaw"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "mpvcontroller.sendRaw() requires string argument");
        std::string data = ValueToString(args[0]);
        if (auto app = HavelApp::instance) {
          if (app->mpv) {
            app->mpv->SendRaw(data);
            return HavelValue(true);
          }
        }
        return HavelRuntimeError("MPVController not available");
      });

  // Define the mpvcontroller object
  environment->Define("mpvcontroller", HavelValue(mpvcontrollerObj));
}

void Interpreter::InitializeFileManagerBuiltins() {
  // === FILEMANAGER MODULE ===
  // Create filemanager module object
  auto filemanagerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // File operations
  (*filemanagerObj)["read"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.read() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(file.read());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to read file: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["write"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "filemanager.write() requires file path and content arguments");
        std::string path = ValueToString(args[0]);
        std::string content = ValueToString(args[1]);
        try {
          FileManager file(path);
          file.write(content);
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to write file: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["append"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("filemanager.append() requires file "
                                   "path and content arguments");
        std::string path = ValueToString(args[0]);
        std::string content = ValueToString(args[1]);
        try {
          FileManager file(path);
          file.append(content);
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to append to file: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["exists"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.exists() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(file.exists());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to check file existence: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["delete"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.delete() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(file.deleteFile());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to delete file: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["copy"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "filemanager.copy() requires source and destination arguments");
        std::string source = ValueToString(args[0]);
        std::string dest = ValueToString(args[1]);
        try {
          FileManager file(source);
          return HavelValue(file.copy(dest));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to copy file: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["move"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "filemanager.move() requires source and destination arguments");
        std::string source = ValueToString(args[0]);
        std::string dest = ValueToString(args[1]);
        try {
          FileManager file(source);
          return HavelValue(file.move(dest));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to move file: " +
                                   std::string(e.what()));
        }
      });

  // File information
  (*filemanagerObj)["size"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.size() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(static_cast<double>(file.size()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get file size: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["wordCount"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.wordCount() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(static_cast<double>(file.wordCount()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to count words: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["lineCount"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.lineCount() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(static_cast<double>(file.lineCount()));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to count lines: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["getChecksum"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.getChecksum() requires file path argument");
        std::string path = ValueToString(args[0]);
        std::string algorithm =
            args.size() > 1 ? ValueToString(args[1]) : "SHA-256";
        try {
          FileManager file(path);
          return HavelValue(file.getChecksum(algorithm));
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to calculate checksum: " +
                                   std::string(e.what()));
        }
      });

  (*filemanagerObj)["getMimeType"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.getMimeType() requires file path argument");
        std::string path = ValueToString(args[0]);
        try {
          FileManager file(path);
          return HavelValue(file.getMimeType());
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to get MIME type: " +
                                   std::string(e.what()));
        }
      });

  // Create File constructor
  (*filemanagerObj)["File"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "filemanager.File() requires file path argument");
        std::string path = ValueToString(args[0]);

        // Create File object
        auto fileObj =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        // Store the file path
        (*fileObj)["path"] = HavelValue(path);

        // Add methods
        (*fileObj)["read"] = BuiltinFunction(
            [path](const std::vector<HavelValue> &args) -> HavelResult {
              try {
                FileManager file(path);
                return HavelValue(file.read());
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to read file: " +
                                         std::string(e.what()));
              }
            });

        (*fileObj)["write"] = BuiltinFunction(
            [path](const std::vector<HavelValue> &args) -> HavelResult {
              if (args.empty())
                return HavelRuntimeError(
                    "File.write() requires content argument");
              std::string content = ValueToString(args[0]);
              try {
                FileManager file(path);
                file.write(content);
                return HavelValue(true);
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to write file: " +
                                         std::string(e.what()));
              }
            });

        (*fileObj)["exists"] = BuiltinFunction(
            [path](const std::vector<HavelValue> &args) -> HavelResult {
              try {
                FileManager file(path);
                return HavelValue(file.exists());
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to check file existence: " +
                                         std::string(e.what()));
              }
            });

        (*fileObj)["size"] = BuiltinFunction(
            [path](const std::vector<HavelValue> &args) -> HavelResult {
              try {
                FileManager file(path);
                return HavelValue(static_cast<double>(file.size()));
              } catch (const std::exception &e) {
                return HavelRuntimeError("Failed to get file size: " +
                                         std::string(e.what()));
              }
            });

        return HavelValue(fileObj);
      });

  // Define the filemanager object
  environment->Define("filemanager", HavelValue(filemanagerObj));

  // === DETECTOR FUNCTIONS ===

  // Display detector
  environment->Define(
      "detectDisplay",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto monitors = DisplayManager::GetMonitors();
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        (*result)["count"] = HavelValue(static_cast<double>(monitors.size()));
        (*result)["type"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        auto monitorsArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto &monitor : monitors) {
          auto monitorObj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*monitorObj)["name"] = HavelValue(monitor.name);
          (*monitorObj)["x"] = HavelValue(static_cast<double>(monitor.x));
          (*monitorObj)["y"] = HavelValue(static_cast<double>(monitor.y));
          (*monitorObj)["width"] =
              HavelValue(static_cast<double>(monitor.width));
          (*monitorObj)["height"] =
              HavelValue(static_cast<double>(monitor.height));
          (*monitorObj)["isPrimary"] = HavelValue(monitor.isPrimary);
          monitorsArray->push_back(HavelValue(monitorObj));
        }
        (*result)["monitors"] = HavelValue(monitorsArray);

        return HavelValue(result);
      }));

  // Monitor config detector
  environment->Define(
      "detectMonitorConfig",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto monitors = DisplayManager::GetMonitors();
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        (*result)["totalMonitors"] =
            HavelValue(static_cast<double>(monitors.size()));

        int primaryCount = 0;
        int totalWidth = 0, totalHeight = 0;

        for (const auto &monitor : monitors) {
          if (monitor.isPrimary)
            primaryCount++;
          totalWidth += monitor.width;
          totalHeight += monitor.height;
        }

        (*result)["primaryMonitors"] =
            HavelValue(static_cast<double>(primaryCount));
        (*result)["totalWidth"] = HavelValue(static_cast<double>(totalWidth));
        (*result)["totalHeight"] = HavelValue(static_cast<double>(totalHeight));
        (*result)["sessionType"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        return HavelValue(result);
      }));

  // Window manager detector
  environment->Define(
      "detectWindowManager",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

        (*result)["name"] = HavelValue(WindowManagerDetector::GetWMName());
        (*result)["isWayland"] = HavelValue(WindowManagerDetector::IsWayland());
        (*result)["isX11"] = HavelValue(WindowManagerDetector::IsX11());
        (*result)["sessionType"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        return HavelValue(result);
      }));

  // System detector (OS, desktop environment, etc.)
  environment->Define(
      "detectSystem",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        auto result =
            std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Detect OS
#ifdef __linux__
        (*result)["os"] = HavelValue("Linux");
#elif _WIN32
        (*result)["os"] = HavelValue("Windows");
#elif __APPLE__
        (*result)["os"] = HavelValue("macOS");
#else
        (*result)["os"] = HavelValue("Unknown");
#endif

        (*result)["windowManager"] =
            HavelValue(WindowManagerDetector::GetWMName());
        (*result)["displayProtocol"] =
            HavelValue(WindowManagerDetector::IsWayland() ? "Wayland" : "X11");

        return HavelValue(result);
      }));
}

void Interpreter::InitializeLauncherBuiltins() {
  // Process launching
  environment->Define(
      "run",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("run() requires command");
            std::string command = ValueToString(args[0]);

            auto result = Launcher::runSync(command);

            // Create result object with all available information
            auto resultObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*resultObj)["success"] = HavelValue(result.success);
            (*resultObj)["exitCode"] =
                HavelValue(static_cast<double>(result.exitCode));
            (*resultObj)["pid"] = HavelValue(static_cast<double>(result.pid));
            (*resultObj)["stdout"] = HavelValue(result.stdout);
            (*resultObj)["stderr"] = HavelValue(result.stderr);
            (*resultObj)["error"] = HavelValue(result.error);
            (*resultObj)["executionTimeMs"] =
                HavelValue(static_cast<double>(result.executionTimeMs));

            return HavelValue(resultObj);
          }));

  environment->Define(
      "runAsync",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("runAsync() requires command");
            std::string command = ValueToString(args[0]);

            auto result = Launcher::runAsync(command);
            return HavelValue(static_cast<double>(result.pid));
          }));

  environment->Define(
      "runDetached",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("runDetached() requires command");
            std::string command = ValueToString(args[0]);

            auto result = Launcher::runDetached(command);
            return HavelValue(result.success);
          }));

  environment->Define(
      "terminal",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("terminal() requires command");
            std::string command = ValueToString(args[0]);

            auto result = Launcher::terminal(command);
            return HavelValue(result.success);
          }));

  // Expose as module object: gui
  auto guiObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("gui.menu"))
    (*guiObj)["menu"] = *v;
  if (auto v = environment->Get("gui.input"))
    (*guiObj)["input"] = *v;
  if (auto v = environment->Get("gui.confirm"))
    (*guiObj)["confirm"] = *v;
  if (auto v = environment->Get("gui.notify"))
    (*guiObj)["notify"] = *v;
  if (auto v = environment->Get("gui.fileDialog"))
    (*guiObj)["fileDialog"] = *v;
  if (auto v = environment->Get("gui.directoryDialog"))
    (*guiObj)["directoryDialog"] = *v;
  environment->Define("gui", HavelValue(guiObj));
}

void Interpreter::InitializeGUIBuiltins() {
  // Menu
  environment->Define(
      "gui.showMenu",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!guiManager)
          return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2)
          return HavelRuntimeError("gui.showMenu() requires (title, options)");

        std::string title = this->ValueToString(args[0]);

        if (!args[1].is<HavelArray>()) {
          return HavelRuntimeError(
              "gui.showMenu() requires an array of options");
        }
        auto optionsVec = args[1].get<HavelArray>();
        std::vector<std::string> options;
        if (optionsVec) {
          for (const auto &opt : *optionsVec) {
            options.push_back(this->ValueToString(opt));
          }
        }

        bool multiSelect = args.size() > 2 ? ValueToBool(args[2]) : false;
        std::string selected =
            guiManager->showMenu(title, options, multiSelect);
        return HavelValue(selected);
      }));

  // Input dialog
  environment->Define(
      "gui.input",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!guiManager)
              return HavelRuntimeError("GUIManager not available");
            if (args.empty())
              return HavelRuntimeError("gui.input() requires title");

            std::string title = ValueToString(args[0]);
            std::string prompt = args.size() > 1 ? ValueToString(args[1]) : "";
            std::string defaultValue =
                args.size() > 2 ? ValueToString(args[2]) : "";

            std::string input =
                guiManager->showInputDialog(title, prompt, defaultValue);
            return HavelValue(input);
          }));

  // Confirm dialog
  environment->Define(
      "gui.confirm",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!guiManager)
          return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2)
          return HavelRuntimeError("gui.confirm() requires (title, message)");

        std::string title = ValueToString(args[0]);
        std::string message = ValueToString(args[1]);

        bool confirmed = guiManager->showConfirmDialog(title, message);
        return HavelValue(confirmed);
      }));

  // Notification
  environment->Define(
      "gui.notify",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!guiManager)
          return HavelRuntimeError("GUIManager not available");
        if (args.size() < 2)
          return HavelRuntimeError("gui.notify() requires (title, message)");

        std::string title = ValueToString(args[0]);
        std::string message = ValueToString(args[1]);
        std::string icon = args.size() > 2 ? ValueToString(args[2]) : "info";

        guiManager->showNotification(title, message, icon);
        return HavelValue(nullptr);
      }));

  // Window transparency
  environment->Define(
      "window.setTransparency",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!guiManager)
              return HavelRuntimeError("GUIManager not available");
            if (args.empty())
              return HavelRuntimeError(
                  "window.setTransparency() requires opacity (0.0-1.0)");

            double opacity = ValueToNumber(args[0]);
            bool success = guiManager->setActiveWindowTransparency(opacity);
            return HavelValue(success);
          }));

  // File dialog
  environment->Define(
      "gui.fileDialog",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!guiManager)
          return HavelRuntimeError("GUIManager not available");

        std::string title =
            args.size() > 0 ? ValueToString(args[0]) : "Select File";
        std::string startDir = args.size() > 1 ? ValueToString(args[1]) : "";
        std::string filter = args.size() > 2 ? ValueToString(args[2]) : "";

        std::string selected =
            guiManager->showFileDialog(title, startDir, filter, false);
        return HavelValue(selected);
      }));

  // Directory dialog
  environment->Define(
      "gui.directoryDialog",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!guiManager)
          return HavelRuntimeError("GUIManager not available");

        std::string title =
            args.size() > 0 ? ValueToString(args[0]) : "Select Directory";
        std::string startDir = args.size() > 1 ? ValueToString(args[1]) : "";

        std::string selected = guiManager->showDirectoryDialog(title, startDir);
        return HavelValue(selected);
      }));

  // Expose as module object
  auto gui = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("gui.showMenu"))
    (*gui)["showMenu"] = *v;
  if (auto v = environment->Get("gui.input"))
    (*gui)["input"] = *v;
  if (auto v = environment->Get("gui.confirm"))
    (*gui)["confirm"] = *v;
  if (auto v = environment->Get("gui.notify"))
    (*gui)["notify"] = *v;
  if (auto v = environment->Get("gui.fileDialog"))
    (*gui)["fileDialog"] = *v;
  if (auto v = environment->Get("gui.directoryDialog"))
    (*gui)["directoryDialog"] = *v;

  environment->Define("gui", HavelValue(gui));
  // Note: window module is already defined in InitializeWindowBuiltins()
  // Don't overwrite it here as it would remove all other window functions

  // === ALTTAB MODULE ===
  // AltTab window switcher
  static std::unique_ptr<AltTabWindow> altTabWindow;

  environment->Define(
      "altTab.show",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (!altTabWindow) {
          altTabWindow = std::make_unique<AltTabWindow>();
        }
        altTabWindow->showAltTab();
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.hide",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (altTabWindow) {
          altTabWindow->hideAltTab();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.next",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (altTabWindow) {
          altTabWindow->nextWindow();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.prev",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (altTabWindow) {
          altTabWindow->prevWindow();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.select",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (altTabWindow) {
          altTabWindow->selectCurrentWindow();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.refresh",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (altTabWindow) {
          altTabWindow->refreshWindows();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "altTab.setThumbnailSize",
      BuiltinFunction([&](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "altTab.setThumbnailSize() requires (width, height)");
        int width = static_cast<int>(ValueToNumber(args[0]));
        int height = static_cast<int>(ValueToNumber(args[1]));
        if (altTabWindow) {
          altTabWindow->setThumbnailSize(width, height);
        }
        return HavelValue(nullptr);
      }));

  // Note: getWindows() is private in AltTabWindow

  // Create altTab module object
  auto altTabMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("altTab.show"))
    (*altTabMod)["show"] = *v;
  if (auto v = environment->Get("altTab.hide"))
    (*altTabMod)["hide"] = *v;
  if (auto v = environment->Get("altTab.next"))
    (*altTabMod)["next"] = *v;
  if (auto v = environment->Get("altTab.prev"))
    (*altTabMod)["prev"] = *v;
  if (auto v = environment->Get("altTab.select"))
    (*altTabMod)["select"] = *v;
  if (auto v = environment->Get("altTab.refresh"))
    (*altTabMod)["refresh"] = *v;
  if (auto v = environment->Get("altTab.setThumbnailSize"))
    (*altTabMod)["setThumbnailSize"] = *v;
  environment->Define("altTab", HavelValue(altTabMod));

  // === MAPMANAGER MODULE ===
  // MapManagerWindow for managing input mappings
  static std::unique_ptr<MapManagerWindow> mapManagerWindow;

  environment->Define(
      "mapmanager.show",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!mapManagerWindow) {
              // Note: MapManagerWindow requires MapManager instance
              // For now, create with null - full implementation needs
              // MapManager integration
              mapManagerWindow =
                  std::make_unique<MapManagerWindow>(nullptr, nullptr);
            }
            mapManagerWindow->show();
            mapManagerWindow->raise();
            mapManagerWindow->activateWindow();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "mapmanager.hide",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (mapManagerWindow) {
          mapManagerWindow->hide();
        }
        return HavelValue(nullptr);
      }));

  // Note: MapManagerWindow methods (refresh, saveAll, loadAll, etc.) are
  // private Qt slots

  // Create mapmanager module object
  auto mapManagerMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("mapmanager.show"))
    (*mapManagerMod)["show"] = *v;
  if (auto v = environment->Get("mapmanager.hide"))
    (*mapManagerMod)["hide"] = *v;
  environment->Define("mapmanager", HavelValue(mapManagerMod));

  // === MAPMANAGER CORE MODULE ===
  // MapManager for programmatic profile/mapping management
  static std::unique_ptr<MapManager> coreMapManager;

  environment->Define(
      "mapmanager.init",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (!coreMapManager && io) {
              coreMapManager = std::make_unique<MapManager>(io);
            }
            return HavelValue(coreMapManager != nullptr);
          }));

  environment->Define(
      "mapmanager.addProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError(
              "MapManager not initialized. Call mapmanager.init() first");
        if (args.size() < 2)
          return HavelRuntimeError(
              "mapmanager.addProfile() requires (id, name)");

        std::string id = ValueToString(args[0]);
        std::string name = ValueToString(args[1]);
        std::string desc = args.size() > 2 ? ValueToString(args[2]) : "";

        Profile profile;
        profile.id = id;
        profile.name = name;
        profile.description = desc;

        coreMapManager->AddProfile(profile);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.removeProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.removeProfile() requires profileId");

        std::string id = ValueToString(args[0]);
        coreMapManager->RemoveProfile(id);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.setActiveProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.setActiveProfile() requires profileId");

        std::string id = ValueToString(args[0]);
        coreMapManager->SetActiveProfile(id);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.getActiveProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");

        return HavelValue(coreMapManager->GetActiveProfileId());
      }));

  environment->Define(
      "mapmanager.getProfileIds",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        auto arr = std::make_shared<std::vector<HavelValue>>();
        if (coreMapManager) {
          auto ids = coreMapManager->GetProfileIds();
          for (const auto &id : ids) {
            arr->push_back(HavelValue(id));
          }
        }
        return HavelValue(arr);
      }));

  environment->Define(
      "mapmanager.addMapping",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.size() < 3)
          return HavelRuntimeError("mapmanager.addMapping() requires "
                                   "(profileId, sourceKey, targetKey)");

        std::string profileId = ValueToString(args[0]);
        std::string sourceKey = ValueToString(args[1]);
        std::string targetKey = ValueToString(args[2]);

        Mapping mapping;
        mapping.id = sourceKey + "_to_" + targetKey;
        mapping.name = sourceKey + " -> " + targetKey;
        mapping.sourceKey = sourceKey;
        mapping.targetKeys.push_back(targetKey);
        mapping.type = MappingType::KeyToKey;
        mapping.actionType = ActionType::Press;

        coreMapManager->AddMapping(profileId, mapping);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.removeMapping",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.size() < 2)
          return HavelRuntimeError(
              "mapmanager.removeMapping() requires (profileId, mappingId)");

        std::string profileId = ValueToString(args[0]);
        std::string mappingId = ValueToString(args[1]);
        coreMapManager->RemoveMapping(profileId, mappingId);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.enableProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.size() < 2)
          return HavelRuntimeError(
              "mapmanager.enableProfile() requires (profileId, enable)");

        std::string profileId = ValueToString(args[0]);
        bool enable = ValueToBool(args[1]);
        coreMapManager->EnableProfile(profileId, enable);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.nextProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (coreMapManager) {
          coreMapManager->NextProfile();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "mapmanager.previousProfile",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (coreMapManager) {
          coreMapManager->PreviousProfile();
        }
        return HavelValue(nullptr);
      }));

  environment->Define(
      "mapmanager.saveProfiles",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.saveProfiles() requires filepath");

        std::string path = ValueToString(args[0]);
        coreMapManager->SaveProfiles(path);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.loadProfiles",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.loadProfiles() requires filepath");

        std::string path = ValueToString(args[0]);
        coreMapManager->LoadProfiles(path);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.clearAllMappings",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (coreMapManager) {
          coreMapManager->ClearAllMappings();
        }
        return HavelValue(nullptr);
      }));

  // Add mapmanager core functions to module
  if (auto v = environment->Get("mapmanager.init"))
    (*mapManagerMod)["init"] = *v;
  if (auto v = environment->Get("mapmanager.addProfile"))
    (*mapManagerMod)["addProfile"] = *v;
  if (auto v = environment->Get("mapmanager.removeProfile"))
    (*mapManagerMod)["removeProfile"] = *v;
  if (auto v = environment->Get("mapmanager.setActiveProfile"))
    (*mapManagerMod)["setActiveProfile"] = *v;
  if (auto v = environment->Get("mapmanager.getActiveProfile"))
    (*mapManagerMod)["getActiveProfile"] = *v;
  if (auto v = environment->Get("mapmanager.getProfileIds"))
    (*mapManagerMod)["getProfileIds"] = *v;
  if (auto v = environment->Get("mapmanager.addMapping"))
    (*mapManagerMod)["addMapping"] = *v;
  if (auto v = environment->Get("mapmanager.removeMapping"))
    (*mapManagerMod)["removeMapping"] = *v;
  if (auto v = environment->Get("mapmanager.enableProfile"))
    (*mapManagerMod)["enableProfile"] = *v;
  if (auto v = environment->Get("mapmanager.nextProfile"))
    (*mapManagerMod)["nextProfile"] = *v;
  if (auto v = environment->Get("mapmanager.previousProfile"))
    (*mapManagerMod)["previousProfile"] = *v;
  if (auto v = environment->Get("mapmanager.saveProfiles"))
    (*mapManagerMod)["saveProfiles"] = *v;
  if (auto v = environment->Get("mapmanager.loadProfiles"))
    (*mapManagerMod)["loadProfiles"] = *v;
  if (auto v = environment->Get("mapmanager.clearAllMappings"))
    (*mapManagerMod)["clearAllMappings"] = *v;

  // === Additional MapManager Methods ===

  environment->Define(
      "mapmanager.getMapping",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!coreMapManager)
              return HavelRuntimeError("MapManager not initialized");
            if (args.size() < 2)
              return HavelRuntimeError(
                  "mapmanager.getMapping() requires (profileId, mappingId)");

            std::string profileId = ValueToString(args[0]);
            std::string mappingId = ValueToString(args[1]);

            auto *mapping = coreMapManager->GetMapping(profileId, mappingId);
            if (!mapping) {
              return HavelValue(nullptr);
            }

            auto obj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["id"] = HavelValue(mapping->id);
            (*obj)["name"] = HavelValue(mapping->name);
            (*obj)["enabled"] = HavelValue(mapping->enabled);
            (*obj)["sourceKey"] = HavelValue(mapping->sourceKey);
            // Convert targetKeys vector to HavelArray
            auto targetKeysArray = std::make_shared<std::vector<HavelValue>>();
            for (const auto& key : mapping->targetKeys) {
              targetKeysArray->push_back(HavelValue(key));
            }
            (*obj)["targetKeys"] = HavelValue(targetKeysArray);
            (*obj)["autofire"] = HavelValue(mapping->autofire);
            (*obj)["turbo"] = HavelValue(mapping->turbo);
            (*obj)["toggleMode"] = HavelValue(mapping->toggleMode);
            return HavelValue(obj);
          }));

  environment->Define(
      "mapmanager.getMappings",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.getMappings() requires profileId");

        std::string profileId = ValueToString(args[0]);
        auto mappings = coreMapManager->GetMappings(profileId);

        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto *mapping : mappings) {
          auto obj =
              std::make_shared<std::unordered_map<std::string, HavelValue>>();
          (*obj)["id"] = HavelValue(mapping->id);
          (*obj)["name"] = HavelValue(mapping->name);
          (*obj)["enabled"] = HavelValue(mapping->enabled);
          (*obj)["sourceKey"] = HavelValue(mapping->sourceKey);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  environment->Define(
      "mapmanager.updateMapping",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.size() < 2)
          return HavelRuntimeError(
              "mapmanager.updateMapping() requires (profileId, mappingData)");

        std::string profileId = ValueToString(args[0]);

        // mappingData should be an object with mapping properties
        if (!args[1].isObject()) {
          return HavelRuntimeError(
              "mapmanager.updateMapping() requires mapping data object");
        }

        auto data = args[1].asObject();
        if (!data)
          return HavelRuntimeError("Invalid mapping data");

        Mapping mapping;
        mapping.id = ValueToString((*data)["id"]);
        mapping.name = ValueToString((*data)["name"]);
        mapping.enabled = ValueToBool((*data)["enabled"]);
        mapping.sourceKey = ValueToString((*data)["sourceKey"]);
        mapping.autofire = ValueToBool((*data)["autofire"]);
        mapping.turbo = ValueToBool((*data)["turbo"]);
        mapping.toggleMode = ValueToBool((*data)["toggleMode"]);

        coreMapManager->UpdateMapping(profileId, mapping);
        return HavelValue(true);
      }));

  environment->Define(
      "mapmanager.setProfileSwitchHotkey",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!coreMapManager)
              return HavelRuntimeError("MapManager not initialized");
            if (args.empty())
              return HavelRuntimeError(
                  "mapmanager.setProfileSwitchHotkey() requires hotkey");

            std::string hotkey = ValueToString(args[0]);
            // Note: This requires IO integration for hotkey registration
            // For now, just store the hotkey string
            return HavelValue(nullptr);
          }));

  environment->Define(
      "mapmanager.resetStats",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (coreMapManager) {
              // Note: ResetStats is private, use clearAllMappings as workaround
              // Or expose via friend class
            }
            return HavelValue(nullptr);
          }));

  environment->Define(
      "mapmanager.exportProfile",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.exportProfile() requires profileId");

        std::string profileId = ValueToString(args[0]);
        try {
          std::string json = coreMapManager->ExportProfileToJson(profileId);
          return HavelValue(json);
        } catch (const std::exception &e) {
          return HavelRuntimeError(std::string("Export failed: ") + e.what());
        }
      }));

  environment->Define(
      "mapmanager.importProfile",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (!coreMapManager)
          return HavelRuntimeError("MapManager not initialized");
        if (args.empty())
          return HavelRuntimeError(
              "mapmanager.importProfile() requires JSON string");

        std::string json = ValueToString(args[0]);
        try {
          coreMapManager->ImportProfileFromJson(json);
          return HavelValue(true);
        } catch (const std::exception &e) {
          return HavelRuntimeError(std::string("Import failed: ") + e.what());
        }
      }));

  // Add additional methods to module
  if (auto v = environment->Get("mapmanager.getMapping"))
    (*mapManagerMod)["getMapping"] = *v;
  if (auto v = environment->Get("mapmanager.getMappings"))
    (*mapManagerMod)["getMappings"] = *v;
  if (auto v = environment->Get("mapmanager.updateMapping"))
    (*mapManagerMod)["updateMapping"] = *v;
  if (auto v = environment->Get("mapmanager.setProfileSwitchHotkey"))
    (*mapManagerMod)["setProfileSwitchHotkey"] = *v;
  if (auto v = environment->Get("mapmanager.exportProfile"))
    (*mapManagerMod)["exportProfile"] = *v;
  if (auto v = environment->Get("mapmanager.importProfile"))
    (*mapManagerMod)["importProfile"] = *v;
}

void Interpreter::InitializeAsyncBuiltins() {
  // spawn function
  environment->Define(
      "spawn", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.size() != 1) {
                       return HavelRuntimeError("spawn requires 1 argument");
                     }

                     if (!args[0].is<std::shared_ptr<HavelFunction>>()) {
                       return HavelRuntimeError("spawn requires a function");
                     }

                     auto func = args[0].get<std::shared_ptr<HavelFunction>>();
                     std::string taskId = "task_" + std::to_string(std::rand());

                     AsyncScheduler::getInstance().spawn(
                         [this, func]() -> HavelResult {
                           return Evaluate(*func->declaration->body);
                         },
                         taskId);

                     return HavelValue(taskId);
                   }));

  // await function
  environment->Define(
      "await",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() != 1) {
              return HavelRuntimeError("await requires 1 argument");
            }

            if (!args[0].is<std::string>()) {
              return HavelRuntimeError("await requires a task ID string");
            }

            std::string taskId = args[0].asString();
            return AsyncScheduler::getInstance().await(taskId);
          }));

  // channel function
  environment->Define(
      "channel",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() != 0) {
              return HavelRuntimeError("channel takes no arguments");
            }

            auto channel = std::make_shared<Channel>();
            return HavelValue(channel);
          }));

  // yield function
  environment->Define(
      "yield", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.size() != 0) {
                       return HavelRuntimeError("yield takes no arguments");
                     }

                     AsyncScheduler::getInstance().yield();
                     return HavelValue(nullptr);
                   }));
}

// Physics builtins
void Interpreter::InitializePhysicsBuiltins() {
  auto physics =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Speed of light (m/s)
  (*physics)["c"] = HavelValue(299792458.0);

  // Gravitational constant (N⋅m²/kg²)
  (*physics)["G"] = HavelValue(6.67430e-11);

  // Elementary charge (C)
  (*physics)["e"] = HavelValue(1.602176634e-19);

  // Electron mass (kg)
  (*physics)["me"] = HavelValue(9.10938356e-31);

  // Proton mass (kg)
  (*physics)["mp"] = HavelValue(1.67262192369e-27);

  // Planck constant (J⋅s)
  (*physics)["h"] = HavelValue(6.62607015e-34);

  // Avogadro constant (mol⁻¹)
  (*physics)["NA"] = HavelValue(6.02214076e23);

  // Boltzmann constant (J/K)
  (*physics)["k"] = HavelValue(1.380649e-23);

  // Permittivity of free space (F/m)
  (*physics)["epsilon0"] = HavelValue(8.854187817e-12);

  // Permeability of free space (H/m)
  (*physics)["mu0"] = HavelValue(1.25663706212e-6);

  // Fine structure constant
  (*physics)["alpha"] = HavelValue(7.2973525693e-3);

  // Rydberg constant (J)
  (*physics)["Rinf"] = HavelValue(10973731.56816);

  // Stefan-Boltzmann constant (W⋅m⁻²⋅K⁻⁴)
  (*physics)["sigma"] = HavelValue(5.670374419e-8);

  // Electron volt (J)
  (*physics)["eV"] = HavelValue(1.602176634e-19);

  // Atomic mass unit (kg)
  (*physics)["u"] = HavelValue(1.66053906660e-27);

  // Bohr radius (m)
  (*physics)["a0"] = HavelValue(5.29177210903e-11);

  // Classical electron radius (m)
  (*physics)["re"] = HavelValue(2.8179403227e-15);

  environment->Define("physics", HavelValue(physics));
}

void Interpreter::InitializeTimerBuiltins() {
  // === TIMER MODULE ===
  auto timerMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Thread-safe setTimeout function
  (*timerMod)["setTimeout"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("setTimeout requires callback and delay");
        }

        int delayMs = static_cast<int>(ValueToNumber(args[0]));

        // Validate callback
        if (!args[1].is<std::shared_ptr<HavelFunction>>()) {
          return HavelRuntimeError(
              "setTimeout second argument must be a function");
        }

        auto callback = args[1].get<std::shared_ptr<HavelFunction>>();

        // Thread-safe timer creation and management
        int timerId;
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          timerId = nextTimerId++;
        }

        // Create timer with exception safety
        try {
          auto timer = havel::SetTimeout(delayMs, [this, callback, timerId]() {
            // Execute callback with thread safety
            try {
              // Direct callback execution with empty args
              ast::CallExpression callExpr(std::make_unique<ast::Identifier>(
                  callback->declaration->name->symbol));
              auto result = Evaluate(callExpr);
              if (std::holds_alternative<HavelRuntimeError>(result)) {
                error("Timer {} callback failed: {}", timerId,
                      std::get<HavelRuntimeError>(result).what());
              }
            } catch (const std::exception &e) {
              error("Timer {} callback threw exception: {}", timerId, e.what());
            } catch (...) {
              error("Timer {} callback threw unknown exception", timerId);
            }

            // Thread-safe cleanup
            {
              std::lock_guard<std::mutex> lock(timersMutex);
              timers.erase(timerId);
            }
          });

          // Thread-safe timer storage
          {
            std::lock_guard<std::mutex> lock(timersMutex);
            timers[timerId] = timer;
          }

          return HavelValue(timerId);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to create timer: " +
                                   std::string(e.what()));
        }
      });

  // Thread-safe setInterval function
  (*timerMod)["setInterval"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "setInterval requires callback and interval");
        }

        int intervalMs = static_cast<int>(ValueToNumber(args[0]));

        // Validate callback
        if (!args[1].is<std::shared_ptr<HavelFunction>>()) {
          return HavelRuntimeError(
              "setInterval second argument must be a function");
        }

        auto callback = args[1].get<std::shared_ptr<HavelFunction>>();

        // Thread-safe timer creation and management
        int timerId;
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          timerId = nextTimerId++;
        }

        // Create interval timer with exception safety
        try {
          auto timer = havel::SetInterval(intervalMs, [this, callback,
                                                       timerId]() {
            // Execute callback with thread safety
            try {
              // Create a call expression to execute the function
              ast::CallExpression callExpr(std::make_unique<ast::Identifier>(
                  callback->declaration->name->symbol));
              auto result = Evaluate(callExpr);
              if (std::holds_alternative<HavelRuntimeError>(result)) {
                error("Interval {} callback failed: {}", timerId,
                      std::get<HavelRuntimeError>(result).what());
              }
            } catch (const std::exception &e) {
              error("Interval {} callback threw exception: {}", timerId,
                    e.what());
            } catch (...) {
              error("Interval {} callback threw unknown exception", timerId);
            }
            // Note: Don't erase interval timers on callback completion
          });

          // Thread-safe timer storage
          {
            std::lock_guard<std::mutex> lock(timersMutex);
            timers[timerId] = timer;
          }

          return HavelValue(timerId);
        } catch (const std::exception &e) {
          return HavelRuntimeError("Failed to create interval: " +
                                   std::string(e.what()));
        }
      });

  // Thread-safe clearTimeout function
  (*timerMod)["clearTimeout"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clearTimeout requires timer ID");
        }

        int timerId = static_cast<int>(ValueToNumber(args[0]));

        // Thread-safe timer cleanup
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          auto it = timers.find(timerId);
          if (it != timers.end()) {
            havel::StopTimer(it->second);
            timers.erase(it);
            return HavelValue(true);
          }
        }

        return HavelValue(false);
      });

  // Thread-safe clearInterval function
  (*timerMod)["clearInterval"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clearInterval requires timer ID");
        }

        int timerId = static_cast<int>(ValueToNumber(args[0]));

        // Thread-safe timer cleanup
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          auto it = timers.find(timerId);
          if (it != timers.end()) {
            havel::StopTimer(it->second);
            timers.erase(it);
            return HavelValue(true);
          }
        }

        return HavelValue(false);
      });

  // Thread-safe stopTimer function (unified)
  (*timerMod)["stopTimer"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("stopTimer requires timer ID");
        }

        int timerId = static_cast<int>(ValueToNumber(args[0]));

        // Thread-safe timer cleanup
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          auto it = timers.find(timerId);
          if (it != timers.end()) {
            havel::StopTimer(it->second);
            timers.erase(it);
            return HavelValue(true);
          }
        }

        return HavelValue(false);
      });

  // Thread-safe getTimerStatus function
  (*timerMod)["getTimerStatus"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("getTimerStatus requires timer ID");
        }

        int timerId = static_cast<int>(ValueToNumber(args[0]));

        // Thread-safe timer status check
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          auto it = timers.find(timerId);
          if (it != timers.end()) {
            auto statusObj =
                std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*statusObj)["id"] = HavelValue(timerId);
            (*statusObj)["running"] = HavelValue(it->second->load());
            return HavelValue(statusObj);
          }
        }

        return HavelValue(nullptr);
      });

  // Thread-safe cleanupAllTimers function
  (*timerMod)["cleanupAllTimers"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        // Thread-safe cleanup of all timers
        std::unordered_map<int, std::shared_ptr<std::atomic<bool>>>
            timersToStop;
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          timersToStop = timers;
          timers.clear();
        }

        // Stop all timers outside the lock to avoid deadlock
        for (const auto &[timerId, timer] : timersToStop) {
          havel::StopTimer(timer);
        }

        return HavelValue(static_cast<int>(timersToStop.size()));
      });

  // Thread-safe getActiveTimers function
  (*timerMod)["getActiveTimers"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        // Thread-safe get all active timers
        std::vector<int> activeTimerIds;
        {
          std::lock_guard<std::mutex> lock(timersMutex);
          activeTimerIds.reserve(timers.size());
          for (const auto &[timerId, timer] : timers) {
            if (timer->load()) {
              activeTimerIds.push_back(timerId);
            }
          }
        }

        // Convert to Havel array
        auto timerArray = std::make_shared<std::vector<HavelValue>>();
        for (int timerId : activeTimerIds) {
          timerArray->push_back(HavelValue(timerId));
        }

        return HavelValue(timerArray);
      });

  environment->Define("timer", HavelValue(timerMod));
}

void Interpreter::InitializeHelpBuiltin() {
  environment->Define(
      "help",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            std::stringstream help;

            if (args.empty()) {
              // Show general help
              help << "\n=== Havel Language Help ===\n\n";
              help << "Usage: help()          - Show this help\n";
              help << "       help(\"module\")  - Show help for specific "
                      "module\n\n";
              help << "Available modules:\n";
              help << "  - system      : System functions (print, sleep, "
                      "exit, "
                      "etc.)\n";
              help << "  - window      : Window management functions\n";
              help << "  - clipboard   : Clipboard operations\n";
              help << "  - text        : Text manipulation (upper, lower, "
                      "trim, "
                      "etc.)\n";
              help << "  - file        : File I/O operations\n";
              help << "  - array       : Array manipulation (map, filter, "
                      "reduce, "
                      "etc.)\n";
              help << "  - io          : Input/output control\n";
              help << "  - audio       : Audio control (volume, mute, "
                      "etc.)\n";
              help << "  - media       : Media playback control\n";
              help << "  - brightness  : Screen brightness control\n";
              help << "  - launcher    : Process launching (run, kill, "
                      "etc.)\n";
              help << "  - gui         : GUI dialogs and menus\n";
              help << "  - screenshot  : Screenshot capture with image data\n";
              help << "  - pixel       : Pixel-level screen operations\n";
              help << "  - image       : Image finding and matching\n";
              help << "  - ocr         : Optical character recognition\n";
              help << "  - timer       : Timer and scheduling functions\n";
              help << "  - automation  : Task automation\n";
              help << "  - physics     : Physics calculations\n";
              help << "  - process     : Process management\n";
              help << "  - http        : HTTP requests\n";
              help << "  - regex       : Regular expressions\n";
              help << "  - media       : Media playback\n";
              help << "  - mpvcontroller : MPV media player control\n";
              help << "  - filemanager : Advanced file operations\n";
              help << "  - altTab      : Window switcher\n";
              help << "  - mapmanager  : Key mapping management\n";
              help << "  - brightness  : Screen brightness control\n";
              help << "  - config      : Configuration access\n";
              help << "  - debug       : Debugging utilities\n";
              help << "  - approx      : Fuzzy comparison for floats\n\n";
              help << "Language features:\n";
              help << "  - const       : Immutable variable bindings\n";
              help << "  - trait/impl  : Interface-based polymorphism\n";
              help << "  - repeat      : Loop with count (supports variables)\n";
              help << "  - $ command   : Shell command execution\n";
              help << "  - `command`   : Shell command with output capture\n";
              help << "  - :duration   : Sleep statement (e.g., :100)\n";
              help << "  - struct methods : Methods on struct instances\n";
              help << "  - Type()      : Struct constructor sugar\n\n";
              help << "For detailed documentation, see Havel.md\n";
            } else {
              std::string module = ValueToString(args[0]);

              if (module == "system") {
                help << "\n=== System Module ===\n\n";
                help << "Constants:\n";
                help << "  true, false, null\n\n";
                help << "Functions:\n";
                help << "  print(...args)         - Print values to "
                        "stdout\n";
                help << "  println(...args)       - Print values with "
                        "newline\n";
                help << "  sleep(ms)              - Sleep for "
                        "milliseconds\n";
                help << "  exit([code])           - Exit program with "
                        "optional "
                        "code\n";
                help << "  type(value)            - Get type of value\n";
                help << "  len(array|string)      - Get length\n";
                help << "  range(start, end)      - Create array of "
                        "numbers\n";
                help << "  random([min, max])     - Generate random "
                        "number\n";
              } else if (module == "window") {
                help << "\n=== Window Module ===\n\n";
                help << "Functions:\n";
                help << "  window.getTitle()              - Get active "
                        "window "
                        "title\n";
                help << "  window.maximize()              - Maximize active "
                        "window\n";
                help << "  window.minimize()              - Minimize active "
                        "window\n";
                help << "  window.close()                 - Close active "
                        "window\n";
                help << "  window.center()                - Center active "
                        "window\n";
                help << "  window.focus()                 - Focus active "
                        "window\n";
                help << "  window.next()                  - Switch to next "
                        "window\n";
                help << "  window.previous()              - Switch to "
                        "previous "
                        "window\n";
                help << "  window.move(x, y)              - Move window to "
                        "position\n";
                help << "  window.resize(w, h)            - Resize window\n";
                help << "  window.moveResize(x,y,w,h)     - Move and "
                        "resize\n";
                help << "  window.alwaysOnTop(enable)     - Set always on "
                        "top\n";
                help << "  window.transparency(level)     - Set "
                        "transparency "
                        "(0-1)\n";
                help << "  window.toggleFullscreen()      - Toggle "
                        "fullscreen\n";
                help << "  window.snap(direction)         - Snap to screen "
                        "edge\n";
                help << "  window.moveToMonitor(index)    - Move to "
                        "monitor\n";
                help << "  window.moveToCorner(corner)    - Move to "
                        "corner\n";
                help << "  window.getClass()              - Get window "
                        "class\n";
                help << "  window.exists()                - Check if window "
                        "exists\n";
                help << "  window.isActive()              - Check if "
                        "window is "
                        "active\n";
              } else if (module == "clipboard") {
                help << "\n=== Clipboard Module ===\n\n";
                help << "Functions:\n";
                help << "  clipboard.get()        - Get clipboard text\n";
                help << "  clipboard.set(text)    - Set clipboard text\n";
                help << "  clipboard.clear()      - Clear clipboard\n";
              } else if (module == "text") {
                help << "\n=== Text Module ===\n\n";
                help << "Functions:\n";
                help << "  upper(text)            - Convert to uppercase\n";
                help << "  lower(text)            - Convert to lowercase\n";
                help << "  trim(text)             - Remove leading/trailing "
                        "whitespace\n";
                help << "  split(text, delimiter) - Split text into array\n";
                help << "  join(array, separator) - Join array into text\n";
                help << "  replace(text, old, new)- Replace text\n";
                help << "  contains(text, search) - Check if text contains "
                        "substring\n";
                help << "  startsWith(text, prefix) - Check if starts "
                        "with\n";
                help << "  endsWith(text, suffix)   - Check if ends with\n";
              } else if (module == "file") {
                help << "\n=== File Module ===\n\n";
                help << "Functions:\n";
                help << "  file.read(path)        - Read file contents\n";
                help << "  file.write(path, data) - Write to file\n";
                help << "  file.exists(path)      - Check if file exists\n";
              } else if (module == "array") {
                help << "\n=== Array Module ===\n\n";
                help << "Functions:\n";
                help << "  map(array, fn)         - Transform array "
                        "elements\n";
                help << "  filter(array, fn)      - Filter array elements\n";
                help << "  reduce(array, fn, init)- Reduce array to single "
                        "value\n";
                help << "  forEach(array, fn)     - Execute function for "
                        "each "
                        "element\n";
                help << "  push(array, value)     - Add element to end\n";
                help << "  pop(array)             - Remove and return last "
                        "element\n";
                help << "  shift(array)           - Remove and return first "
                        "element\n";
                help << "  unshift(array, value)  - Add element to "
                        "beginning\n";
                help << "  reverse(array)         - Reverse array\n";
                help << "  sort(array, [fn])      - Sort array\n";
              } else if (module == "io") {
                help << "\n=== IO Module ===\n\n";
                help << "Functions:\n";
                help << "  io.block()             - Block all input\n";
                help << "  io.unblock()           - Unblock input\n";
                help << "  send(keys)             - Send keystrokes\n";
                help << "  click([button])        - Simulate mouse click\n";
                help << "  mouseMove(x, y)        - Move mouse to "
                        "position\n";
              } else if (module == "audio") {
                help << "\n=== Audio Module ===\n\n";
                help << "Functions:\n";
                help << "  audio.getVolume()      - Get system volume "
                        "(0-100)\n";
                help << "  audio.setVolume(level) - Set system volume\n";
                help << "  audio.mute()           - Mute audio\n";
                help << "  audio.unmute()         - Unmute audio\n";
                help << "  audio.toggleMute()     - Toggle mute state\n";
              } else if (module == "media") {
                help << "\n=== Media Module ===\n\n";
                help << "Functions:\n";
                help << "  media.play()           - Play media\n";
                help << "  media.pause()          - Pause media\n";
                help << "  media.stop()           - Stop media\n";
                help << "  media.next()           - Next track\n";
                help << "  media.previous()       - Previous track\n";
              } else if (module == "brightness") {
                help << "\n=== Brightness Module ===\n\n";
                help << "Functions:\n";
                help << "  brightnessManager.getBrightness()    - Get "
                        "brightness "
                        "(0-100)\n";
                help << "  brightnessManager.setBrightness(val) - Set "
                        "brightness\n";
              } else if (module == "launcher") {
                help << "\n=== Launcher Module ===\n\n";
                help << "Functions:\n";
                help << "  run(command)           - Run command and return "
                        "result "
                        "object {success, exitCode, stdout, stderr, pid, "
                        "error, "
                        "executionTimeMs\n";
                help << "  runAsync(command)      - Run command "
                        "asynchronously\n";
                help << "  runDetached(command)   - Run command detached "
                        "from "
                        "parent\n";
                help << "  terminal(command)      - Run command in "
                        "terminal\n";
                help << "  kill(pid)              - Kill process by PID\n";
                help << "  killByName(name)       - Kill process by name\n";
              } else if (module == "gui") {
                help << "\n=== GUI Module ===\n\n";
                help << "Functions:\n";
                help << "  gui.menu(items)        - Show menu dialog\n";
                help << "  gui.notify(title, msg) - Show notification\n";
                help << "  gui.confirm(msg)       - Show confirmation "
                        "dialog\n";
                help << "  gui.input(prompt)      - Show input dialog\n";
                help << "  gui.fileDialog([title, dir, filter]) - Show file "
                        "picker\n";
                help << "  gui.directoryDialog([title, dir])    - Show "
                        "directory "
                        "picker\n";
              } else if (module == "debug") {
                help << "\n=== Debug Module ===\n\n";
                help << "Variables:\n";
                help << "  debug                  - Debug flag "
                        "(boolean)\n\n";
                help << "Functions:\n";
                help << "  assert(condition, msg) - Assert condition\n";
                help << "  trace(msg)             - Print trace message\n";
              } else if (module == "mpvcontroller") {
                help << "\n=== MPVController Module ===\n\n";
                help << "Functions:\n";
                help << "  mpvcontroller.volumeUp()                    - "
                        "Increase "
                        "volume\n";
                help << "  mpvcontroller.volumeDown()                  - "
                        "Decrease "
                        "volume\n";
                help << "  mpvcontroller.toggleMute()                  - "
                        "Toggle "
                        "mute\n";
                help << "  mpvcontroller.seekForward()                 - "
                        "Seek "
                        "forward\n";
                help << "  mpvcontroller.seekBackward()                - "
                        "Seek "
                        "backward\n";
                help << "  mpvcontroller.speedUp()                     - "
                        "Increase "
                        "playback speed\n";
                help << "  mpvcontroller.slowDown()                    - "
                        "Decrease "
                        "playback speed\n";
                help << "  mpvcontroller.toggleSubtitleVisibility()   - "
                        "Toggle "
                        "subtitles\n";
                help << "  mpvcontroller.setLoop(enabled)              - "
                        "Set loop "
                        "mode\n";
                help << "  mpvcontroller.sendRaw(command)             - "
                        "Send raw "
                        "MPV command\n";
              } else if (module == "textchunker") {
                help << "\n=== TextChunker Module ===\n\n";
                help << "Functions:\n";
                help << "  textchunker.chunk(text, maxSize)           - "
                        "Split "
                        "text "
                        "into chunks\n";
                help << "  textchunker.merge(chunks)                   - "
                        "Merge "
                        "chunks back\n";
              } else if (module == "ocr") {
                help << "\n=== OCR Module ===\n\n";
                help << "Functions:\n";
                help << "  ocr.capture()                               - "
                        "Capture "
                        "screen and extract text\n";
                help << "  ocr.captureRegion(x, y, width, height)      - "
                        "Capture "
                        "region and extract text\n";
                help << "  ocr.extractText(imagePath)                  - "
                        "Extract "
                        "text from image file\n";
              } else if (module == "alttab") {
                help << "\n=== AltTab Module ===\n\n";
                help << "Functions:\n";
                help << "  alttab.show()                               - "
                        "Show "
                        "alt-tab window switcher\n";
                help << "  alttab.next()                               - "
                        "Switch "
                        "to "
                        "next window\n";
                help << "  alttab.previous()                           - "
                        "Switch "
                        "to "
                        "previous window\n";
                help << "  alttab.hide()                               - "
                        "Hide "
                        "alt-tab switcher\n";
              } else if (module == "clipboardmanager") {
                help << "\n=== ClipboardManager Module ===\n\n";
                help << "Functions:\n";
                help << "  clipboardmanager.copy(text)                 - "
                        "Copy "
                        "text "
                        "to clipboard\n";
                help << "  clipboardmanager.paste()                    - "
                        "Paste "
                        "from clipboard\n";
                help << "  clipboardmanager.clear()                    - "
                        "Clear "
                        "clipboard\n";
                help << "  clipboardmanager.history()                  - "
                        "Get "
                        "clipboard history\n";
              } else if (module == "mapmanager") {
                help << "\n=== MapManager Module ===\n\n";
                help << "Functions:\n";
                help << "  mapmanager.load(mapFile)                    - "
                        "Load key "
                        "mapping file\n";
                help << "  mapmanager.save(mapFile)                    - "
                        "Save "
                        "current mappings\n";
                help << "  mapmanager.clear()                          - "
                        "Clear "
                        "all "
                        "mappings\n";
                help << "  mapmanager.list()                           - "
                        "List all "
                        "mappings\n";
                help << "  mapmanager.add(key, action)                 - "
                        "Add key "
                        "mapping\n";
                help << "  mapmanager.remove(key)                      - "
                        "Remove "
                        "key mapping\n";
              } else if (module == "filemanager") {
                help << "\n=== FileManager Module ===\n\n";
                help << "Functions:\n";
                help << "  filemanager.read(path)                      - "
                        "Read "
                        "file "
                        "content\n";
                help << "  filemanager.write(path, content)             - "
                        "Write "
                        "content to file\n";
                help << "  filemanager.append(path, content)            - "
                        "Append "
                        "content to file\n";
                help << "  filemanager.exists(path)                     - "
                        "Check "
                        "if "
                        "file exists\n";
                help << "  filemanager.delete(path)                    - "
                        "Delete "
                        "file\n";
                help << "  filemanager.copy(source, dest)              - "
                        "Copy "
                        "file\n";
                help << "  filemanager.move(source, dest)              - "
                        "Move "
                        "file\n";
                help << "  filemanager.size(path)                      - "
                        "Get file "
                        "size\n";
                help << "  filemanager.wordCount(path)                 - "
                        "Count "
                        "words in file\n";
                help << "  filemanager.lineCount(path)                 - "
                        "Count "
                        "lines in file\n";
                help << "  filemanager.getChecksum(path, algorithm)    - "
                        "Get file "
                        "checksum\n";
                help << "  filemanager.getMimeType(path)               - "
                        "Get MIME "
                        "type\n";
                help << "  filemanager.File(path)                      - "
                        "Create "
                        "File object\n\n";
                help << "Detector Functions:\n";
                help << "  detectDisplay()                             - "
                        "Detect "
                        "display configuration\n";
                help << "  detectMonitorConfig()                       - "
                        "Detect "
                        "monitor configuration\n";
                help << "  detectWindowManager()                       - "
                        "Detect "
                        "window manager\n";
                help << "  detectSystem()                              - "
                        "Detect "
                        "system information\n";
              } else if (module == "screenshot") {
                help << "\n=== Screenshot Module ===\n\n";
                help << "Functions (all return {path, data, width, height}):\n";
                help << "  screenshot.full([path])           - Full screen screenshot\n";
                help << "  screenshot.region(x,y,w,h)        - Region screenshot\n";
                help << "  screenshot.monitor()              - Current monitor screenshot\n";
                help << "\nReturns object with:\n";
                help << "  - path: File path to saved PNG\n";
                help << "  - data: Base64-encoded image data\n";
                help << "  - width, height: Image dimensions\n";
              } else if (module == "pixel") {
                help << "\n=== Pixel Module ===\n\n";
                help << "Functions:\n";
                help << "  pixel.get()                  - Get pixel color at cursor\n";
                help << "  pixel.get(x, y)              - Get pixel color at position\n";
                help << "  pixel.match(color)           - Check cursor pixel matches color\n";
                help << "  pixel.match(x, y, color, tol)- Check if pixel matches color\n";
                help << "  pixel.waitFor(x, y, color, timeout) - Wait for pixel color\n";
                help << "  pixel.setCacheEnabled(enabled, cacheTime) - Enable screenshot cache\n";
              } else if (module == "timer") {
                help << "\n=== Timer Module ===\n\n";
                help << "Functions:\n";
                help << "  timer.start(id, interval, callback) - Start repeating timer\n";
                help << "  timer.stop(id)                      - Stop timer\n";
                help << "  timer.once(delay, callback)         - One-shot timer\n";
              } else if (module == "approx") {
                help << "\n=== Approx Module ===\n\n";
                help << "Functions:\n";
                help << "  approx(a, b, epsilon)  - Fuzzy float comparison (relative tolerance)\n";
                help << "\nExample:\n";
                help << "  approx(0.1 + 0.2, 0.3)  => true\n";
              } else if (module == "type") {
                help << "\n=== Type Conversion ===\n\n";
                help << "Functions:\n";
                help << "  int(x)     - Convert to integer (truncates)\n";
                help << "  num(x)     - Convert to double\n";
                help << "  str(x)     - Convert to string\n";
                help << "  list(...)  - Create list from arguments or iterable\n";
                help << "  tuple(...) - Create tuple (fixed-size list)\n";
                help << "  set_(...)  - Create set (unique elements)\n";
              } else if (module == "implements") {
                help << "\n=== Traits ===\n\n";
                help << "Syntax:\n";
                help << "  trait Name { fn method() }\n";
                help << "  impl Name for Type { fn method() { ... } }\n";
                help << "\nFunctions:\n";
                help << "  implements(obj, traitName) - Check if type implements trait\n";
              } else if (module == "repeat") {
                help << "\n=== Repeat Statement ===\n\n";
                help << "Syntax:\n";
                help << "  repeat count { body }     - Repeat count times\n";
                help << "  repeat count statement    - Inline form\n";
                help << "\nCount can be literal, variable, or expression:\n";
                help << "  repeat 5 { ... }          - Literal\n";
                help << "  repeat n { ... }          - Variable\n";
                help << "  repeat 2 + 3 { ... }      - Expression\n";
              } else if (module == "shell") {
                help << "\n=== Shell Commands ===\n\n";
                help << "Syntax:\n";
                help << "  $ command          - Execute shell command (fire-and-forget)\n";
                help << "  `command`          - Execute and capture output\n";
                help << "\nBacktick returns object:\n";
                help << "  - stdout: Command output\n";
                help << "  - stderr: Error output\n";
                help << "  - exitCode: Exit code\n";
                help << "  - success: Boolean success flag\n";
                help << "  - error: Error message if any\n";
              } else if (module == "sleep") {
                help << "\n=== Sleep Statement ===\n\n";
                help << "Syntax:\n";
                help << "  :duration          - Sleep for duration\n";
                help << "\nDuration formats:\n";
                help << "  :100               - Milliseconds\n";
                help << "  :1s                - Seconds\n";
                help << "  :1m30s             - Minutes and seconds\n";
                help << "  :0:0:30.500        - HH:MM:SS.mmm format\n";
              } else if (module == "struct") {
                help << "\n=== Structs ===\n\n";
                help << "Syntax:\n";
                help << "  struct Name {\n";
                help << "    field1\n";
                help << "    field2\n";
                help << "    fn init(args) { this.field = args }\n";
                help << "    fn method() { ... }\n";
                help << "  }\n";
                help << "\nConstruction:\n";
                help << "  let obj = Name.new(args)  - Constructor\n";
                help << "  let obj = Name(args)      - Sugar (same as above)\n";
                help << "\nMethod calls:\n";
                help << "  obj.method()              - Instance method\n";
                help << "  obj.field                 - Field access\n";
              } else if (module == "const") {
                help << "\n=== Const ===\n\n";
                help << "Syntax:\n";
                help << "  const name = value      - Immutable binding\n";
                help << "\nConst prevents reassignment:\n";
                help << "  const x = 10\n";
                help << "  x = 20    // Error!\n";
                help << "\nNote: Object properties can still be modified:\n";
                help << "  const obj = {a: 1}\n";
                help << "  obj.a = 2   // OK\n";
                help << "  obj = {}    // Error!\n";
              } else if (module == "image") {
                help << "\n=== Image Module ===\n\n";
                help << "Functions:\n";
                help << "  image.find(path, [tolerance])  - Find image on screen\n";
                help << "  image.findAll(path)            - Find all occurrences\n";
                help << "  image.wait(path, timeout)      - Wait for image to appear\n";
              } else if (module == "ocr") {
                help << "\n=== OCR Module ===\n\n";
                help << "Functions:\n";
                help << "  ocr.read(image, [lang])  - Extract text from image\n";
                help << "  ocr.readRegion(x,y,w,h)  - OCR on screen region\n";
              } else if (module == "automation") {
                help << "\n=== Automation Module ===\n\n";
                help << "Functions:\n";
                help << "  automation.start(type, params)  - Start automation task\n";
                help << "  automation.stop(type)           - Stop automation task\n";
                help << "  automation.toggle(type)         - Toggle automation\n";
              } else if (module == "physics") {
                help << "\n=== Physics Module ===\n\n";
                help << "Functions:\n";
                help << "  physics.distance(x1,y1,x2,y2)  - Distance between points\n";
                help << "  physics.angle(x1,y1,x2,y2)     - Angle between points\n";
                help << "  physics.lerp(a,b,t)            - Linear interpolation\n";
              } else if (module == "process") {
                help << "\n=== Process Module ===\n\n";
                help << "Functions:\n";
                help << "  process.list()           - List running processes\n";
                help << "  process.byName(name)     - Find process by name\n";
                help << "  process.byPid(pid)       - Get process by PID\n";
                help << "  process.kill(pid)        - Kill process\n";
              } else if (module == "http") {
                help << "\n=== HTTP Module ===\n\n";
                help << "Functions:\n";
                help << "  http.get(url)            - GET request\n";
                help << "  http.post(url, data)     - POST request\n";
                help << "  http.put(url, data)      - PUT request\n";
                help << "  http.delete(url)         - DELETE request\n";
              } else if (module == "regex") {
                help << "\n=== Regex Module ===\n\n";
                help << "Functions:\n";
                help << "  regex.match(text, pattern)     - Match pattern\n";
                help << "  regex.replace(text, pat, repl) - Replace matches\n";
                help << "  regex.split(text, pattern)     - Split by pattern\n";
              } else if (module == "altTab") {
                help << "\n=== AltTab Module ===\n\n";
                help << "Functions:\n";
                help << "  altTab.show()     - Show window switcher\n";
                help << "  altTab.next()     - Next window\n";
                help << "  altTab.previous() - Previous window\n";
                help << "  altTab.hide()     - Hide switcher\n";
              } else if (module == "mapmanager") {
                help << "\n=== MapManager Module ===\n\n";
                help << "Functions:\n";
                help << "  mapmanager.load(file)     - Load key mappings\n";
                help << "  mapmanager.save(file)     - Save mappings\n";
                help << "  mapmanager.add(key, act)  - Add mapping\n";
                help << "  mapmanager.remove(key)    - Remove mapping\n";
                help << "  mapmanager.list()         - List all mappings\n";
              } else if (module == "config") {
                help << "\n=== Config Module ===\n\n";
                help << "Access configuration values:\n";
                help << "  config.get(key)    - Get config value\n";
                help << "  config.set(k, v)   - Set config value\n";
              } else {
                help << "\nUnknown module: " << module << "\n";
                help << "Use help() to see available modules.\n";
              }
            }

            std::cout << help.str();
            return HavelValue(nullptr);
          }));
}

void Interpreter::visitConditionalHotkey(const ast::ConditionalHotkey &node) {
  // Extract the hotkey string for use with HotkeyManager
  std::string hotkeyStr;
  if (!node.binding->hotkeys.empty()) {
    if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
            node.binding->hotkeys[0].get())) {
      hotkeyStr = hotkeyLit->combination;
    }
  }

  if (hotkeyStr.empty()) {
    lastResult =
        HavelRuntimeError("Invalid hotkey in conditional hotkey binding");
    return;
  }

  if (hotkeyManager) {
    // Create a lambda that captures the condition expression and
    // re-evaluates it
    // Capture shared_from_this() to ensure Interpreter stays alive during execution
    auto self = shared_from_this();
    auto destroyedFlag = m_destroyed;
    auto conditionFunc = [self, destroyedFlag, condExpr = node.condition.get()]() -> bool {
      if (destroyedFlag->load()) return false;  // Interpreter destroyed, skip evaluation
      try {
        auto result = self->Evaluate(*condExpr);
        if (isError(result)) {
          // Log error but return false to prevent the hotkey from triggering
          return false;
        }
        return Interpreter::ValueToBool(unwrap(result));
      } catch (const std::exception& e) {
        // Catch any exceptions to prevent them escaping into Qt event loop
        std::cerr << "Conditional hotkey condition evaluation failed: " << e.what() << std::endl;
        return false;
      }
    };

    // Create the action callback
    auto actionFunc = [self, destroyedFlag, action = node.binding->action.get()]() {
      if (destroyedFlag->load()) return;  // Interpreter destroyed, skip action
      try {
        if (action) {
          // Lock interpreter mutex to protect environment and lastResult
          std::lock_guard<std::mutex> lock(self->interpreterMutex);
          auto result = self->Evaluate(*action);
          if (isError(result)) {
            std::cerr << "Conditional hotkey action evaluation failed: "
                      << std::get<HavelRuntimeError>(result).what() << std::endl;
          }
        }
      } catch (const std::exception& e) {
        // Catch any exceptions to prevent them escaping into Qt event loop
        std::cerr << "Conditional hotkey action threw exception: " << e.what() << std::endl;
      }
    };

    // Register as a contextual hotkey with the HotkeyManager using
    // function-based condition
    hotkeyManager->AddContextualHotkey(hotkeyStr, conditionFunc, actionFunc);
    lastResult = nullptr;
  } else {
    // Fallback: static evaluation if HotkeyManager is not available
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
      lastResult = conditionResult;
      return;
    }

    bool conditionMet = ValueToBool(unwrap(conditionResult));

    if (conditionMet) {
      // If condition is true, register the hotkey binding normally
      visitHotkeyBinding(*node.binding);
    } else {
      // If condition is false, we don't register the hotkey, and we
      // don't evaluate the action Only register the hotkey if the
      // condition is initially true
      lastResult = nullptr;
    }
  }
}

void Interpreter::visitWhenBlock(const ast::WhenBlock &node) {
  // For each statement in the when block, wrap it with the shared
  // condition
  auto self = shared_from_this();
  for (const auto &stmt : node.statements) {
    // Check if it's a hotkey binding
    if (auto *hotkeyBinding =
            dynamic_cast<const ast::HotkeyBinding *>(stmt.get())) {
      // Extract hotkey string
      std::string hotkeyStr;
      if (!hotkeyBinding->hotkeys.empty()) {
        if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                hotkeyBinding->hotkeys[0].get())) {
          hotkeyStr = hotkeyLit->combination;
        }
      }

      if (hotkeyStr.empty()) {
        lastResult = HavelRuntimeError("Invalid hotkey in when block");
        return;
      }

      if (hotkeyManager) {
        // Create a lambda that captures the shared condition
        // Capture shared_from_this() to ensure Interpreter stays alive during execution
        auto destroyedFlag = m_destroyed;
        auto conditionFunc = [self, destroyedFlag, condExpr = node.condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          auto result = self->Evaluate(*condExpr);
          if (isError(result)) {
            // Log error but return false to prevent the hotkey from
            // triggering
            return false;
          }
          return Interpreter::ValueToBool(unwrap(result));
        };

        // Create the action callback
        auto actionFunc = [self, destroyedFlag, action = hotkeyBinding->action.get()]() {
          if (destroyedFlag->load()) return;  // Interpreter destroyed
          if (action) {
            // Lock interpreter mutex to protect environment and lastResult
            std::lock_guard<std::mutex> lock(self->interpreterMutex);
            auto result = self->Evaluate(*action);
            if (isError(result)) {
              std::cerr << "When block hotkey action failed: "
                        << std::get<HavelRuntimeError>(result).what()
                        << std::endl;
            }
          }
        };

        // Register as contextual hotkey with shared condition
        hotkeyManager->AddContextualHotkey(hotkeyStr, conditionFunc,
                                           actionFunc);
      }
    } else if (auto *conditionalHotkey =
                   dynamic_cast<const ast::ConditionalHotkey *>(stmt.get())) {
      // Handle nested conditional hotkeys inside when blocks
      // Combine the outer condition with the inner condition
      if (hotkeyManager) {
        // Extract hotkey string from the nested binding
        std::string hotkeyStr;
        if (!conditionalHotkey->binding->hotkeys.empty()) {
          if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                  conditionalHotkey->binding->hotkeys[0].get())) {
            hotkeyStr = hotkeyLit->combination;
          }
        }

        if (hotkeyStr.empty()) {
          lastResult = HavelRuntimeError(
              "Invalid hotkey in conditional hotkey within when block");
          return;
        }

        // Create a combined condition that requires both outer and
        // inner conditions
        // Capture shared_from_this() to ensure Interpreter stays alive during execution
        auto destroyedFlag = m_destroyed;
        auto combinedConditionFunc =
            [self, destroyedFlag, outerCond = node.condition.get(),
             innerCond = conditionalHotkey->condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          // Evaluate outer condition
          auto outerResult = self->Evaluate(*outerCond);
          if (isError(outerResult) || !Interpreter::ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = self->Evaluate(*innerCond);
          if (isError(innerResult) || !Interpreter::ValueToBool(unwrap(innerResult))) {
            return false;
          }

          return true;
        };

        // Create the action callback from the inner binding
        auto actionFunc =
            [self, destroyedFlag, action = conditionalHotkey->binding->action.get()]() {
              if (destroyedFlag->load()) return;  // Interpreter destroyed
              if (action) {
                // Lock interpreter mutex to protect environment and lastResult
                std::lock_guard<std::mutex> lock(self->interpreterMutex);
                auto result = self->Evaluate(*action);
                if (isError(result)) {
                  std::cerr << "Nested conditional hotkey action failed: "
                            << std::get<HavelRuntimeError>(result).what()
                            << std::endl;
                }
              }
            };

        // Register with combined condition
        hotkeyManager->AddContextualHotkey(hotkeyStr, combinedConditionFunc,
                                           actionFunc);
      }
    } else if (auto *whenBlock =
                   dynamic_cast<const ast::WhenBlock *>(stmt.get())) {
      // Handle nested when blocks - inherit the condition by creating a
      // combined condition
      if (hotkeyManager) {
        // Create a combined condition that requires both the outer and
        // inner conditions
        auto destroyedFlag = m_destroyed;
        auto combinedConditionFunc =
            [self, destroyedFlag, outerCond = node.condition.get(),
             innerCond = whenBlock->condition.get()]() -> bool {
          if (destroyedFlag->load()) return false;  // Interpreter destroyed
          // Evaluate outer condition
          auto outerResult = self->Evaluate(*outerCond);
          if (isError(outerResult) || !Interpreter::ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = self->Evaluate(*innerCond);
          if (isError(innerResult) || !Interpreter::ValueToBool(unwrap(innerResult))) {
            return false;
          }

          return true;
        };

        // For nested when blocks, we need to recursively process their
        // statements with the combined condition. For simplicity, we'll
        // just evaluate inner when blocks with the combined condition,
        // but a more complete implementation would recursively process
        // each statement inside the nested when block.
        for (const auto &innerStmt : whenBlock->statements) {
          // Process each statement in the nested when block with the
          // combined condition
          if (auto *innerHotkeyBinding =
                  dynamic_cast<const ast::HotkeyBinding *>(innerStmt.get())) {
            // Extract hotkey string from the nested statement
            std::string innerHotkeyStr;
            if (!innerHotkeyBinding->hotkeys.empty()) {
              if (auto *hotkeyLit = dynamic_cast<const ast::HotkeyLiteral *>(
                      innerHotkeyBinding->hotkeys[0].get())) {
                innerHotkeyStr = hotkeyLit->combination;
              }
            }

            if (innerHotkeyStr.empty()) {
              continue; // Skip invalid hotkeys
            }

            // Use the same action
            auto innerActionFunc =
                [self, action = innerHotkeyBinding->action.get()]() {
                  if (action) {
                    // Lock interpreter mutex to protect environment and lastResult
                    std::lock_guard<std::mutex> lock(self->interpreterMutex);
                    auto result = self->Evaluate(*action);
                    if (isError(result)) {
                      std::cerr << "Nested when block hotkey action failed: "
                                << std::get<HavelRuntimeError>(result).what()
                                << std::endl;
                    }
                  }
                };

            // Register with the combined condition (outer && inner)
            hotkeyManager->AddContextualHotkey(
                innerHotkeyStr, combinedConditionFunc, innerActionFunc);
          }
        }
      }
    } else {
      // Non-hotkey statements in when block - evaluate once statically
      auto result = Evaluate(*stmt);
      if (isError(result)) {
        lastResult = result;
        return;
      }
    }
  }

  lastResult = nullptr;
}

// ============================================================================
// Script Auto-Reload Implementation
// ============================================================================

void Interpreter::enableReload() {
  reloadEnabled.store(true);
  if (scriptPath.empty()) {
    warn("Cannot enable auto-reload: no script path set");
    return;
  }
  startReloadWatcher();
  info("Auto-reload enabled for script: {}", scriptPath);
}

void Interpreter::disableReload() {
  reloadEnabled.store(false);
  stopReloadWatcher();
  info("Auto-reload disabled");
}

void Interpreter::toggleReload() {
  if (reloadEnabled.load()) {
    disableReload();
  } else {
    enableReload();
  }
}

void Interpreter::startReloadWatcher() {
  if (reloadWatcherRunning.load() || scriptPath.empty()) {
    return;
  }
  
  reloadWatcherRunning.store(true);
  reloadWatcherThread = std::thread([this]() {
    namespace fs = std::filesystem;
    
    // Get initial modification time
    try {
      if (fs::exists(scriptPath)) {
        lastModifiedTime = fs::last_write_time(scriptPath);
      }
    } catch (...) {
      // File doesn't exist yet or can't be accessed
    }
    
    while (reloadWatcherRunning.load() && reloadEnabled.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      
      try {
        if (fs::exists(scriptPath)) {
          auto currentTime = fs::last_write_time(scriptPath);
          if (currentTime > lastModifiedTime) {
            lastModifiedTime = currentTime;
            info("Script file changed, triggering reload...");
            triggerReload();
          }
        }
      } catch (...) {
        // File access error, continue watching
      }
    }
  });
}

void Interpreter::stopReloadWatcher() {
  reloadWatcherRunning.store(false);
  if (reloadWatcherThread.joinable()) {
    reloadWatcherThread.join();
  }
}

void Interpreter::triggerReload() {
  std::lock_guard<std::mutex> lock(reloadMutex);
  
  if (!reloadEnabled.load() || scriptPath.empty()) {
    return;
  }

  // Clear all hotkeys before reload
  if (hotkeyManager) {
    info("Clearing hotkeys before reload...");
    hotkeyManager->clearAllHotkeys();
  }

  // Execute on reload handler
  executeOnReload();

  // Re-execute the script
  try {
    std::ifstream file(scriptPath);
    if (file) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      std::string code = buffer.str();
      Execute(code);
      info("Script reloaded successfully");
    }
  } catch (const std::exception& e) {
    error("Failed to reload script: {}", e.what());
  }
}

void Interpreter::executeOnStart() {
  if (onStartHandler) {
    onStartHandler();
  }
}

void Interpreter::executeOnReload() {
  if (onReloadHandler) {
    onReloadHandler();
  }
}

bool Interpreter::hasRunOnce(const std::string& id) {
  auto it = runOnceExecuted.find(id);
  return it != runOnceExecuted.end() && it->second;
}

void Interpreter::markRunOnce(const std::string& id) {
  runOnceExecuted[id] = true;
}

void Interpreter::clearRunOnce(const std::string& id) {
  runOnceExecuted.erase(id);
}

// Debug control methods
void Interpreter::setStopOnError(bool stop) {
  stopOnError = stop;
  havel::info("Debug: stopOnError = {}", stop ? "true" : "false");
}

void Interpreter::setShowAST(bool show) {
  showASTOnParse = show;
  havel::info("Debug: showASTOnParse = {}", show ? "true" : "false");
}

std::string Interpreter::getInterpreterState() const {
  std::ostringstream oss;
  oss << "Interpreter State:\n";
  oss << "  Debug flags:\n";
  oss << "    lexer: " << (debug.lexer ? "true" : "false") << "\n";
  oss << "    parser: " << (debug.parser ? "true" : "false") << "\n";
  oss << "    ast: " << (debug.ast ? "true" : "false") << "\n";
  oss << "    bytecode: " << (debug.bytecode ? "true" : "false") << "\n";
  oss << "    jit: " << (debug.jit ? "true" : "false") << "\n";
  oss << "  Control flags:\n";
  oss << "    stopOnError: " << (stopOnError ? "true" : "false") << "\n";
  oss << "    showASTOnParse: " << (showASTOnParse ? "true" : "false") << "\n";
  oss << "  Runtime:\n";
  oss << "    reloadEnabled: " << (reloadEnabled ? "true" : "false") << "\n";
  oss << "    timers: " << timers.size() << "\n";
  return oss.str();
}

} // namespace havel
