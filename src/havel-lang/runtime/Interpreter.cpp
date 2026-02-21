#include "Interpreter.hpp"
#include "core/BrightnessManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/automation/AutomationManager.hpp"
#include "core/browser/BrowserModule.hpp"
#include "core/io/EventListener.hpp"
#include "core/io/KeyTap.hpp"
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
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <signal.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
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
    return ret->value;
  }
  if (auto *err = std::get_if<HavelRuntimeError>(&result)) {
    throw *err;
  }
  // This should not be called on break/continue.
  throw std::runtime_error("Cannot unwrap control flow result");
}

std::string Interpreter::ValueToString(const HavelValue &value) {
  // Helper to format numbers nicely (remove trailing zeros)
  auto formatNumber = [](double d) -> std::string {
    std::string s = std::to_string(d);
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.')
      s.pop_back();
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
        } else
          return "unprintable";
      },
      value);
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
  if (std::holds_alternative<double>(value)) {
    double num = std::get<double>(value);
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
  } else if (std::holds_alternative<int>(value)) {
    int num = std::get<int>(value);
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
  if (std::holds_alternative<HavelValue>(result)) {
    return ValueToBool(std::get<HavelValue>(result));
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
      value);
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
      value);
}

// Constructor with Dependency Injection
Interpreter::Interpreter(IO &io_system, WindowManager &window_mgr,
                         HotkeyManager *hotkey_mgr,
                         BrightnessManager *brightness_mgr,
                         AudioManager *audio_mgr, GUIManager *gui_mgr,
                         ScreenshotManager *screenshot_mgr,
                         const std::vector<std::string> &cli_args)
    : io(io_system), windowManager(window_mgr), hotkeyManager(hotkey_mgr),
      brightnessManager(brightness_mgr), audioManager(audio_mgr),
      guiManager(gui_mgr), screenshotManager(screenshot_mgr),
      lastResult(HavelValue(nullptr)), cliArgs(cli_args) {
  info("Interpreter constructor called");
  environment = std::make_shared<Environment>();
  environment->Define("constructor_called", HavelValue(true));
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
    auto *programPtr = program.get();
    // Keep the AST alive to avoid dangling pointers captured in
    // functions/closures
    loadedPrograms.push_back(std::move(program));

    if (debug.ast) {
      std::cout << "AST: Parsed program:" << std::endl;
      if (programPtr) {
        parser.printAST(*programPtr);
      } else {
        std::cout << "AST: (null program)" << std::endl;
      }
    }

    auto result = Evaluate(*programPtr);
    return result;
  } catch (const havel::LexError &e) {
    return HavelRuntimeError("Lex error at line " + std::to_string(e.line) +
                             ", column " + std::to_string(e.column) + ": " +
                             e.what());
  } catch (const havel::parser::ParseError &e) {
    return HavelRuntimeError("Parse error at line " + std::to_string(e.line) +
                             ", column " + std::to_string(e.column) + ": " +
                             e.what());
  } catch (const std::exception &e) {
    return HavelRuntimeError(std::string("Parse error: ") + e.what());
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
  HavelValue lastValue = nullptr;
  for (const auto &stmt : node.body) {
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
    // Simple variable declaration: let x = value
    environment->Define(ident->symbol, value);
  } else if (auto *arrayPattern =
                 dynamic_cast<const ast::ArrayPattern *>(node.pattern.get())) {
    // Array destructuring: let [a, b] = arr
    if (!node.value) {
      lastResult =
          HavelRuntimeError("Array destructuring requires initialization");
      return;
    }

    if (auto *array = std::get_if<HavelArray>(&value)) {
      if (*array) {
        for (size_t i = 0;
             i < arrayPattern->elements.size() && i < (*array)->size(); ++i) {
          const auto &element = (*array)->at(i);
          const auto &pattern = arrayPattern->elements[i];

          if (auto *ident =
                  dynamic_cast<const ast::Identifier *>(pattern.get())) {
            environment->Define(ident->symbol, element);
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

    if (auto *object = std::get_if<HavelObject>(&value)) {
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
  lastResult = ReturnValue{value};
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
          if (modeVal && std::holds_alternative<std::string>(*modeVal)) {
            return std::get<std::string>(*modeVal) == condValue;
          }
          // If mode is not set or is not a string, default to false
          return false;
        });
      } else if (condType == "title") {
        contextChecks.push_back([this, condValue]() {
          std::string activeTitle = this->windowManager.GetActiveWindowTitle();
          return activeTitle.find(condValue) != std::string::npos;
        });
      } else if (condType == "class") {
        contextChecks.push_back([this, condValue]() {
          std::string activeClass = this->windowManager.GetActiveWindowClass();
          return activeClass.find(condValue) != std::string::npos;
        });
      } else if (condType == "process") {
        contextChecks.push_back([this, condValue]() {
          pID pid = this->windowManager.GetActiveWindowPID();
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
    io.Hotkey(hotkey, actionHandler);
  }

  // Return null after registering the hotkey
  lastResult = nullptr;
}
void Interpreter::visitExpressionStatement(
    const ast::ExpressionStatement &node) {
  lastResult = Evaluate(*node.expression);
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
    if (std::holds_alternative<std::string>(left) ||
        std::holds_alternative<std::string>(right)) {
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
    if (ValueToNumber(right) == 0.0) {
      lastResult = HavelRuntimeError("Division by zero");
      return;
    }
    lastResult = ValueToNumber(left) / ValueToNumber(right);
    break;
  case ast::BinaryOperator::Mod:
    if (ValueToNumber(right) == 0.0) {
      lastResult = HavelRuntimeError("Modulo by zero");
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
      lastResult = HavelRuntimeError("Undefined variable: " + id->symbol);
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

    if (auto *objPtr = std::get_if<HavelObject>(&objectValue)) {
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
    args.push_back(unwrap(argRes));
  }

  if (auto *builtin = std::get_if<BuiltinFunction>(&callee)) {
    lastResult = (*builtin)(args);
  } else if (auto *userFunc =
                 std::get_if<std::shared_ptr<HavelFunction>>(&callee)) {
    auto &func = *userFunc;
    if (args.size() != func->declaration->parameters.size()) {
      lastResult = HavelRuntimeError("Mismatched argument count for function " +
                                     func->declaration->name->symbol);
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
    lastResult = HavelRuntimeError("Attempted to call a non-callable value: " +
                                   ValueToString(callee));
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
  if (auto *objPtr = std::get_if<HavelObject>(&objectValue)) {
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
  if (auto *arrPtr = std::get_if<HavelArray>(&objectValue)) {
    if (propName == "length") {
      lastResult = static_cast<double>((*arrPtr) ? (*arrPtr)->size() : 0);
      return;
    }
  }

  lastResult = HavelRuntimeError("Member access not supported for this type");
}

void Interpreter::visitLambdaExpression(const ast::LambdaExpression &node) {
  // Capture current environment (closure)
  auto closureEnv = this->environment;
  // Build a callable that binds args to parameter names and evaluates body
  BuiltinFunction lambda =
      [this, closureEnv,
       &node](const std::vector<HavelValue> &args) -> HavelResult {
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
    if (std::holds_alternative<ReturnValue>(res))
      return std::get<ReturnValue>(res).value;
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
    if (auto *builtin = std::get_if<BuiltinFunction>(&callee)) {
      currentResult = (*builtin)(args);
    } else if (auto *userFunc =
                   std::get_if<std::shared_ptr<HavelFunction>>(&callee)) {
      // This logic is duplicated from visitCallExpression, could be refactored
      auto &func = *userFunc;
      if (args.size() != func->declaration->parameters.size()) {
        lastResult = HavelRuntimeError(
            "Mismatched argument count for function in pipeline");
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
      if (std::holds_alternative<ReturnValue>(currentResult)) {
        currentResult = std::get<ReturnValue>(currentResult).value;
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
      if (!val || !std::holds_alternative<HavelObject>(*val)) {
        lastResult = HavelRuntimeError(
            "Built-in module not found or not an object: " + moduleName);
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
    if (path.rfind("havel:", 0) == 0)
      moduleName = path.substr(6);
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
      std::string source((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

      // Execute module in a new environment
      Interpreter moduleInterpreter(io, windowManager);
      auto moduleResult = moduleInterpreter.Execute(source);
      if (isError(moduleResult)) {
        lastResult = moduleResult;
        return;
      }

      HavelValue exportedValue = unwrap(moduleResult);
      if (!std::holds_alternative<HavelObject>(exportedValue)) {
        lastResult = HavelRuntimeError(
            "Module must return an object of exports: " + path);
        return;
      }
      exports = std::get<HavelObject>(exportedValue);
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
    if (!std::holds_alternative<HavelObject>(*moduleVal)) {
      lastResult = HavelRuntimeError("Not a module/object: " + moduleName);
      return;
    }

    auto moduleObj = std::get<HavelObject>(*moduleVal);
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
  if (!std::holds_alternative<HavelObject>(*objectVal)) {
    lastResult = HavelRuntimeError("Not an object: " + node.objectName);
    return;
  }

  auto withObject = std::get<HavelObject>(*objectVal);
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
  lastResult = node.value;
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

  lastResult = result;
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
    lastResult = HavelRuntimeError("Undefined variable: " + node.symbol);
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
    array->push_back(unwrap(result));
  }

  lastResult = HavelValue(array);
}

void Interpreter::visitObjectLiteral(const ast::ObjectLiteral &node) {
  auto object = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  for (const auto &[key, valueExpr] : node.pairs) {
    auto result = Evaluate(*valueExpr);
    if (isError(result)) {
      lastResult = result;
      return;
    }
    (*object)[key] = unwrap(result);
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

  // Process all config key-value pairs
  for (const auto &[key, valueExpr] : node.pairs) {
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
      auto &defaults = std::get<HavelObject>(value);
      if (defaults)
        for (const auto &[defaultKey, defaultValue] : *defaults) {
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
  environment->Define("config", HavelValue(configObject));

  lastResult = nullptr; // Config blocks don't return a value
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
    if (std::holds_alternative<HavelObject>(value)) {
      auto &modeConfig = std::get<HavelObject>(value);

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
  if (auto *arrayPtr = std::get_if<HavelArray>(&objectValue)) {
    // Convert index to integer
    int index = static_cast<int>(ValueToNumber(indexValue));

    if (!*arrayPtr || index < 0 ||
        index >= static_cast<int>((*arrayPtr)->size())) {
      lastResult = HavelRuntimeError("Array index out of bounds: " +
                                     std::to_string(index));
      return;
    }

    lastResult = (**arrayPtr)[index];
    return;
  }

  // Handle object property access
  if (auto *objectPtr = std::get_if<HavelObject>(&objectValue)) {
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
    if (std::holds_alternative<double>(switchValue) &&
        std::holds_alternative<double>(testValue)) {
      matches = (std::get<double>(switchValue) == std::get<double>(testValue));
    } else if (std::holds_alternative<std::string>(switchValue) &&
               std::holds_alternative<std::string>(testValue)) {
      matches = (std::get<std::string>(switchValue) ==
                 std::get<std::string>(testValue));
    } else if (std::holds_alternative<bool>(switchValue) &&
               std::holds_alternative<bool>(testValue)) {
      matches = (std::get<bool>(switchValue) == std::get<bool>(testValue));
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

  // Create an array from start to end (inclusive)
  auto rangeArray = std::make_shared<std::vector<HavelValue>>();
  for (int i = start; i <= end; ++i) {
    rangeArray->push_back(HavelValue(i));
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
          HavelRuntimeError("Undefined variable: " + identifier->symbol);
      return;
    }
    HavelValue newValue = applyCompound(op, *current, value);
    if (!environment->Assign(identifier->symbol, newValue)) {
      lastResult =
          HavelRuntimeError("Undefined variable: " + identifier->symbol);
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

    if (auto *arrayPtr = std::get_if<HavelArray>(&objectValue)) {
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
    } else if (auto *objectPtr = std::get_if<HavelObject>(&objectValue)) {
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
  } else {
    lastResult = HavelRuntimeError("Invalid assignment target");
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

  // Execute finally block if present (always runs)
  if (node.finallyBlock) {
    auto finallyResult = Evaluate(*node.finallyBlock);
    if (isError(finallyResult)) {
      lastResult = finallyResult;
      return;
    }
  }

  // Check if try body threw an error
  if (auto *err = std::get_if<HavelRuntimeError>(&tryResult)) {
    // If we have a catch block, execute it
    if (node.catchBody) {
      // If catch variable is specified, create it in current scope
      if (node.catchVariable) {
        // Store the error value in the catch variable
        // For now, we'll store the error message as string
        std::string errorMsg = err->what();
        environment->Define(node.catchVariable->symbol, HavelValue(errorMsg));
      }

      auto catchResult = Evaluate(*node.catchBody);
      if (isError(catchResult)) {
        lastResult = catchResult;
        return;
      }
      lastResult = catchResult;
      return;
    }

    // No catch handler, re-throw the original error
    lastResult = *err;
    return;
  }

  // Try body succeeded, return its result
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

  // Store the thrown value as a runtime error
  // This preserves the original value type instead of converting to string
  lastResult =
      HavelRuntimeError("Thrown: " + ValueToString(unwrap(valueResult)));
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
  if (std::holds_alternative<HavelArray>(iterableValue)) {
    // Array iteration
    auto array = std::get<HavelArray>(iterableValue);
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
  } else if (std::holds_alternative<HavelObject>(iterableValue)) {
    // Object iteration
    auto object = std::get<HavelObject>(iterableValue);
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

void Interpreter::visitOffModeStatement(const ast::OffModeStatement &node) {
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

  environment->Define("app", HavelValue(appObj));

  // Debug control builtins
  auto debugObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  BuiltinFunction lexerToggle =
      [this](const std::vector<HavelValue> &args) -> HavelValue {
    if (args.size() >= 1) {
      if (auto b = std::get_if<bool>(&args[0])) {
        this->debug.lexer = *b;
      }
    }
    return HavelValue(nullptr);
  };
  (*debugObj)["lexer"] = HavelValue(lexerToggle);

  (*debugObj)["parser"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = std::get_if<bool>(&args[0])) {
            this->debug.parser = *b;
          }
        }
        return HavelValue(nullptr);
      }));

  (*debugObj)["ast"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = std::get_if<bool>(&args[0])) {
            this->debug.ast = *b;
          }
        }
        return HavelValue(nullptr);
      }));

  (*debugObj)["bytecode"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = std::get_if<bool>(&args[0])) {
            this->debug.bytecode = *b;
          }
        }
        return HavelValue(nullptr);
      }));

  (*debugObj)["jit"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelValue {
        if (args.size() >= 1) {
          if (auto b = std::get_if<bool>(&args[0])) {
            this->debug.jit = *b;
          }
        }
        return HavelValue(nullptr);
      }));

  environment->Define("debug", HavelValue(debugObj));

  // Initialize all builtin modules
  InitializeSystemBuiltins();
  InitializeWindowBuiltins();
  InitializeClipboardBuiltins();
  InitializeTextBuiltins();
  InitializeFileBuiltins();
  InitializeArrayBuiltins();
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
  if (auto v = environment->Get("io.mouseMove"))
    (*ioMod)["mouseMove"] = *v;
  if (auto v = environment->Get("io.mouseMoveTo"))
    (*ioMod)["mouseMoveTo"] = *v;
  if (auto v = environment->Get("io.mouseClick"))
    (*ioMod)["mouseClick"] = *v;
  if (auto v = environment->Get("io.mouseDown"))
    (*ioMod)["mouseDown"] = *v;
  if (auto v = environment->Get("io.mouseUp"))
    (*ioMod)["mouseUp"] = *v;
  if (auto v = environment->Get("io.mouseWheel"))
    (*ioMod)["mouseWheel"] = *v;
  if (auto v = environment->Get("io.getKeyState"))
    (*ioMod)["getKeyState"] = *v;
  if (auto v = environment->Get("io.isShiftPressed"))
    (*ioMod)["isShiftPressed"] = *v;
  if (auto v = environment->Get("io.isCtrlPressed"))
    (*ioMod)["isCtrlPressed"] = *v;
  if (auto v = environment->Get("io.isAltPressed"))
    (*ioMod)["isAltPressed"] = *v;
  if (auto v = environment->Get("io.isWinPressed"))
    (*ioMod)["isWinPressed"] = *v;
  if (auto v = environment->Get("io.scroll"))
    (*ioMod)["scroll"] = *v;
  if (auto v = environment->Get("io.getMouseSensitivity"))
    (*ioMod)["getMouseSensitivity"] = *v;
  if (auto v = environment->Get("io.setMouseSensitivity"))
    (*ioMod)["setMouseSensitivity"] = *v;
  if (auto v = environment->Get("io.emergencyReleaseAllKeys"))
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
          if (std::holds_alternative<BuiltinFunction>(tapAction)) {
            auto func = std::get<BuiltinFunction>(tapAction);
            onTap = [this, func]() {
              auto result = func({});
              if (isError(result)) {
                std::cerr << "Error in tap action: "
                          << std::get<HavelRuntimeError>(result).what()
                          << std::endl;
              }
            };
          } else if (std::holds_alternative<std::string>(tapAction)) {
            std::string cmd = ValueToString(tapAction);
            onTap = [this, cmd]() { io.Send(cmd); };
          }
        }

        // Handle tapCondition parameter (string or lambda function)
        if (args.size() >= 3) {
          auto condition = args[2];
          if (std::holds_alternative<std::string>(condition)) {
            tapCondition = ValueToString(condition);
          } else if (std::holds_alternative<BuiltinFunction>(condition)) {
            auto func = std::get<BuiltinFunction>(condition);
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
          if (std::holds_alternative<BuiltinFunction>(comboAction)) {
            auto func = std::get<BuiltinFunction>(comboAction);
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
        return HavelValue(keyName + " KeyTap created");
      })));
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

  // Core verb functions - global for fast typing
  environment->Define(
      "sleep",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("sleep() requires milliseconds");
            int ms = static_cast<int>(ValueToNumber(args[0]));
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
                    io.Send(keys.c_str());
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
          if (auto *builtin = std::get_if<BuiltinFunction>(&fn)) {
            res = (*builtin)(fnArgs);
          } else if (auto *userFunc =
                         std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
            auto &func = *userFunc;
            auto funcEnv = std::make_shared<Environment>(func->closure);
            for (size_t p = 0;
                 p < func->declaration->parameters.size() && p < fnArgs.size();
                 ++p) {
              funcEnv->Define(func->declaration->parameters[p]->symbol,
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
        auto currentModeOpt = environment->Get("__current_mode__");
        std::string currentMode = "default";
        if (currentModeOpt &&
            std::holds_alternative<std::string>(*currentModeOpt)) {
          currentMode = std::get<std::string>(*currentModeOpt);
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

        // Set new current mode
        environment->Define("__current_mode__", HavelValue(newMode));
        return HavelValue(nullptr);
      });

  // Switch to previous mode
  (*modeObj)["toggle"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        auto currentModeOpt = environment->Get("__current_mode__");
        auto previousModeOpt = environment->Get("__previous_mode__");

        std::string currentMode = "default";
        std::string previousMode = "default";

        if (currentModeOpt &&
            std::holds_alternative<std::string>(*currentModeOpt)) {
          currentMode = std::get<std::string>(*currentModeOpt);
        }
        if (previousModeOpt &&
            std::holds_alternative<std::string>(*previousModeOpt)) {
          previousMode = std::get<std::string>(*previousModeOpt);
        }

        // Swap modes
        environment->Define("__previous_mode__", HavelValue(currentMode));
        environment->Define("__current_mode__", HavelValue(previousMode));
        return HavelValue(nullptr);
      });

  // Check if in specific mode
  (*modeObj)["is"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("mode.is() requires mode name");
        std::string checkMode = this->ValueToString(args[0]);

        auto currentModeOpt = environment->Get("__current_mode__");
        if (currentModeOpt &&
            std::holds_alternative<std::string>(*currentModeOpt)) {
          std::string currentMode = std::get<std::string>(*currentModeOpt);
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
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("sleep() requires milliseconds");
        double ms = ValueToNumber(args[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds((int)ms));
        return HavelValue(nullptr);
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
        return std::visit(
            [](auto &&arg) -> HavelValue {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, std::nullptr_t>)
                return HavelValue("null");
              else if constexpr (std::is_same_v<T, bool>)
                return HavelValue("boolean");
              else if constexpr (std::is_same_v<T, int> ||
                                 std::is_same_v<T, double>)
                return HavelValue("number");
              else if constexpr (std::is_same_v<T, std::string>)
                return HavelValue("string");
              else if constexpr (std::is_same_v<T, HavelArray>)
                return HavelValue("array");
              else if constexpr (std::is_same_v<T, HavelObject>)
                return HavelValue("object");
              else if constexpr (std::is_same_v<T,
                                                std::shared_ptr<HavelFunction>>)
                return HavelValue("function");
              else if constexpr (std::is_same_v<T, BuiltinFunction>)
                return HavelValue("builtin");
              else
                return HavelValue("unknown");
            },
            args[0]);
      }));

  // Send text/keys to the system
  environment->Define(
      "send", BuiltinFunction(
                  [this](const std::vector<HavelValue> &args) -> HavelResult {
                    if (args.empty())
                      return HavelRuntimeError("send() requires text");
                    std::string text = this->ValueToString(args[0]);
                    this->io.Send(text.c_str());
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
          if (std::holds_alternative<std::string>(v)) {
            std::string cmd = std::get<std::string>(v);
            return [cmd]() { Launcher::runShellDetached(cmd.c_str()); };
          }

          if (std::holds_alternative<std::shared_ptr<HavelFunction>>(v)) {
            auto fn = std::get<std::shared_ptr<HavelFunction>>(v);
            return [this, fn]() { this->Evaluate(*fn->declaration); };
          }

          if (std::holds_alternative<BuiltinFunction>(v)) {
            auto fn = std::get<BuiltinFunction>(v);
            return [fn]() { fn({}); };
          }

          throw HavelRuntimeError("Invalid action type");
        };

        // --- helper: convert value → bool() ---
        auto toBoolCondition = [this](const HavelValue &v)
            -> std::variant<std::string, std::function<bool()>> {
          if (std::holds_alternative<std::string>(v)) {
            return std::get<std::string>(v);
          }

          if (std::holds_alternative<std::shared_ptr<HavelFunction>>(v)) {
            auto fn = std::get<std::shared_ptr<HavelFunction>>(v);
            return [this, fn]() {
              auto result = this->Evaluate(*fn->declaration);
              return ExecResultToBool(result);
            };
          }

          if (std::holds_alternative<BuiltinFunction>(v)) {
            auto fn = std::get<BuiltinFunction>(v);
            return [this, fn]() {
              auto result = fn({});
              return ExecResultToBool(result);
            };
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
        if (std::holds_alternative<std::string>(condition)) {
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
        if (std::holds_alternative<double>(args[0])) {
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

        if (std::holds_alternative<bool>(value)) {
          config.Set(key, std::get<bool>(value));
        } else if (std::holds_alternative<int>(value)) {
          config.Set(key, std::get<int>(value));
        } else if (std::holds_alternative<double>(value)) {
          config.Set(key, std::get<double>(value));
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
        if (io.GetEventListener()) {
          info("Stopping EventListener before exit...");
          io.GetEventListener()->Stop();
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
      "io.getKeyState",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("io.getKeyState() requires key name");
            std::string key = this->ValueToString(args[0]);
            return HavelValue(this->io.GetKeyState(key));
          }));

  environment->Define(
      "io.isShiftPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io.IsShiftPressed());
          }));

  environment->Define(
      "io.isCtrlPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io.IsCtrlPressed());
          }));

  environment->Define(
      "io.isAltPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io.IsAltPressed());
          }));

  environment->Define(
      "io.isWinPressed",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->io.IsWinPressed());
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
            bool muted = std::get<bool>(args[0]);
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
        uint32_t index = static_cast<uint32_t>(std::get<double>(args[0]));
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
            double volume = std::get<double>(args[1]);
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
            double amount = args.size() > 1 ? std::get<double>(args[1]) : 0.05;
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
            double amount = args.size() > 1 ? std::get<double>(args[1]) : 0.05;
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
            double volume = std::get<double>(args[0]);
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
            double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
            return HavelValue(
                this->audioManager->increaseActiveApplicationVolume(amount));
          }));

  environment->Define(
      "audio.decreaseActiveAppVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            double amount = args.empty() ? 0.05 : std::get<double>(args[0]);
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

  // Expose as module objects
  auto clip = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("clipboard.get"))
    (*clip)["get"] = *v;
  if (auto v = environment->Get("clipboard.set"))
    (*clip)["set"] = *v;
  if (auto v = environment->Get("clipboard.clear"))
    (*clip)["clear"] = *v;
  environment->Define("clipboard", HavelValue(clip));

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
        bool ignoreCache = !args.empty() && std::get<double>(args[0]) != 0;
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
        double level = std::get<double>(args[0]);
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
          (*obj)["title"] = HavelValue(tab.title);
          (*obj)["url"] = HavelValue(tab.url);
          (*obj)["type"] = HavelValue(tab.type);
          arr->push_back(HavelValue(obj));
        }
        return HavelValue(arr);
      }));

  (*browserMod)["activate"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.activate() requires tabId");
        int tabId = static_cast<int>(std::get<double>(args[0]));
        return HavelValue(getBrowser().activate(tabId));
      }));

  (*browserMod)["close"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int tabId =
            args.empty() ? -1 : static_cast<int>(std::get<double>(args[0]));
        return HavelValue(getBrowser().closeTab(tabId));
      }));

  (*browserMod)["closeAll"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(getBrowser().closeAll());
      }));

  // === New Browser Functions ===

  (*browserMod)["connectFirefox"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int port =
            args.empty() ? 2828 : static_cast<int>(std::get<double>(args[0]));
        return HavelValue(getBrowser().connectFirefox(port));
      }));

  (*browserMod)["setPort"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("browser.setPort() requires port number");
        int port = static_cast<int>(std::get<double>(args[0]));
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
        int width = static_cast<int>(std::get<double>(args[0]));
        int height = static_cast<int>(std::get<double>(args[1]));
        return HavelValue(getBrowser().setWindowSize(-1, width, height));
      }));

  (*browserMod)["setWindowPosition"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError(
              "browser.setWindowPosition() requires (x, y)");
        int x = static_cast<int>(std::get<double>(args[0]));
        int y = static_cast<int>(std::get<double>(args[1]));
        return HavelValue(getBrowser().setWindowPosition(-1, x, y));
      }));

  (*browserMod)["maximizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId = args.empty() ? -1 : static_cast<int>(std::get<double>(args[0]));
        return HavelValue(getBrowser().maximizeWindow(windowId));
      }));

  (*browserMod)["minimizeWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId = args.empty() ? -1 : static_cast<int>(std::get<double>(args[0]));
        return HavelValue(getBrowser().minimizeWindow(windowId));
      }));

  (*browserMod)["fullscreenWindow"] = HavelValue(BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int windowId = args.empty() ? -1 : static_cast<int>(std::get<double>(args[0]));
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
        int timeout = static_cast<int>(std::get<double>(args[0]));
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
            return HavelValue(this->windowManager.GetActiveWindowTitle());
          }));

  environment->Define(
      "window.getPID",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            return HavelValue(
                static_cast<double>(this->windowManager.GetActiveWindowPID()));
          }));

  environment->Define(
      "window.maximize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin = Window(this->windowManager.GetActiveWindow());
            activeWin.Max();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.minimize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin = Window(this->windowManager.GetActiveWindow());
            activeWin.Min();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.next",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager.AltTab();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.previous",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager.AltTab();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.close",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window w(this->windowManager.GetActiveWindow());
            w.Close();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.center",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            this->windowManager.Center(this->windowManager.GetActiveWindow());
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
            int x = static_cast<int>(std::get<double>(args[0]));
            int y = static_cast<int>(std::get<double>(args[1]));
            Window activeWin(this->windowManager.GetActiveWindow());
            return HavelValue(activeWin.Move(x, y));
          }));

  environment->Define(
      "window.resize",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("window.resize() requires (width, height)");
        int width = static_cast<int>(std::get<double>(args[0]));
        int height = static_cast<int>(std::get<double>(args[1]));
        Window activeWin(this->windowManager.GetActiveWindow());
        return HavelValue(activeWin.Resize(width, height));
      }));

  environment->Define(
      "window.moveResize",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 4)
              return HavelRuntimeError(
                  "window.moveResize() requires (x, y, width, height)");
            int x = static_cast<int>(std::get<double>(args[0]));
            int y = static_cast<int>(std::get<double>(args[1]));
            int width = static_cast<int>(std::get<double>(args[2]));
            int height = static_cast<int>(std::get<double>(args[3]));
            Window activeWin(this->windowManager.GetActiveWindow());
            return HavelValue(activeWin.MoveResize(x, y, width, height));
          }));

  // Note: Hide/Show not implemented yet in Window class
  // environment->Define("window.hide", BuiltinFunction([this](const
  // std::vector<HavelValue>& args) -> HavelResult {
  //     Window activeWin(this->windowManager.GetActiveWindow());
  //     activeWin.Hide();
  //     return HavelValue(nullptr);
  // }));
  //
  // environment->Define("window.show", BuiltinFunction([this](const
  // std::vector<HavelValue>& args) -> HavelResult {
  //     Window activeWin(this->windowManager.GetActiveWindow());
  //     activeWin.Show();
  //     return HavelValue(nullptr);
  // }));

  environment->Define(
      "window.alwaysOnTop",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            bool top = args.empty() ? true : std::get<bool>(args[0]);
            Window activeWin(this->windowManager.GetActiveWindow());
            activeWin.AlwaysOnTop(top);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.transparency",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        int alpha =
            args.empty() ? 255 : static_cast<int>(std::get<double>(args[0]));
        Window activeWin(this->windowManager.GetActiveWindow());
        activeWin.Transparency(alpha);
        return HavelValue(nullptr);
      }));

  environment->Define(
      "window.toggleFullscreen",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            Window activeWin(this->windowManager.GetActiveWindow());
            activeWin.ToggleFullscreen();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "window.snap",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("window.snap() requires position (0-3)");
            int position = static_cast<int>(std::get<double>(args[0]));
            Window activeWin(this->windowManager.GetActiveWindow());
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
            int monitor = static_cast<int>(std::get<double>(args[0]));
            Window activeWin(this->windowManager.GetActiveWindow());
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
            Window activeWin(this->windowManager.GetActiveWindow());
            return HavelValue(activeWin.MoveToCorner(corner));
          }));

  environment->Define(
      "window.getClass",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            return HavelValue(this->windowManager.GetActiveWindowClass());
          }));

  environment->Define(
      "window.exists",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty()) {
              Window activeWin(this->windowManager.GetActiveWindow());
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
            Window activeWin(this->windowManager.GetActiveWindow());
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
  environment->Define("window", HavelValue(win));
}

