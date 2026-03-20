/*
 * CallDispatcher.cpp
 *
 * Function call dispatch for Havel runtime.
 * Extracted from ExprEvaluator to reduce complexity.
 */
#include "CallDispatcher.hpp"
#include "../Environment.hpp"
#include "../Interpreter.hpp" // Include first for full type definitions
#include "../evaluator/ExprEvaluator.hpp"

namespace havel {

// Private method declarations
static HavelResult callBuiltin(BuiltinFunction builtin,
                               const std::vector<HavelValue> &args);
static HavelResult callUserFunction(Interpreter *interp,
                                    const std::shared_ptr<HavelFunction> &func,
                                    const std::vector<HavelValue> &args);
static HavelResult callObject(const HavelObject &obj,
                              const std::vector<HavelValue> &args);
static void handleSpreadArgument(const HavelValue &value,
                                 std::vector<HavelValue> &args);

CallDispatcher::CallDispatcher(Interpreter *interp) : interpreter(interp) {}

void CallDispatcher::dispatchCall(const ast::CallExpression &node) {
  // Check environment first
  if (!interpreter->environment) {
    interpreter->lastResult = HavelRuntimeError("Environment not available");
    return;
  }

  // Evaluate callee
  ExprEvaluator eval(interpreter);
  auto calleeRes = eval.Evaluate(*node.callee);
  if (eval.isError(calleeRes)) {
    interpreter->lastResult = calleeRes;
    return;
  }
  HavelValue callee = eval.unwrap(calleeRes);

  // Check for null callee
  if (callee.isNull()) {
    interpreter->lastResult = HavelRuntimeError(
        "Attempted to call a null value", node.line, node.column);
    return;
  }

  // Evaluate arguments with spread handling
  std::vector<HavelValue> args;
  for (const auto &arg : node.args) {
    auto argRes = eval.Evaluate(*arg);
    if (eval.isError(argRes)) {
      interpreter->lastResult = argRes;
      return;
    }

    if (dynamic_cast<const ast::SpreadExpression *>(arg.get())) {
      handleSpreadArgument(eval.unwrap(argRes), args);
    } else {
      args.push_back(eval.unwrap(argRes));
    }
  }

  // Dispatch based on callee type
  if (auto *builtin = callee.get_if<BuiltinFunction>()) {
    interpreter->lastResult = callBuiltin(*builtin, args);
  } else if (auto *userFunc = callee.get_if<std::shared_ptr<HavelFunction>>()) {
    interpreter->lastResult = callUserFunction(interpreter, *userFunc, args);
  } else if (auto *objPtr = callee.get_if<HavelObject>()) {
    interpreter->lastResult = callObject(*objPtr, args);
  } else {
    interpreter->lastResult =
        HavelRuntimeError("Attempted to call a non-callable value: " +
                              interpreter->ValueToString(callee),
                          node.line, node.column);
  }
}

// Private method implementations
static HavelResult callBuiltin(BuiltinFunction builtin,
                               const std::vector<HavelValue> &args) {
  return (builtin)(args);
}

static HavelResult callUserFunction(Interpreter *interp,
                                    const std::shared_ptr<HavelFunction> &func,
                                    const std::vector<HavelValue> &args) {
  if (!func || !func->declaration) {
    return HavelRuntimeError("Invalid function");
  }

  // Check argument count
  if (args.size() != func->declaration->parameters.size()) {
    return HavelRuntimeError("Mismatched argument count for function " +
                             func->declaration->name->symbol);
  }

  // Create function environment with closure
  auto funcEnv = std::make_shared<Environment>(func->closure);

  // Bind arguments to parameters
  for (size_t i = 0; i < args.size(); ++i) {
    funcEnv->Define(func->declaration->parameters[i]->paramName->symbol,
                    args[i]);
  }

  // Execute function body
  auto originalEnv = interp->getEnvironment();
  interp->getEnvironment() = funcEnv;

  ExprEvaluator eval(interp);
  auto bodyResult = eval.Evaluate(*func->declaration->body);

  interp->getEnvironment() = originalEnv;

  // Handle return value
  if (std::holds_alternative<ReturnValue>(bodyResult)) {
    auto ret = std::get<ReturnValue>(bodyResult);
    return ret.value ? *ret.value : HavelValue();
  } else if (eval.isError(bodyResult)) {
    return bodyResult;
  } else {
    // IMPLICIT RETURN: use the body result (last expression value)
    return eval.unwrap(bodyResult);
  }
}

static HavelResult callObject(const HavelObject &obj,
                              const std::vector<HavelValue> &args) {
  if (!obj) {
    return HavelRuntimeError("Null object");
  }

  // Check for __call__ metamethod
  auto it = obj->find("__call__");
  if (it != obj->end() && it->second.is<BuiltinFunction>()) {
    auto callFunc = it->second.get<BuiltinFunction>();
    return callFunc(args);
  }

  return HavelRuntimeError("Object is not callable");
}

static void handleSpreadArgument(const HavelValue &value,
                                 std::vector<HavelValue> &args) {
  // Spread arrays - flatten elements into args
  if (auto *arrPtr = value.get_if<HavelArray>()) {
    if (*arrPtr) {
      for (const auto &item : **arrPtr) {
        args.push_back(item);
      }
    }
  }
  // Spread objects - use values sorted by key
  else if (auto *objPtr = value.get_if<HavelObject>()) {
    if (*objPtr) {
      std::vector<std::pair<std::string, HavelValue>> sortedPairs(
          (*objPtr)->begin(), (*objPtr)->end());
      std::sort(sortedPairs.begin(), sortedPairs.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
      for (const auto &pair : sortedPairs) {
        args.push_back(pair.second);
      }
    }
  }
  // Non-array/object spread - add as single arg
  else {
    args.push_back(value);
  }
}

} // namespace havel
