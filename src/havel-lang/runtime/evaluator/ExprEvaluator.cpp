/*
 * ExprEvaluator.cpp
 *
 * Expression evaluation for Havel interpreter.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "ExprEvaluator.hpp"
#include "../Interpreter.hpp"
#include "core/ConfigManager.hpp"
#include "process/Launcher.hpp"

namespace havel {

void ExprEvaluator::visitBinaryExpression(const ast::BinaryExpression& node) {
    auto leftRes = Evaluate(*node.left);
    if (isError(leftRes)) {
        interpreter->lastResult = leftRes;
        return;
    }
    auto rightRes = Evaluate(*node.right);
    if (isError(rightRes)) {
        interpreter->lastResult = rightRes;
        return;
    }

    HavelValue left = unwrap(leftRes);
    HavelValue right = unwrap(rightRes);

    switch (node.operator_) {
    case ast::BinaryOperator::Add:
        if (left.isString() || right.isString()) {
            std::string result = ValueToString(left) + ValueToString(right);
            interpreter->lastResult = HavelValue(result);
        } else {
            interpreter->lastResult = ValueToNumber(left) + ValueToNumber(right);
        }
        break;
    case ast::BinaryOperator::Sub:
        interpreter->lastResult = ValueToNumber(left) - ValueToNumber(right);
        break;
    case ast::BinaryOperator::Mul:
        interpreter->lastResult = ValueToNumber(left) * ValueToNumber(right);
        break;
    case ast::BinaryOperator::Div:
        if (ValueToNumber(right) == 0.0) {
            interpreter->lastResult = HavelRuntimeError("Division by zero", node.line, node.column);
            return;
        }
        interpreter->lastResult = ValueToNumber(left) / ValueToNumber(right);
        break;
    case ast::BinaryOperator::Mod:
        if (ValueToNumber(right) == 0.0) {
            interpreter->lastResult = HavelRuntimeError("Modulo by zero", node.line, node.column);
            return;
        }
        interpreter->lastResult = static_cast<int>(ValueToNumber(left)) %
                     static_cast<int>(ValueToNumber(right));
        break;
    case ast::BinaryOperator::Equal:
        interpreter->lastResult = HavelValue(ValueToString(left) == ValueToString(right));
        break;
    case ast::BinaryOperator::NotEqual:
        interpreter->lastResult = HavelValue(ValueToString(left) != ValueToString(right));
        break;
    case ast::BinaryOperator::Less:
        interpreter->lastResult = HavelValue(ValueToNumber(left) < ValueToNumber(right));
        break;
    case ast::BinaryOperator::Greater:
        interpreter->lastResult = HavelValue(ValueToNumber(left) > ValueToNumber(right));
        break;
    case ast::BinaryOperator::LessEqual:
        interpreter->lastResult = HavelValue(ValueToNumber(left) <= ValueToNumber(right));
        break;
    case ast::BinaryOperator::GreaterEqual:
        interpreter->lastResult = HavelValue(ValueToNumber(left) >= ValueToNumber(right));
        break;
    case ast::BinaryOperator::And:
        interpreter->lastResult = HavelValue(ValueToBool(left) && ValueToBool(right));
        break;
    case ast::BinaryOperator::Or:
        interpreter->lastResult = HavelValue(ValueToBool(left) || ValueToBool(right));
        break;
    case ast::BinaryOperator::ConfigAppend:
        if (right.isObject()) {
            auto rightObj = right.asObject();
            if (rightObj && left.isString()) {
                interpreter->environment->Define("__config_value__", left);
                interpreter->lastResult = left;
            } else {
                interpreter->lastResult = HavelRuntimeError("Config append requires string value");
            }
        } else if (right.isString()) {
            std::string configKey = right.asString();
            if (left.isString()) {
                auto& config = Configs::Get();
                config.Set(configKey, left.asString(), false);
                interpreter->lastResult = left;
            } else {
                interpreter->lastResult = HavelRuntimeError("Config value must be a string");
            }
        } else {
            interpreter->lastResult = HavelRuntimeError("Config append requires string or object on right side");
        }
        break;
    default:
        interpreter->lastResult = HavelRuntimeError("Unsupported binary operator");
    }
}

void ExprEvaluator::visitUnaryExpression(const ast::UnaryExpression& node) {
    auto operandRes = Evaluate(*node.operand);
    if (isError(operandRes)) {
        interpreter->lastResult = operandRes;
        return;
    }
    HavelValue operand = unwrap(operandRes);

    switch (node.operator_) {
    case ast::UnaryExpression::UnaryOperator::Not:
        interpreter->lastResult = !ValueToBool(operand);
        break;
    case ast::UnaryExpression::UnaryOperator::Minus:
        interpreter->lastResult = -ValueToNumber(operand);
        break;
    case ast::UnaryExpression::UnaryOperator::Plus:
        interpreter->lastResult = ValueToNumber(operand);
        break;
    default:
        interpreter->lastResult = HavelRuntimeError("Unsupported unary operator");
    }
}

void ExprEvaluator::visitUpdateExpression(const ast::UpdateExpression& node) {
    // Determine the variable or property being updated
    if (auto *id = dynamic_cast<const ast::Identifier *>(node.argument.get())) {
        auto currentValOpt = interpreter->environment->Get(id->symbol);
        if (!currentValOpt) {
            interpreter->lastResult = HavelRuntimeError("Undefined variable: " + id->symbol, node.line, node.column);
            return;
        }

        double currentNum = ValueToNumber(*currentValOpt);
        double newNum =
            (node.operator_ == ast::UpdateExpression::Operator::Increment)
                ? currentNum + 1.0
                : currentNum - 1.0;

        interpreter->environment->Assign(id->symbol, newNum);

        interpreter->lastResult = node.isPrefix ? newNum : currentNum;
        return;
    }

    // Member expression (obj.prop++)
    if (auto *member =
            dynamic_cast<const ast::MemberExpression *>(node.argument.get())) {
        auto objectResult = Evaluate(*member->object);
        if (isError(objectResult)) {
            interpreter->lastResult = objectResult;
            return;
        }
        HavelValue objectValue = unwrap(objectResult);

        auto *propId =
            dynamic_cast<const ast::Identifier *>(member->property.get());
        if (!propId) {
            interpreter->lastResult =
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
                interpreter->lastResult = node.isPrefix ? newNum : currentNum;
                return;
            }
        }
        interpreter->lastResult = HavelRuntimeError("Cannot update property of non-object");
        return;
    }

    interpreter->lastResult = HavelRuntimeError("Invalid update target");
}

void ExprEvaluator::visitMemberExpression(const ast::MemberExpression& node) {
    auto objectResult = Evaluate(*node.object);
    if (isError(objectResult)) {
        interpreter->lastResult = objectResult;
        return;
    }
    HavelValue objectValue = unwrap(objectResult);

    auto *propId = dynamic_cast<const ast::Identifier *>(node.property.get());
    if (!propId) {
        interpreter->lastResult = HavelRuntimeError("Invalid property access");
        return;
    }
    std::string propName = propId->symbol;

    // Objects: o.b
    if (auto *objPtr = objectValue.get_if<HavelObject>()) {
        if (*objPtr) {
            auto it = (*objPtr)->find(propName);
            if (it != (*objPtr)->end()) {
                interpreter->lastResult = it->second;
                return;
            }
        }
        interpreter->lastResult = HavelValue(nullptr);
        return;
    }

    // Arrays: special properties like length and methods
    if (auto *arrPtr = objectValue.get_if<HavelArray>()) {
        if (propName == "length") {
            interpreter->lastResult = static_cast<double>((*arrPtr) ? (*arrPtr)->size() : 0);
            return;
        }
        // Check for array methods (push, pop, etc.)
        std::optional<HavelValue> methodValOpt = interpreter->environment->Get(propName);
        if (methodValOpt && methodValOpt->is<BuiltinFunction>()) {
            auto builtin = methodValOpt->get<BuiltinFunction>();
            // Create a bound function that captures the array as first argument
            auto array = objectValue;  // Capture the array value
            interpreter->lastResult = HavelValue(BuiltinFunction([array, builtin](const std::vector<HavelValue> &args) -> HavelResult {
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
        std::optional<HavelValue> methodValOpt = interpreter->environment->Get(propName);
        if (methodValOpt && methodValOpt->is<BuiltinFunction>()) {
            auto builtin = methodValOpt->get<BuiltinFunction>();
            // Create a bound function that captures the string as first argument
            auto str = objectValue;  // Capture the string value
            interpreter->lastResult = HavelValue(BuiltinFunction([str, builtin](const std::vector<HavelValue> &args) -> HavelResult {
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
                interpreter->lastResult = it->second;
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
                interpreter->lastResult = HavelValue(BuiltinFunction([this, instance, methodPtr](const std::vector<HavelValue> &args) -> HavelResult {
                    // Check argument count
                    if (args.size() != methodPtr->parameters.size()) {
                        return HavelRuntimeError("Method expects " + std::to_string(methodPtr->parameters.size()) + " args but got " + std::to_string(args.size()));
                    }
                    // Create method environment with 'this' bound
                    auto methodEnv = std::make_shared<Environment>(interpreter->environment);
                    methodEnv->Define("this", instance);
                    // Bind parameters
                    for (size_t i = 0; i < methodPtr->parameters.size() && i < args.size(); ++i) {
                        methodEnv->Define(methodPtr->parameters[i]->paramName->symbol, args[i]);
                    }
                    // Execute method body
                    auto originalEnv = interpreter->environment;
                    interpreter->environment = methodEnv;
                    auto res = Evaluate(*methodPtr->body);
                    interpreter->environment = originalEnv;
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
                    interpreter->lastResult = HavelValue(BuiltinFunction([this, instance, traitMethod](const std::vector<HavelValue> &args) -> HavelResult {
                        // Create method environment with 'this' bound
                        auto methodEnv = std::make_shared<Environment>(interpreter->environment);
                        methodEnv->Define("this", instance);
                        auto originalEnv = interpreter->environment;
                        interpreter->environment = methodEnv;
                        // Call the trait method with instance as first arg
                        std::vector<HavelValue> callArgs = args;
                        auto res = traitMethod(callArgs);
                        interpreter->environment = originalEnv;
                        return res;
                    }));
                    return;
                }
            }
        }
        interpreter->lastResult = HavelValue(nullptr);
        return;
    }

    interpreter->lastResult = HavelRuntimeError("Member access not supported for this type");
}

void ExprEvaluator::visitLambdaExpression(const ast::LambdaExpression& node) {
    // Capture current environment (closure)
    auto closureEnv = interpreter->environment;

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
                auto defaultRes = interpreter->Evaluate(*paramInfos[i].defaultValue);
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

        auto originalEnv = interpreter->environment;
        interpreter->environment = funcEnv;
        auto res = Evaluate(*bodyPtr);
        interpreter->environment = originalEnv;
        if (std::holds_alternative<ReturnValue>(res)) {
            auto ret = std::get<ReturnValue>(res);
            return ret.value ? *ret.value : HavelValue();
        }
        return res;
    };
    interpreter->lastResult = HavelValue(lambda);
}

void ExprEvaluator::visitSetExpression(const ast::SetExpression& node) {
    auto set = std::make_shared<std::vector<HavelValue>>();

    for (const auto &element : node.elements) {
        auto result = Evaluate(*element);
        if (isError(result)) {
            interpreter->lastResult = result;
            return;
        }
        set->push_back(unwrap(result));
    }

    interpreter->lastResult = HavelValue(HavelSet(set));
}

void ExprEvaluator::visitPipelineExpression(const ast::PipelineExpression& node) {
    if (node.stages.empty()) {
        interpreter->lastResult = HavelValue(nullptr);
        return;
    }

    HavelResult currentResult = Evaluate(*node.stages[0]);
    if (isError(currentResult)) {
        interpreter->lastResult = currentResult;
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
                    interpreter->lastResult = argRes;
                    return;
                }
                args.push_back(unwrap(argRes));
            }
        }

        auto calleeRes = Evaluate(*calleeExpr);
        if (isError(calleeRes)) {
            interpreter->lastResult = calleeRes;
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
                interpreter->lastResult = HavelRuntimeError(
                    "Mismatched argument count for function in pipeline");
                return;
            }
            auto funcEnv = std::make_shared<Environment>(func->closure);
            for (size_t j = 0; j < args.size(); ++j) {
                funcEnv->Define(func->declaration->parameters[j]->paramName->symbol, args[j]);
            }
            auto originalEnv = interpreter->environment;
            interpreter->environment = funcEnv;
            auto bodyResult = Evaluate(*func->declaration->body);
            interpreter->environment = originalEnv;
            if (std::holds_alternative<ReturnValue>(bodyResult)) {
                auto ret = std::get<ReturnValue>(bodyResult);
                currentResult = ret.value ? *ret.value : HavelValue();
            } else {
                currentResult = HavelValue(nullptr);
            }
        } else {
            interpreter->lastResult = HavelRuntimeError("Pipeline stage must be a function");
            return;
        }

        if (isError(currentResult)) {
            interpreter->lastResult = currentResult;
            return;
        }
    }

    interpreter->lastResult = unwrap(currentResult);
}

void ExprEvaluator::visitStringLiteral(const ast::StringLiteral& node) {
    interpreter->lastResult = HavelValue(node.value);
}

void ExprEvaluator::visitInterpolatedStringExpression(const ast::InterpolatedStringExpression& node) {
    std::string result;

    for (const auto &segment : node.segments) {
        if (segment.isString) {
            result += segment.stringValue;
        } else {
            // Evaluate the expression
            auto exprResult = Evaluate(*segment.expression);
            if (isError(exprResult)) {
                interpreter->lastResult = exprResult;
                return;
            }
            // Convert result to string and append
            result += ValueToString(unwrap(exprResult));
        }
    }

    interpreter->lastResult = HavelValue(result);
}

void ExprEvaluator::visitHotkeyLiteral(const ast::HotkeyLiteral& node) {
    interpreter->lastResult = HavelValue(node.combination);
}

void ExprEvaluator::visitAsyncExpression(const ast::AsyncExpression& node) {
    // For now, just execute the expression synchronously
    // TODO: Implement proper async/await support
    if (node.body) {
        node.body->accept(*interpreter);
    } else {
        interpreter->lastResult = nullptr;
    }
}

void ExprEvaluator::visitAwaitExpression(const ast::AwaitExpression& node) {
    // For now, just return the awaited value
    // TODO: Implement proper async/await support
    if (node.argument) {
        node.argument->accept(*interpreter);
    } else {
        interpreter->lastResult = nullptr;
    }
}

void ExprEvaluator::visitArrayLiteral(const ast::ArrayLiteral& node) {
    auto array = std::make_shared<std::vector<HavelValue>>();

    for (const auto &element : node.elements) {
        auto result = Evaluate(*element);
        if (isError(result)) {
            interpreter->lastResult = result;
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

    interpreter->lastResult = HavelValue(array);
}

void ExprEvaluator::visitSpreadExpression(const ast::SpreadExpression& node) {
    // Spread expressions are handled by their container (array/object literal)
    // This is just a fallback - should not normally be reached
    auto result = Evaluate(*node.target);
    if (isError(result)) {
        interpreter->lastResult = result;
        return;
    }
    interpreter->lastResult = unwrap(result);
}

void ExprEvaluator::visitObjectLiteral(const ast::ObjectLiteral& node) {
    auto object = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    for (const auto &[key, valueExpr] : node.pairs) {
        auto result = Evaluate(*valueExpr);
        if (isError(result)) {
            interpreter->lastResult = result;
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

    interpreter->lastResult = HavelValue(object);
}

void ExprEvaluator::visitIndexExpression(const ast::IndexExpression& node) {
    auto objectResult = Evaluate(*node.object);
    if (isError(objectResult)) {
        interpreter->lastResult = objectResult;
        return;
    }

    auto indexResult = Evaluate(*node.index);
    if (isError(indexResult)) {
        interpreter->lastResult = indexResult;
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
            interpreter->lastResult = HavelRuntimeError("Array index out of bounds: " +
                                     std::to_string(index));
            return;
        }

        interpreter->lastResult = (**arrayPtr)[index];
        return;
    }

    // Handle object property access
    if (objectValue.isObject()) {
        auto objectPtr = objectValue.get_if<HavelObject>();
        std::string key = ValueToString(indexValue);

        if (objectPtr && *objectPtr) {
            auto it = (*objectPtr)->find(key);
            if (it != (*objectPtr)->end()) {
                interpreter->lastResult = it->second;
            } else {
                interpreter->lastResult = nullptr; // Return null for missing properties
            }
        } else {
            interpreter->lastResult = nullptr;
        }
        return;
    }

    interpreter->lastResult = HavelRuntimeError("Cannot index non-array/non-object value");
}

void ExprEvaluator::visitTernaryExpression(const ast::TernaryExpression& node) {
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
        interpreter->lastResult = conditionResult;
        return;
    }

    if (ValueToBool(unwrap(conditionResult))) {
        interpreter->lastResult = Evaluate(*node.trueValue);
    } else {
        interpreter->lastResult = Evaluate(*node.falseValue);
    }
}

void ExprEvaluator::visitRangeExpression(const ast::RangeExpression& node) {
    auto startResult = Evaluate(*node.start);
    if (isError(startResult)) {
        interpreter->lastResult = startResult;
        return;
    }

    auto endResult = Evaluate(*node.end);
    if (isError(endResult)) {
        interpreter->lastResult = endResult;
        return;
    }

    int start = static_cast<int>(ValueToNumber(unwrap(startResult)));
    int end = static_cast<int>(ValueToNumber(unwrap(endResult)));

    // Handle optional step value
    int step = 1;
    if (node.step) {
        auto stepResult = Evaluate(*node.step);
        if (isError(stepResult)) {
            interpreter->lastResult = stepResult;
            return;
        }
        step = static_cast<int>(ValueToNumber(unwrap(stepResult)));
        if (step == 0) {
            interpreter->lastResult = HavelRuntimeError("Range step cannot be zero", node.line, node.column);
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

    interpreter->lastResult = rangeArray;
}

void ExprEvaluator::visitAssignmentExpression(const ast::AssignmentExpression& node) {
    // Evaluate the right-hand side
    auto valueResult = Evaluate(*node.value);
    if (isError(valueResult)) {
        interpreter->lastResult = valueResult;
        return;
    }
    HavelValue value = unwrap(valueResult);

    auto applyCompound = [this](const std::string &op, const HavelValue &lhs,
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
        auto current = interpreter->environment->Get(identifier->symbol);
        if (!current.has_value()) {
            interpreter->lastResult =
                HavelRuntimeError("Undefined variable: " + identifier->symbol, identifier->line, identifier->column);
            return;
        }

        // Check if this is a const variable
        if (interpreter->environment->IsConst(identifier->symbol)) {
            interpreter->lastResult = HavelRuntimeError("Cannot assign to const variable: " + identifier->symbol,
                                     identifier->line, identifier->column);
            return;
        }

        HavelValue newValue = applyCompound(op, *current, value);
        if (!interpreter->environment->Assign(identifier->symbol, newValue)) {
            interpreter->lastResult =
                HavelRuntimeError("Undefined variable: " + identifier->symbol, identifier->line, identifier->column);
            return;
        }
        value = newValue;
    } else if (auto *index = dynamic_cast<const ast::IndexExpression *>(
                   node.target.get())) {
        // Array/object index assignment (array[0] = value)
        auto objectResult = Evaluate(*index->object);
        if (isError(objectResult)) {
            interpreter->lastResult = objectResult;
            return;
        }

        auto indexResult = Evaluate(*index->index);
        if (isError(indexResult)) {
            interpreter->lastResult = indexResult;
            return;
        }

        HavelValue objectValue = unwrap(objectResult);
        HavelValue indexValue = unwrap(indexResult);

        if (auto *arrayPtr = objectValue.get_if<HavelArray>()) {
            int idx = static_cast<int>(ValueToNumber(indexValue));
            if (!*arrayPtr || idx < 0 ||
                idx >= static_cast<int>((*arrayPtr)->size())) {
                interpreter->lastResult = HavelRuntimeError("Array index out of bounds");
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
            interpreter->lastResult = HavelRuntimeError("Cannot index non-array/non-object value");
            return;
        }
    } else if (auto *member = dynamic_cast<const ast::MemberExpression *>(
                   node.target.get())) {
        // Member expression assignment (obj.prop = value)
        auto objectResult = Evaluate(*member->object);
        if (isError(objectResult)) {
            interpreter->lastResult = objectResult;
            return;
        }
        HavelValue objectValue = unwrap(objectResult);

        // Get property name
        std::string propName;
        if (auto *propId = dynamic_cast<const ast::Identifier *>(member->property.get())) {
            propName = propId->symbol;
        } else {
            interpreter->lastResult = HavelRuntimeError("Invalid property name", node.line, node.column);
            return;
        }

        // Check if object supports property assignment
        if (auto *objectPtr = objectValue.get_if<HavelObject>()) {
            if (!*objectPtr) {
                *objectPtr = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            }
            (**objectPtr)[propName] = value;
        } else {
            interpreter->lastResult = HavelRuntimeError("Cannot set property on non-object value", node.line, node.column);
            return;
        }
    } else {
        interpreter->lastResult = HavelRuntimeError("Invalid assignment target", node.line, node.column);
        return;
    }

    interpreter->lastResult = value; // Assignment expressions return the assigned value
}

void ExprEvaluator::visitTryExpression(const ast::TryExpression& node) {
    // Execute try body
    auto tryResult = Evaluate(*node.tryBody);

    // Check if try body threw an error
    if (auto *err = std::get_if<HavelRuntimeError>(&tryResult)) {
        // If we have a catch block, execute it
        if (node.catchBody) {
            // Create new environment for catch block with catch variable
            auto catchEnv = std::make_shared<Environment>(interpreter->environment);
            auto originalEnv = interpreter->environment;
            interpreter->environment = catchEnv;

            // If catch variable is specified, create it in catch scope
            if (node.catchVariable) {
                // Store the error message as string in the catch variable
                std::string errorMsg = err->what();
                interpreter->environment->Define(node.catchVariable->symbol, HavelValue(errorMsg));
            }

            auto catchResult = Evaluate(*node.catchBody);

            // Restore original environment
            interpreter->environment = originalEnv;

            // Execute finally block if present (always runs, even after catch)
            if (node.finallyBlock) {
                auto finallyResult = Evaluate(*node.finallyBlock);
                if (isError(finallyResult)) {
                    interpreter->lastResult = finallyResult;
                    return;
                }
            }

            if (isError(catchResult)) {
                interpreter->lastResult = catchResult;
                return;
            }
            interpreter->lastResult = catchResult;
            return;
        }

        // No catch handler, execute finally if present
        if (node.finallyBlock) {
            auto finallyResult = Evaluate(*node.finallyBlock);
            if (isError(finallyResult)) {
                interpreter->lastResult = finallyResult;
                return;
            }
        }

        // Re-throw the original error
        interpreter->lastResult = *err;
        return;
    }

    // Try body succeeded, execute finally if present
    if (node.finallyBlock) {
        auto finallyResult = Evaluate(*node.finallyBlock);
        if (isError(finallyResult)) {
            interpreter->lastResult = finallyResult;
            return;
        }
    }

    // Return try body result
    interpreter->lastResult = tryResult;
}

void ExprEvaluator::visitBlockExpression(const ast::BlockExpression& node) {
    auto blockEnv = std::make_shared<Environment>(interpreter->environment);
    auto originalEnv = interpreter->environment;
    interpreter->environment = blockEnv;

    // Execute statements
    for (const auto &stmt : node.body) {
        auto result = Evaluate(*stmt);
        if (isError(result) ||
            std::holds_alternative<ReturnValue>(result) ||
            std::holds_alternative<BreakValue>(result) ||
            std::holds_alternative<ContinueValue>(result)) {
            interpreter->environment = originalEnv;
            interpreter->lastResult = result;
            return;
        }
    }

    // Evaluate final expression (the value of the block)
    if (node.value) {
        auto valueResult = Evaluate(*node.value);
        interpreter->lastResult = valueResult;
    } else {
        interpreter->lastResult = HavelValue(nullptr);
    }

    interpreter->environment = originalEnv;
}

void ExprEvaluator::visitIfExpression(const ast::IfExpression& node) {
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
        interpreter->lastResult = conditionResult;
        return;
    }

    bool conditionMet = ValueToBool(unwrap(conditionResult));

    if (conditionMet) {
        interpreter->lastResult = Evaluate(*node.thenBranch);
    } else if (node.elseBranch) {
        interpreter->lastResult = Evaluate(*node.elseBranch);
    } else {
        interpreter->lastResult = HavelValue(nullptr);  // No else branch, return null
    }
}

void ExprEvaluator::visitBacktickExpression(const ast::BacktickExpression& node) {
    // Execute shell command and capture output using Launcher
    ProcessResult result = havel::Launcher::runShell(node.command);

    // Return structured ProcessResult as an object
    auto resultObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*resultObj)["stdout"] = HavelValue(result.stdout);
    (*resultObj)["stderr"] = HavelValue(result.stderr);
    (*resultObj)["exitCode"] = HavelValue(static_cast<double>(result.exitCode));
    (*resultObj)["success"] = HavelValue(result.success);
    (*resultObj)["error"] = HavelValue(result.error);

    interpreter->lastResult = HavelValue(resultObj);
}

void ExprEvaluator::visitShellCommandExpression(const ast::ShellCommandExpression& node) {
    // Evaluate command expression to support variables, arrays, etc.
    auto cmdResult = Evaluate(*node.commandExpr);
    if (isError(cmdResult)) {
        interpreter->lastResult = cmdResult;
        return;
    }

    HavelValue cmdValue = unwrap(cmdResult);
    ProcessResult result;

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
            interpreter->lastResult = HavelRuntimeError("Shell command array is empty");
            return;
        }
    } else {
        // String mode: execute through shell
        std::string command = ValueToString(cmdValue);
        result = havel::Launcher::runShell(command);
    }

    // Return stdout (capture mode is implicit for expressions)
    interpreter->lastResult = HavelValue(result.stdout);
}

void ExprEvaluator::visitExpressionStatement(const ast::ExpressionStatement& node) {
    Evaluate(*node.expression);
}

// Helper method implementations
HavelResult ExprEvaluator::Evaluate(const ast::ASTNode& node) {
    return interpreter->Evaluate(node);
}

bool ExprEvaluator::isError(const HavelResult& result) {
    return std::holds_alternative<HavelRuntimeError>(result);
}

HavelValue ExprEvaluator::unwrap(const HavelResult& result) {
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

std::string ExprEvaluator::ValueToString(const HavelValue& value) {
    return interpreter->ValueToString(value);
}

double ExprEvaluator::ValueToNumber(const HavelValue& value) {
    return interpreter->ValueToNumber(value);
}

bool ExprEvaluator::ValueToBool(const HavelValue& value) {
    return interpreter->ValueToBool(value);
}

void ExprEvaluator::visitCallExpression(const ast::CallExpression& node) {
    auto calleeRes = Evaluate(*node.callee);
    if (isError(calleeRes)) {
        interpreter->lastResult = calleeRes;
        return;
    }
    HavelValue callee = unwrap(calleeRes);

    std::vector<HavelValue> args;
    for (const auto& arg : node.args) {
        auto argRes = Evaluate(*arg);
        if (isError(argRes)) {
            interpreter->lastResult = argRes;
            return;
        }

        if (dynamic_cast<const ast::SpreadExpression*>(arg.get())) {
            auto value = unwrap(argRes);
            if (auto* arrPtr = value.get_if<HavelArray>()) {
                if (*arrPtr) {
                    for (const auto& item : **arrPtr) {
                        args.push_back(item);
                    }
                }
            } else if (auto* objPtr = value.get_if<HavelObject>()) {
                if (*objPtr) {
                    std::vector<std::pair<std::string, HavelValue>> sortedPairs((*objPtr)->begin(), (*objPtr)->end());
                    std::sort(sortedPairs.begin(), sortedPairs.end(),
                        [](const auto& a, const auto& b) { return a.first < b.first; });
                    for (const auto& pair : sortedPairs) {
                        args.push_back(pair.second);
                    }
                }
            } else {
                args.push_back(value);
            }
        } else {
            args.push_back(unwrap(argRes));
        }
    }

    if (auto* builtin = callee.get_if<BuiltinFunction>()) {
        interpreter->lastResult = (*builtin)(args);
    } else if (auto* userFunc = callee.get_if<std::shared_ptr<HavelFunction>>()) {
        auto& func = *userFunc;
        if (args.size() != func->declaration->parameters.size()) {
            interpreter->lastResult = HavelRuntimeError("Mismatched argument count for function " +
                                 func->declaration->name->symbol);
            return;
        }

        auto funcEnv = std::make_shared<Environment>(func->closure);
        for (size_t i = 0; i < args.size(); ++i) {
            funcEnv->Define(func->declaration->parameters[i]->paramName->symbol, args[i]);
        }

        auto originalEnv = interpreter->environment;
        interpreter->environment = funcEnv;
        auto bodyResult = Evaluate(*func->declaration->body);
        interpreter->environment = originalEnv;

        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            auto ret = std::get<ReturnValue>(bodyResult);
            interpreter->lastResult = ret.value ? *ret.value : HavelValue();
        } else {
            interpreter->lastResult = nullptr;
        }
    } else if (auto* objPtr = callee.get_if<HavelObject>()) {
        if (*objPtr) {
            auto it = (*objPtr)->find("__call__");
            if (it != (*objPtr)->end() && it->second.is<BuiltinFunction>()) {
                auto callFunc = it->second.get<BuiltinFunction>();
                interpreter->lastResult = callFunc(args);
                return;
            }
        }
        interpreter->lastResult = HavelRuntimeError("Attempted to call a non-callable value: " +
                             ValueToString(callee), node.line, node.column);
    } else {
        interpreter->lastResult = HavelRuntimeError("Attempted to call a non-callable value: " +
                             ValueToString(callee), node.line, node.column);
    }
}

} // namespace havel