void Interpreter::InitializeClipboardBuiltins() {
  environment->Define(
      "clipboard.get",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        QClipboard *clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
      }));

  environment->Define(
      "clipboard.set",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("clipboard.set() requires text");
            std::string text = this->ValueToString(args[0]);
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(QString::fromStdString(text));
            return HavelValue(true);
          }));

  environment->Define(
      "clipboard.clear",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->clear();
        return HavelValue(nullptr);
      }));
}

void Interpreter::InitializeTextBuiltins() {
  // format(formatString, arg0, arg1, ...) - Python-style string formatting
  // Supports: {0}, {0:.4f}, {0:.2}, {0:d}, etc.
  environment->Define(
      "format",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError(
              "format() requires at least a format string");

        std::string formatStr = this->ValueToString(args[0]);
        std::string result;
        size_t pos = 0;
        size_t argIndex = 0;

        while (pos < formatStr.length()) {
          size_t openBrace = formatStr.find('{', pos);

          // No more placeholders, append rest of string
          if (openBrace == std::string::npos) {
            result += formatStr.substr(pos);
            break;
          }

          // Append text before placeholder
          result += formatStr.substr(pos, openBrace - pos);

          // Find closing brace
          size_t closeBrace = formatStr.find('}', openBrace);
          if (closeBrace == std::string::npos) {
            return HavelRuntimeError("Unclosed placeholder in format string");
          }

          // Parse placeholder: {index[:format]}
          std::string placeholder =
              formatStr.substr(openBrace + 1, closeBrace - openBrace - 1);

          // Extract index and format specifier
          size_t colonPos = placeholder.find(':');
          size_t index = 0;
          std::string formatSpec;

          if (colonPos == std::string::npos) {
            // Just index: {0} or empty: {}
            if (!placeholder.empty()) {
              try {
                index = std::stoul(placeholder);
              } catch (...) {
                return HavelRuntimeError("Invalid placeholder index");
              }
            } else {
              index = argIndex++;
            }
          } else {
            // Index with format: {0:.4f}
            try {
              index = std::stoul(placeholder.substr(0, colonPos));
            } catch (...) {
              return HavelRuntimeError("Invalid placeholder index");
            }
            formatSpec = placeholder.substr(colonPos + 1);
          }

          // Get argument value
          if (index + 1 > args.size()) {
            return HavelRuntimeError("Placeholder index out of range");
          }

          const HavelValue &value = args[index + 1];
          result += this->FormatValue(value, formatSpec);
          argIndex++;

          pos = closeBrace + 1;
        }

        return HavelValue(result);
      }));

  environment->Define(
      "upper", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.empty())
                       return HavelRuntimeError("upper() requires text");
                     std::string text = this->ValueToString(args[0]);
                     std::transform(text.begin(), text.end(), text.begin(),
                                    ::toupper);
                     return HavelValue(text);
                   }));

  environment->Define(
      "lower", BuiltinFunction(
                   [this](const std::vector<HavelValue> &args) -> HavelResult {
                     if (args.empty())
                       return HavelRuntimeError("lower() requires text");
                     std::string text = this->ValueToString(args[0]);
                     std::transform(text.begin(), text.end(), text.begin(),
                                    ::tolower);
                     return HavelValue(text);
                   }));

  environment->Define(
      "trim",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("trim() requires text");
            std::string text = this->ValueToString(args[0]);
            // Trim whitespace
            text.erase(text.begin(), std::find_if(text.begin(), text.end(),
                                                  [](unsigned char ch) {
                                                    return !std::isspace(ch);
                                                  }));
            text.erase(
                std::find_if(text.rbegin(), text.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                    .base(),
                text.end());
            return HavelValue(text);
          }));

  environment->Define(
      "length", BuiltinFunction(
                    [this](const std::vector<HavelValue> &args) -> HavelResult {
                      if (args.empty())
                        return HavelRuntimeError("length() requires text");
                      std::string text = this->ValueToString(args[0]);
                      return HavelValue((double)text.length());
                    }));

  environment->Define(
      "replace",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 3)
              return HavelRuntimeError(
                  "replace() requires (text, search, replacement)");
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

  environment->Define(
      "contains",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("contains() requires (text, search)");
            std::string text = this->ValueToString(args[0]);
            std::string search = this->ValueToString(args[1]);
            return HavelValue(text.find(search) != std::string::npos);
          }));

  environment->Define(
      "substr",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("substr() requires (text, start[, length])");
        std::string text = this->ValueToString(args[0]);
        int start = static_cast<int>(ValueToNumber(args[1]));
        if (start < 0)
          start = 0;
        if (start > static_cast<int>(text.size()))
          start = static_cast<int>(text.size());

        if (args.size() >= 3) {
          int length = static_cast<int>(ValueToNumber(args[2]));
          if (length < 0)
            length = 0;
          return HavelValue(text.substr(static_cast<size_t>(start),
                                        static_cast<size_t>(length)));
        }

        return HavelValue(text.substr(static_cast<size_t>(start)));
      }));

  environment->Define(
      "left",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("left() requires (text, count)");
            std::string text = this->ValueToString(args[0]);
            int count = static_cast<int>(ValueToNumber(args[1]));
            if (count <= 0)
              return HavelValue(std::string(""));
            if (count >= static_cast<int>(text.size()))
              return HavelValue(text);
            return HavelValue(text.substr(0, static_cast<size_t>(count)));
          }));

  environment->Define(
      "right",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("right() requires (text, count)");
            std::string text = this->ValueToString(args[0]);
            int count = static_cast<int>(ValueToNumber(args[1]));
            if (count <= 0)
              return HavelValue(std::string(""));
            if (count >= static_cast<int>(text.size()))
              return HavelValue(text);
            return HavelValue(
                text.substr(text.size() - static_cast<size_t>(count)));
          }));
}

void Interpreter::InitializeFileBuiltins() {
  environment->Define(
      "file.read",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("file.read() requires path");
            std::string path = this->ValueToString(args[0]);
            std::ifstream file(path);
            if (!file.is_open())
              return HavelRuntimeError("Cannot open file: " + path);

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            return HavelValue(content);
          }));

  environment->Define(
      "file.write",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() < 2)
              return HavelRuntimeError("file.write() requires (path, content)");
            std::string path = this->ValueToString(args[0]);
            std::string content = this->ValueToString(args[1]);

            std::ofstream file(path);
            if (!file.is_open())
              return HavelRuntimeError("Cannot write to file: " + path);

            file << content;
            return HavelValue(true);
          }));

  environment->Define(
      "file.exists",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.empty())
              return HavelRuntimeError("file.exists() requires path");
            std::string path = this->ValueToString(args[0]);
            return HavelValue(std::filesystem::exists(path));
          }));
}

void Interpreter::InitializeArrayBuiltins() {
  // Array map
  environment->Define(
      "map",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("map() requires (array, function)");
        if (!std::holds_alternative<HavelArray>(args[0]))
          return HavelRuntimeError("map() first arg must be array");

        auto array = std::get<HavelArray>(args[0]);
        auto &fn = args[1];

        auto result = std::make_shared<std::vector<HavelValue>>();
        if (array) {
          for (const auto &item : *array) {
            // Call function with item
            std::vector<HavelValue> fnArgs = {item};
            HavelResult res;

            if (auto *builtin = std::get_if<BuiltinFunction>(&fn)) {
              res = (*builtin)(fnArgs);
            } else if (auto *userFunc =
                           std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
              auto &func = *userFunc;
              if (fnArgs.size() != func->declaration->parameters.size()) {
                return HavelRuntimeError("Function parameter count mismatch");
              }

              auto funcEnv = std::make_shared<Environment>(func->closure);
              for (size_t i = 0; i < fnArgs.size(); ++i) {
                funcEnv->Define(func->declaration->parameters[i]->symbol,
                                fnArgs[i]);
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

            if (isError(res))
              return res;
            result->push_back(unwrap(res));
          }
        }
        return HavelValue(result);
      }));

  // Array filter
  environment->Define(
      "filter",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("filter() requires (array, predicate)");
        if (!std::holds_alternative<HavelArray>(args[0]))
          return HavelRuntimeError("filter() first arg must be array");

        auto array = std::get<HavelArray>(args[0]);
        auto &fn = args[1];

        auto result = std::make_shared<std::vector<HavelValue>>();
        if (array) {
          for (const auto &item : *array) {
            std::vector<HavelValue> fnArgs = {item};
            HavelResult res;

            if (auto *builtin = std::get_if<BuiltinFunction>(&fn)) {
              res = (*builtin)(fnArgs);
            } else if (auto *userFunc =
                           std::get_if<std::shared_ptr<HavelFunction>>(&fn)) {
              auto &func = *userFunc;
              auto funcEnv = std::make_shared<Environment>(func->closure);
              for (size_t i = 0; i < fnArgs.size(); ++i) {
                funcEnv->Define(func->declaration->parameters[i]->symbol,
                                fnArgs[i]);
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

            if (isError(res))
              return res;
            if (ValueToBool(unwrap(res))) {
              result->push_back(item);
            }
          }
        }
        return HavelValue(result);
      }));

  // Array push
  environment->Define(
      "push",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("push() requires (array, value)");
        if (!std::holds_alternative<HavelArray>(args[0]))
          return HavelRuntimeError("push() first arg must be array");

        auto array = std::get<HavelArray>(args[0]);
        if (!array)
          return HavelRuntimeError("push() received null array");
        array->push_back(args[1]);
        return HavelValue(array);
      }));

  // Array pop
  environment->Define(
      "pop",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("pop() requires array");
        if (!std::holds_alternative<HavelArray>(args[0]))
          return HavelRuntimeError("pop() arg must be array");

        auto array = std::get<HavelArray>(args[0]);
        if (!array || array->empty())
          return HavelRuntimeError("Cannot pop from empty array");

        HavelValue last = array->back();
        array->pop_back();
        return last;
      }));

  // Array join
  environment->Define(
      "join",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("join() requires array");
        if (!std::holds_alternative<HavelArray>(args[0]))
          return HavelRuntimeError("join() first arg must be array");

        auto array = std::get<HavelArray>(args[0]);
        std::string separator = args.size() > 1 ? ValueToString(args[1]) : ",";

        std::string result;
        if (array) {
          for (size_t i = 0; i < array->size(); ++i) {
            result += ValueToString((*array)[i]);
            if (i < array->size() - 1)
              result += separator;
          }
        }
        return HavelValue(result);
      }));

  // String split
  environment->Define(
      "split",
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("split() requires string");
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
            this->io.Map(from, to);
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
            this->io.Remap(key1, key2);
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
            return HavelValue(this->io.Suspend());
          }));

  // IO resume - only resumes if currently suspended
  environment->Define(
      "io.resume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            if (this->io.isSuspended) {
              return HavelValue(this->io.Suspend());
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

        if (!io.MouseMove(dx, dy))
          return HavelRuntimeError("MouseMove failed");

        return HavelValue(true);
      });

  //
  // mouse.moveTo(x, y)
  //
  (*mouseObj)["moveTo"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 2)
          return HavelRuntimeError("mouse.moveTo(x, y) requires 2 arguments");

        int x = static_cast<int>(ValueToNumber(args[0]));
        int y = static_cast<int>(ValueToNumber(args[1]));

        if (!io.MouseMoveTo(x, y))
          return HavelRuntimeError("MouseMoveTo failed");

        return HavelValue(true);
      });

  //
  // mouse.down(button)
  //
  (*mouseObj)["down"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int button =
            args.empty() ? 1 : static_cast<int>(ValueToNumber(args[0]));

        if (!io.MouseDown(button))
          return HavelRuntimeError("MouseDown failed");

        return HavelValue(true);
      });

  //
  // mouse.up(button)
  //
  (*mouseObj)["up"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        int button =
            args.empty() ? 1 : static_cast<int>(ValueToNumber(args[0]));

        if (!io.MouseUp(button))
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
        int button = 1;
        bool doDown = true;
        bool doUp = true;

        if (!args.empty()) {
          std::string buttonStr = toLower(ValueToString(args[0]));
          if (buttonStr == "left") {
            button = 1;
          } else if (buttonStr == "right") {
            button = 2;
          } else if (buttonStr == "middle") {
            button = 3;
          } else {
            button = static_cast<int>(ValueToNumber(args[0]));
          }
        }

        if (args.size() >= 2) {
          bool down = ValueToNumber(args[1]) != 0;
          if (down) {
            doUp = false; // press only
          } else {
            doDown = false; // release only
          }
        }

        bool ok = true;

        if (doDown)
          ok &= io.MouseDown(button);

        if (doUp)
          ok &= io.MouseUp(button);

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

        if (!io.Scroll(dy, dx))
          return HavelRuntimeError("Scroll failed");

        return HavelValue(true);
      });

  (*mouseObj)["getSensitivity"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        return HavelValue(static_cast<double>(this->io.mouseSensitivity));
      });

  (*mouseObj)["setSensitivity"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.setMouseSensitivity() requires value");
        this->io.mouseSensitivity = ValueToNumber(args[0]);
        return HavelValue(static_cast<double>(this->io.mouseSensitivity));
      });
  environment->Define("mouse", mouseObj);
  environment->Define("click", (*mouseObj)["click"]);
  environment->Define(
      "io.emergencyReleaseAllKeys",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            (void)args;
            this->io.EmergencyReleaseAllKeys();
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
        return HavelValue(this->io.EnableHotkey(hotkey));
      }));

  environment->Define(
      "io.disableHotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.disableHotkey() requires hotkey name");
        std::string hotkey = ValueToString(args[0]);
        return HavelValue(this->io.DisableHotkey(hotkey));
      }));

  environment->Define(
      "io.toggleHotkey",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty())
          return HavelRuntimeError("io.toggleHotkey() requires hotkey name");
        std::string hotkey = ValueToString(args[0]);
        return HavelValue(this->io.ToggleHotkey(hotkey));
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
            bool isNumber = std::holds_alternative<int>(arg) ||
                            std::holds_alternative<double>(arg);

            if (isNumber) {
              int id = static_cast<int>(numVal);
              return HavelValue(this->io.RemoveHotkey(id));
            } else {
              std::string name = ValueToString(args[0]);
              return HavelValue(this->io.RemoveHotkey(name));
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
  // === MATH MODULE ===
  auto mathObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  auto &math = *mathObj;
  // Basic arithmetic functions
  math["abs"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("abs() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::abs(value));
      });

  math["ceil"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("ceil() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::ceil(value));
      });

  math["floor"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("floor() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::floor(value));
      });

  math["round"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("round() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::round(value));
      });

  // Trigonometric functions
  math["sin"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("sin() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::sin(value));
      });

  math["cos"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("cos() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::cos(value));
      });

  math["tan"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("tan() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::tan(value));
      });

  math["asin"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("asin() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value < -1.0 || value > 1.0)
          return HavelRuntimeError("asin() argument must be between -1 and 1");

        return HavelValue(std::asin(value));
      });

  math["acos"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("acos() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value < -1.0 || value > 1.0)
          return HavelRuntimeError("acos() argument must be between -1 and 1");

        return HavelValue(std::acos(value));
      });

  math["atan"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("atan() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::atan(value));
      });

  math["atan2"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 2)
          return HavelRuntimeError("atan2() requires 2 arguments (y, x)");

        double y = ValueToNumber(args[0]);
        double x = ValueToNumber(args[1]);
        return HavelValue(std::atan2(y, x));
      });

  // Hyperbolic functions
  math["sinh"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("sinh() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::sinh(value));
      });

  math["cosh"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("cosh() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::cosh(value));
      });

  math["tanh"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("tanh() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::tanh(value));
      });

  // Exponential and logarithmic functions
  math["exp"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("exp() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::exp(value));
      });

  math["log"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("log() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value <= 0.0)
          return HavelRuntimeError("log() argument must be positive");

        return HavelValue(std::log(value));
      });

  math["log10"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("log10() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value <= 0.0)
          return HavelRuntimeError("log10() argument must be positive");

        return HavelValue(std::log10(value));
      });

  math["log2"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("log2() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value <= 0.0)
          return HavelRuntimeError("log2() argument must be positive");

        return HavelValue(std::log2(value));
      });

  math["sqrt"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("sqrt() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value < 0.0)
          return HavelRuntimeError("sqrt() argument must be non-negative");

        return HavelValue(std::sqrt(value));
      });

  math["cbrt"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("cbrt() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(std::cbrt(value));
      });

  // Power functions
  math["pow"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 2)
          return HavelRuntimeError(
              "pow() requires 2 arguments (base, exponent)");

        double base = ValueToNumber(args[0]);
        double exponent = ValueToNumber(args[1]);
        return HavelValue(std::pow(base, exponent));
      });

  // Constants
  math["PI"] = HavelValue(M_PI);
  math["E"] = HavelValue(M_E);
  math["TAU"] = HavelValue(2 * M_PI);
  math["SQRT2"] = HavelValue(M_SQRT2);
  math["SQRT1_2"] = HavelValue(M_SQRT1_2);
  math["LN2"] = HavelValue(M_LN2);
  math["LN10"] = HavelValue(M_LN10);
  math["LOG2E"] = HavelValue(M_LOG2E);
  math["LOG10E"] = HavelValue(M_LOG10E);

  // Utility functions
  math["min"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("min() requires at least 2 arguments");

        double result = ValueToNumber(args[0]);
        for (size_t i = 1; i < args.size(); i++) {
          result = std::min(result, ValueToNumber(args[i]));
        }
        return HavelValue(result);
      });

  math["max"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("max() requires at least 2 arguments");

        double result = ValueToNumber(args[0]);
        for (size_t i = 1; i < args.size(); i++) {
          result = std::max(result, ValueToNumber(args[i]));
        }
        return HavelValue(result);
      });

  math["clamp"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 3)
          return HavelRuntimeError(
              "clamp() requires 3 arguments (value, min, max)");

        double value = ValueToNumber(args[0]);
        double minVal = ValueToNumber(args[1]);
        double maxVal = ValueToNumber(args[2]);

        if (minVal > maxVal)
          return HavelRuntimeError(
              "clamp() min must be less than or equal to max");

        return HavelValue(std::clamp(value, minVal, maxVal));
      });

  math["lerp"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 3)
          return HavelRuntimeError(
              "lerp() requires 3 arguments (start, end, t)");

        double start = ValueToNumber(args[0]);
        double end = ValueToNumber(args[1]);
        double t = ValueToNumber(args[2]);

        return HavelValue(start + t * (end - start));
      });

  // Random functions
  math["random"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        static std::random_device rd;
        static std::mt19937 gen(rd());

        if (args.empty()) {
          // random() -> [0, 1)
          std::uniform_real_distribution<double> dis(0.0, 1.0);
          return HavelValue(dis(gen));
        } else if (args.size() == 1) {
          // random(max) -> [0, max)
          double maxVal = ValueToNumber(args[0]);
          if (maxVal <= 0)
            return HavelRuntimeError("random(max) requires max > 0");
          std::uniform_real_distribution<double> dis(0.0, maxVal);
          return HavelValue(dis(gen));
        } else if (args.size() == 2) {
          // random(min, max) -> [min, max)
          double minVal = ValueToNumber(args[0]);
          double maxVal = ValueToNumber(args[1]);
          if (minVal >= maxVal)
            return HavelRuntimeError("random(min, max) requires min < max");
          std::uniform_real_distribution<double> dis(minVal, maxVal);
          return HavelValue(dis(gen));
        } else {
          return HavelRuntimeError("random() accepts 0, 1, or 2 arguments");
        }
      });

  math["randint"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        static std::random_device rd;
        static std::mt19937 gen(rd());

        if (args.size() == 1) {
          // randint(max) -> [0, max]
          int maxVal = static_cast<int>(ValueToNumber(args[0]));
          if (maxVal < 0)
            return HavelRuntimeError("randint(max) requires max >= 0");
          std::uniform_int_distribution<int> dis(0, maxVal);
          return HavelValue(static_cast<double>(dis(gen)));
        } else if (args.size() == 2) {
          // randint(min, max) -> [min, max]
          int minVal = static_cast<int>(ValueToNumber(args[0]));
          int maxVal = static_cast<int>(ValueToNumber(args[1]));
          if (minVal > maxVal)
            return HavelRuntimeError("randint(min, max) requires min <= max");
          std::uniform_int_distribution<int> dis(minVal, maxVal);
          return HavelValue(static_cast<double>(dis(gen)));
        } else {
          return HavelRuntimeError("randint() requires 1 or 2 arguments");
        }
      });

  // Angle conversion functions
  math["deg2rad"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("deg2rad() requires 1 argument");

        double degrees = ValueToNumber(args[0]);
        return HavelValue(degrees * M_PI / 180.0);
      });

  math["rad2deg"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("rad2deg() requires 1 argument");

        double radians = ValueToNumber(args[0]);
        return HavelValue(radians * 180.0 / M_PI);
      });

  // Special functions
  math["sign"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("sign() requires 1 argument");

        double value = ValueToNumber(args[0]);
        if (value > 0)
          return HavelValue(1.0);
        if (value < 0)
          return HavelValue(-1.0);
        return HavelValue(0.0);
      });

  math["fract"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1)
          return HavelRuntimeError("fract() requires 1 argument");

        double value = ValueToNumber(args[0]);
        return HavelValue(value - std::floor(value));
      });

  math["mod"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 2)
          return HavelRuntimeError("mod() requires 2 arguments (x, y)");

        double x = ValueToNumber(args[0]);
        double y = ValueToNumber(args[1]);

        if (y == 0.0)
          return HavelRuntimeError("mod() divisor cannot be zero");

        return HavelValue(std::fmod(x, y));
      });

  // Distance and geometry functions
  math["distance"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 4)
          return HavelRuntimeError(
              "distance() requires 4 arguments (x1, y1, x2, y2)");

        double x1 = ValueToNumber(args[0]);
        double y1 = ValueToNumber(args[1]);
        double x2 = ValueToNumber(args[2]);
        double y2 = ValueToNumber(args[3]);

        double dx = x2 - x1;
        double dy = y2 - y1;
        return HavelValue(std::sqrt(dx * dx + dy * dy));
      });

  math["hypot"] =
      BuiltinFunction([](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2)
          return HavelRuntimeError("hypot() requires at least 2 arguments");

        double sumSquares = 0.0;
        for (const auto &arg : args) {
          double value = ValueToNumber(arg);
          sumSquares += value * value;
        }

        return HavelValue(std::sqrt(sumSquares));
      });

  environment->Define("math", HavelValue(mathObj));
}

void Interpreter::InitializeBrightnessBuiltins() {
  // Brightness get
  environment->Define(
      "brightnessManager.getBrightness",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty()) {
              return HavelValue(brightnessManager->getBrightness());
            }
            int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
            return HavelValue(brightnessManager->getBrightness(monitorIndex));
          }));

  environment->Define(
      "brightnessManager.getTemperature",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty()) {
              return HavelValue(
                  static_cast<double>(brightnessManager->getTemperature()));
            }
            int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
            return HavelValue(static_cast<double>(
                brightnessManager->getTemperature(monitorIndex)));
          }));

  // Brightness set
  environment->Define(
      "brightnessManager.setBrightness",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError(
                  "setBrightness() requires value or (monitorIndex, value)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              double brightness = ValueToNumber(args[1]);
              brightnessManager->setBrightness(monitorIndex, brightness);
              return HavelValue(nullptr);
            }
            double brightness = ValueToNumber(args[0]);
            brightnessManager->setBrightness(brightness);
            return HavelValue(nullptr);
          }));

  // Brightness increase
  environment->Define(
      "brightnessManager.increaseBrightness",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              double step = ValueToNumber(args[1]);
              brightnessManager->increaseBrightness(monitorIndex, step);
              return HavelValue(nullptr);
            }
            double step = args.empty() ? 0.1 : ValueToNumber(args[0]);
            brightnessManager->increaseBrightness(step);
            return HavelValue(nullptr);
          }));

  // Brightness decrease
  environment->Define(
      "brightnessManager.decreaseBrightness",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              double step = ValueToNumber(args[1]);
              brightnessManager->decreaseBrightness(monitorIndex, step);
              return HavelValue(nullptr);
            }
            double step = args.empty() ? 0.1 : ValueToNumber(args[0]);
            brightnessManager->decreaseBrightness(step);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.setTemperature",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError("setTemperature() requires kelvin "
                                       "or (monitorIndex, kelvin)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              int kelvin = static_cast<int>(ValueToNumber(args[1]));
              brightnessManager->setTemperature(monitorIndex, kelvin);
              return HavelValue(nullptr);
            }
            int kelvin = static_cast<int>(ValueToNumber(args[0]));
            brightnessManager->setTemperature(kelvin);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.getShadowLift",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty()) {
              return HavelValue(brightnessManager->getShadowLift());
            }
            int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
            return HavelValue(brightnessManager->getShadowLift(monitorIndex));
          }));

  environment->Define(
      "brightnessManager.setShadowLift",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError(
                  "setShadowLift() requires lift or (monitorIndex, lift)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              double lift = ValueToNumber(args[1]);
              brightnessManager->setShadowLift(monitorIndex, lift);
              return HavelValue(nullptr);
            }
            double lift = ValueToNumber(args[0]);
            brightnessManager->setShadowLift(lift);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.decreaseGamma",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError("decreaseGamma() requires amount "
                                       "or (monitorIndex, amount)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              int amount = static_cast<int>(ValueToNumber(args[1]));
              brightnessManager->decreaseGamma(monitorIndex, amount);
              return HavelValue(nullptr);
            }
            int amount = static_cast<int>(ValueToNumber(args[0]));
            brightnessManager->decreaseGamma(amount);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.increaseGamma",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError("increaseGamma() requires amount "
                                       "or (monitorIndex, amount)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              int amount = static_cast<int>(ValueToNumber(args[1]));
              brightnessManager->increaseGamma(monitorIndex, amount);
              return HavelValue(nullptr);
            }
            int amount = static_cast<int>(ValueToNumber(args[0]));
            brightnessManager->increaseGamma(amount);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.setGammaRGB",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.size() < 3)
              return HavelRuntimeError("setGammaRGB() requires (r, g, b) "
                                       "or (monitorIndex, r, g, b)");
            if (args.size() >= 4) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              double r = ValueToNumber(args[1]);
              double g = ValueToNumber(args[2]);
              double b = ValueToNumber(args[3]);
              brightnessManager->setGammaRGB(monitorIndex, r, g, b);
              return HavelValue(nullptr);
            }
            double r = ValueToNumber(args[0]);
            double g = ValueToNumber(args[1]);
            double b = ValueToNumber(args[2]);
            brightnessManager->setGammaRGB(r, g, b);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.increaseTemperature",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError("increaseTemperature() requires amount "
                                       "or (monitorIndex, amount)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              int amount = static_cast<int>(ValueToNumber(args[1]));
              brightnessManager->increaseTemperature(monitorIndex, amount);
              return HavelValue(nullptr);
            }
            int amount = static_cast<int>(ValueToNumber(args[0]));
            brightnessManager->increaseTemperature(amount);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "brightnessManager.decreaseTemperature",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!brightnessManager)
              return HavelRuntimeError("BrightnessManager not available");
            if (args.empty())
              return HavelRuntimeError("decreaseTemperature() requires amount "
                                       "or (monitorIndex, amount)");
            if (args.size() >= 2) {
              int monitorIndex = static_cast<int>(ValueToNumber(args[0]));
              int amount = static_cast<int>(ValueToNumber(args[1]));
              brightnessManager->decreaseTemperature(monitorIndex, amount);
              return HavelValue(nullptr);
            }
            int amount = static_cast<int>(ValueToNumber(args[0]));
            brightnessManager->decreaseTemperature(amount);
            return HavelValue(nullptr);
          }));

  // Expose as module object: brightnessManager
  auto brightnessManagerObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("brightnessManager.getBrightness"))
    (*brightnessManagerObj)["getBrightness"] = *v;
  if (auto v = environment->Get("brightnessManager.getTemperature"))
    (*brightnessManagerObj)["getTemperature"] = *v;
  if (auto v = environment->Get("brightnessManager.setBrightness"))
    (*brightnessManagerObj)["setBrightness"] = *v;
  if (auto v = environment->Get("brightnessManager.increaseBrightness"))
    (*brightnessManagerObj)["increaseBrightness"] = *v;
  if (auto v = environment->Get("brightnessManager.decreaseBrightness"))
    (*brightnessManagerObj)["decreaseBrightness"] = *v;
  if (auto v = environment->Get("brightnessManager.setTemperature"))
    (*brightnessManagerObj)["setTemperature"] = *v;
  if (auto v = environment->Get("brightnessManager.increaseTemperature"))
    (*brightnessManagerObj)["increaseTemperature"] = *v;
  if (auto v = environment->Get("brightnessManager.decreaseTemperature"))
    (*brightnessManagerObj)["decreaseTemperature"] = *v;
  if (auto v = environment->Get("brightnessManager.getShadowLift"))
    (*brightnessManagerObj)["getShadowLift"] = *v;
  if (auto v = environment->Get("brightnessManager.setShadowLift"))
    (*brightnessManagerObj)["setShadowLift"] = *v;
  if (auto v = environment->Get("brightnessManager.decreaseGamma"))
    (*brightnessManagerObj)["decreaseGamma"] = *v;
  if (auto v = environment->Get("brightnessManager.increaseGamma"))
    (*brightnessManagerObj)["increaseGamma"] = *v;
  if (auto v = environment->Get("brightnessManager.setGammaRGB"))
    (*brightnessManagerObj)["setGammaRGB"] = *v;
  environment->Define("brightnessManager", HavelValue(brightnessManagerObj));

  // Expose as module object: launcher
  auto launcher =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("run"))
    (*launcher)["run"] = *v;
  if (auto v = environment->Get("runAsync"))
    (*launcher)["runAsync"] = *v;
  if (auto v = environment->Get("runDetached"))
    (*launcher)["runDetached"] = *v;
  if (auto v = environment->Get("terminal"))
    (*launcher)["terminal"] = *v;
  environment->Define("launcher", HavelValue(launcher));
}

// KeyTap constructor implementation
KeyTap *Interpreter::createKeyTap(
    const std::string &keyName, std::function<void()> onTap,
    std::variant<std::string, std::function<bool()>> tapCondition,
    std::variant<std::string, std::function<bool()>> comboCondition,
    std::function<void()> onCombo, bool grabDown, bool grabUp) {
  auto keyTap =
      std::make_unique<KeyTap>(io, *hotkeyManager, keyName, onTap, tapCondition,
                               comboCondition, onCombo, grabDown, grabUp);

  KeyTap *rawPtr = keyTap.get();

  keyTaps.push_back(std::move(keyTap));
  rawPtr->setup();

  return rawPtr;
}

void Interpreter::InitializeAudioBuiltins() {
  // Volume control
  environment->Define(
      "audio.getVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            return HavelValue(audioManager->getVolume());
          }));

  environment->Define(
      "audio.setVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            if (args.empty())
              return HavelRuntimeError(
                  "setVolume() requires volume value (0.0-1.0)");
            double volume = ValueToNumber(args[0]);
            audioManager->setVolume(volume);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "audio.increaseVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
            audioManager->increaseVolume(amount);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "audio.decreaseVolume",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            double amount = args.empty() ? 0.05 : ValueToNumber(args[0]);
            audioManager->decreaseVolume(amount);
            return HavelValue(nullptr);
          }));

  // Mute control
  environment->Define(
      "audio.toggleMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            audioManager->toggleMute();
            return HavelValue(nullptr);
          }));

  environment->Define(
      "audio.setMute",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            if (args.empty())
              return HavelRuntimeError("setMute() requires boolean value");
            bool muted = ValueToBool(args[0]);
            audioManager->setMute(muted);
            return HavelValue(nullptr);
          }));

  environment->Define(
      "audio.isMuted",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (!audioManager)
              return HavelRuntimeError("AudioManager not available");
            return HavelValue(audioManager->isMuted());
          }));
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

        if (!std::holds_alternative<HavelArray>(args[1])) {
          return HavelRuntimeError(
              "gui.showMenu() requires an array of options");
        }
        auto optionsVec = std::get<HavelArray>(args[1]);
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
          return HavelRuntimeError("altTab.setThumbnailSize() requires (width, height)");
        int width = static_cast<int>(ValueToNumber(args[0]));
        int height = static_cast<int>(ValueToNumber(args[1]));
        if (altTabWindow) {
          altTabWindow->setThumbnailSize(width, height);
        }
        return HavelValue(nullptr);
      }));

  // Note: getWindows() is private in AltTabWindow

  // Create altTab module object
  auto altTabMod = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("altTab.show")) (*altTabMod)["show"] = *v;
  if (auto v = environment->Get("altTab.hide")) (*altTabMod)["hide"] = *v;
  if (auto v = environment->Get("altTab.next")) (*altTabMod)["next"] = *v;
  if (auto v = environment->Get("altTab.prev")) (*altTabMod)["prev"] = *v;
  if (auto v = environment->Get("altTab.select")) (*altTabMod)["select"] = *v;
  if (auto v = environment->Get("altTab.refresh")) (*altTabMod)["refresh"] = *v;
  if (auto v = environment->Get("altTab.setThumbnailSize")) (*altTabMod)["setThumbnailSize"] = *v;
  environment->Define("altTab", HavelValue(altTabMod));

  // === MAPMANAGER MODULE ===
  // MapManagerWindow for managing input mappings
  static std::unique_ptr<MapManagerWindow> mapManagerWindow;
  
  environment->Define(
      "mapmanager.show",
      BuiltinFunction([this](const std::vector<HavelValue> &args) -> HavelResult {
        (void)args;
        if (!mapManagerWindow) {
          // Note: MapManagerWindow requires MapManager instance
          // For now, create with null - full implementation needs MapManager integration
          mapManagerWindow = std::make_unique<MapManagerWindow>(nullptr, nullptr);
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

  // Note: MapManagerWindow methods (refresh, saveAll, loadAll, etc.) are private Qt slots

  // Create mapmanager module object
  auto mapManagerMod = std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("mapmanager.show")) (*mapManagerMod)["show"] = *v;
  if (auto v = environment->Get("mapmanager.hide")) (*mapManagerMod)["hide"] = *v;
  environment->Define("mapmanager", HavelValue(mapManagerMod));
}

void Interpreter::InitializeScreenshotBuiltins() {
  environment->Define(
      "screenshot.full",
      BuiltinFunction([this](const std::vector<HavelValue> &) -> HavelResult {
        if (!screenshotManager) {
          return HavelRuntimeError("ScreenshotManager not available");
        }
        QMetaObject::invokeMethod(screenshotManager, "takeScreenshot",
                                  Qt::QueuedConnection);
        return HavelValue(nullptr);
      }));

  environment->Define(
      "screenshot.region",
      BuiltinFunction([this](const std::vector<HavelValue> &) -> HavelResult {
        if (!screenshotManager) {
          return HavelRuntimeError("ScreenshotManager not available");
        }
        QMetaObject::invokeMethod(screenshotManager, "takeRegionScreenshot",
                                  Qt::QueuedConnection);
        return HavelValue(nullptr);
      }));

  environment->Define(
      "screenshot.monitor",
      BuiltinFunction([this](const std::vector<HavelValue> &) -> HavelResult {
        if (!screenshotManager) {
          return HavelRuntimeError("ScreenshotManager not available");
        }
        QMetaObject::invokeMethod(screenshotManager,
                                  "takeScreenshotOfCurrentMonitor",
                                  Qt::QueuedConnection);
        return HavelValue(nullptr);
      }));

  // Module object
  auto screenshotMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();
  if (auto v = environment->Get("screenshot.full"))
    (*screenshotMod)["full"] = *v;
  if (auto v = environment->Get("screenshot.region"))
    (*screenshotMod)["region"] = *v;
  if (auto v = environment->Get("screenshot.monitor"))
    (*screenshotMod)["monitor"] = *v;
  environment->Define("screenshot", HavelValue(screenshotMod));
}

void Interpreter::InitializeAutomationBuiltins() {
  // === AUTOMATION MODULE ===
  auto automationMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // AutoClicker functions
  (*automationMod)["startAutoClicker"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string button = args.empty() ? "left" : ValueToString(args[0]);
            int intervalMs = args.size() > 1
                                 ? static_cast<int>(ValueToNumber(args[1]))
                                 : 100;
            auto task =
                app->automationManager->createAutoClicker(button, intervalMs);
            task->start();
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["stopAutoClicker"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName =
                args.empty() ? "AutoClicker" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->stop();
              return HavelValue(true);
            }
          }
        }
        return HavelValue(false);
      });

  // AutoKeyPresser functions
  (*automationMod)["startAutoKeyPresser"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string key = args.empty() ? "space" : ValueToString(args[0]);
            int intervalMs = args.size() > 1
                                 ? static_cast<int>(ValueToNumber(args[1]))
                                 : 100;
            auto task =
                app->automationManager->createAutoKeyPresser(key, intervalMs);
            task->start();
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["stopAutoKeyPresser"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName =
                args.empty() ? "AutoKeyPresser" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->stop();
              return HavelValue(true);
            }
          }
        }
        return HavelValue(false);
      });

  // AutoRunner functions
  (*automationMod)["startAutoRunner"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string direction = args.empty() ? "w" : ValueToString(args[0]);
            int intervalMs =
                args.size() > 1 ? static_cast<int>(ValueToNumber(args[1])) : 50;
            auto task =
                app->automationManager->createAutoRunner(direction, intervalMs);
            task->start();
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["stopAutoRunner"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName =
                args.empty() ? "AutoRunner" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->stop();
              return HavelValue(true);
            }
          }
        }
        return HavelValue(false);
      });

  // ChainedTask functions
  (*automationMod)["createChainedTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "createChainedTask requires name and actions array");
        }

        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName = ValueToString(args[0]);

            if (!std::holds_alternative<HavelArray>(args[1])) {
              return HavelRuntimeError(
                  "Second argument must be an array of actions");
            }

            auto actionsArray = std::get<HavelArray>(args[1]);
            std::vector<havel::automation::AutomationManager::TimedAction>
                timedActions;

            for (const auto &action : *actionsArray) {
              if (std::holds_alternative<HavelArray>(action)) {
                auto actionArray = std::get<HavelArray>(action);
                if (actionArray->size() >= 2) {
                  std::string actionStr = ValueToString((*actionArray)[0]);
                  int delayMs =
                      static_cast<int>(ValueToNumber((*actionArray)[1]));

                  // Create action function based on string
                  auto actionFunc = [actionStr]() {
                    // Parse and execute the action string
                    if (actionStr == "click") {
                      if (auto app = HavelApp::instance) {
                        if (app->io) {
                          app->io->MouseClick(1); // Left click
                        }
                      }
                    } else if (actionStr == "rightClick") {
                      if (auto app = HavelApp::instance) {
                        if (app->io) {
                          app->io->MouseClick(3); // Right click
                        }
                      }
                    } else if (actionStr.find("key:") == 0) {
                      std::string key =
                          actionStr.substr(4); // Remove "key:" prefix
                      IO::PressKey(key, true); // Key down
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds(10));
                      IO::PressKey(key, false); // Key up
                    } else if (actionStr.find("wait:") == 0) {
                      int waitMs = std::stoi(
                          actionStr.substr(5)); // Remove "wait:" prefix
                      std::this_thread::sleep_for(
                          std::chrono::milliseconds(waitMs));
                    }
                  };

                  timedActions.push_back(
                      havel::automation::AutomationManager::makeTimedAction(
                          actionFunc, delayMs));
                }
              }
            }

            bool loop = args.size() > 2 ? ValueToBool(args[2]) : false;
            auto task = app->automationManager->createChainedTask(
                taskName, timedActions, loop);
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["startChainedTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName =
                args.empty() ? "ChainedTask" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->start();
              return HavelValue(true);
            }
          }
        }
        return HavelValue(false);
      });

  (*automationMod)["stopChainedTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName =
                args.empty() ? "ChainedTask" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->stop();
              return HavelValue(true);
            }
          }
        }
        return HavelValue(false);
      });

  // Task management functions
  (*automationMod)["getTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName = args.empty() ? "" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              auto taskObj = std::make_shared<
                  std::unordered_map<std::string, HavelValue>>();
              (*taskObj)["name"] = HavelValue(task->getName());
              (*taskObj)["running"] = HavelValue(task->isRunning());
              return HavelValue(taskObj);
            }
          }
        }
        return HavelValue(nullptr);
      });

  (*automationMod)["hasTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName = args.empty() ? "" : ValueToString(args[0]);
            bool hasTask = app->automationManager->hasTask(taskName);
            return HavelValue(hasTask);
          }
        }
        return HavelValue(false);
      });

  (*automationMod)["removeTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName = args.empty() ? "" : ValueToString(args[0]);
            app->automationManager->removeTask(taskName);
            return HavelValue(true);
          }
        }
        return HavelValue(false);
      });

  (*automationMod)["stopAllTasks"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            app->automationManager->stopAll();
            return HavelValue(true);
          }
        }
        return HavelValue(false);
      });

  (*automationMod)["toggleTask"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string taskName = args.empty() ? "" : ValueToString(args[0]);
            auto task = app->automationManager->getTask(taskName);
            if (task) {
              task->toggle();
              return HavelValue(task->isRunning());
            }
          }
        }
        return HavelValue(false);
      });

  // Convenience functions for common automation patterns
  (*automationMod)["autoClick"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string button = args.empty() ? "left" : ValueToString(args[0]);
            int intervalMs = args.size() > 1
                                 ? static_cast<int>(ValueToNumber(args[1]))
                                 : 100;
            auto task =
                app->automationManager->createAutoClicker(button, intervalMs);
            task->toggle(); // Start the task
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["autoPress"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string key = args.empty() ? "space" : ValueToString(args[0]);
            int intervalMs = args.size() > 1
                                 ? static_cast<int>(ValueToNumber(args[1]))
                                 : 100;
            auto task =
                app->automationManager->createAutoKeyPresser(key, intervalMs);
            task->toggle(); // Start the task
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  (*automationMod)["autoRun"] = BuiltinFunction(
      [this](const std::vector<HavelValue> &args) -> HavelResult {
        if (auto app = HavelApp::instance) {
          if (app->automationManager) {
            std::string direction = args.empty() ? "w" : ValueToString(args[0]);
            int intervalMs =
                args.size() > 1 ? static_cast<int>(ValueToNumber(args[1])) : 50;
            auto task =
                app->automationManager->createAutoRunner(direction, intervalMs);
            task->toggle(); // Start the task
            return HavelValue(task->getName());
          }
        }
        return HavelRuntimeError("AutomationManager not available");
      });

  environment->Define("automation", HavelValue(automationMod));
}

// Async builtins
void Interpreter::InitializeAsyncBuiltins() {
  // spawn function
  environment->Define(
      "spawn",
      BuiltinFunction([this](
                          const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() != 1) {
          return HavelRuntimeError("spawn requires 1 argument");
        }

        if (!std::holds_alternative<std::shared_ptr<HavelFunction>>(args[0])) {
          return HavelRuntimeError("spawn requires a function");
        }

        auto func = std::get<std::shared_ptr<HavelFunction>>(args[0]);
        std::string taskId = "task_" + std::to_string(std::rand());

        AsyncScheduler::getInstance().spawn(
            [this, func]() -> HavelResult {
              return Evaluate(*func->declaration->body);
            },
            taskId);

        return taskId;
      }));

  // await function
  environment->Define(
      "await",
      BuiltinFunction(
          [this](const std::vector<HavelValue> &args) -> HavelResult {
            if (args.size() != 1) {
              return HavelRuntimeError("await requires 1 argument");
            }

            if (!std::holds_alternative<std::string>(args[0])) {
              return HavelRuntimeError("await requires a task ID string");
            }

            std::string taskId = std::get<std::string>(args[0]);
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
        if (!std::holds_alternative<std::shared_ptr<HavelFunction>>(args[1])) {
          return HavelRuntimeError(
              "setTimeout second argument must be a function");
        }

        auto callback = std::get<std::shared_ptr<HavelFunction>>(args[1]);

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
        if (!std::holds_alternative<std::shared_ptr<HavelFunction>>(args[1])) {
          return HavelRuntimeError(
              "setInterval second argument must be a function");
        }

        auto callback = std::get<std::shared_ptr<HavelFunction>>(args[1]);

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
              help << "  - debug       : Debugging utilities\n\n";
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
    auto conditionFunc = [this, condExpr = node.condition.get()]() -> bool {
      auto result = Evaluate(*condExpr);
      if (isError(result)) {
        // Log error but return false to prevent the hotkey from
        // triggering std::cerr << "Conditional hotkey condition
        // evaluation failed: "
        //           << std::get<HavelRuntimeError>(result).what() <<
        //           std::endl;
        return false;
      }
      return ValueToBool(unwrap(result));
    };

    // Create the action callback
    auto actionFunc = [this, action = node.binding->action.get()]() {
      if (action) {
        auto result = Evaluate(*action);
        if (isError(result)) {
          std::cerr << "Conditional hotkey action evaluation failed: "
                    << std::get<HavelRuntimeError>(result).what() << std::endl;
        }
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
        auto conditionFunc = [this, condExpr = node.condition.get()]() -> bool {
          auto result = Evaluate(*condExpr);
          if (isError(result)) {
            // Log error but return false to prevent the hotkey from
            // triggering
            return false;
          }
          return ValueToBool(unwrap(result));
        };

        // Create the action callback
        auto actionFunc = [this, action = hotkeyBinding->action.get()]() {
          if (action) {
            auto result = Evaluate(*action);
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
        auto combinedConditionFunc =
            [this, outerCond = node.condition.get(),
             innerCond = conditionalHotkey->condition.get()]() -> bool {
          // Evaluate outer condition
          auto outerResult = Evaluate(*outerCond);
          if (isError(outerResult) || !ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = Evaluate(*innerCond);
          if (isError(innerResult) || !ValueToBool(unwrap(innerResult))) {
            return false;
          }

          return true;
        };

        // Create the action callback from the inner binding
        auto actionFunc =
            [this, action = conditionalHotkey->binding->action.get()]() {
              if (action) {
                auto result = Evaluate(*action);
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
        auto combinedConditionFunc =
            [this, outerCond = node.condition.get(),
             innerCond = whenBlock->condition.get()]() -> bool {
          // Evaluate outer condition
          auto outerResult = Evaluate(*outerCond);
          if (isError(outerResult) || !ValueToBool(unwrap(outerResult))) {
            return false;
          }

          // Evaluate inner condition
          auto innerResult = Evaluate(*innerCond);
          if (isError(innerResult) || !ValueToBool(unwrap(innerResult))) {
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
                [this, action = innerHotkeyBinding->action.get()]() {
                  if (action) {
                    auto result = Evaluate(*action);
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

} // namespace havel
