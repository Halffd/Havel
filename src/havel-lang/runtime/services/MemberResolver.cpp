/*
 * MemberResolver.cpp
 *
 * Member access resolution for Havel runtime.
 * Extracted from ExprEvaluator to reduce complexity.
 */
#include "../Interpreter.hpp"  // Include first for full type definitions
#include "MemberResolver.hpp"
#include "../evaluator/ExprEvaluator.hpp"
#include "../Environment.hpp"

namespace havel {

MemberResolver::MemberResolver(Interpreter* interp) : interpreter(interp) {}

void MemberResolver::resolveMember(const ast::MemberExpression& node) {
    // Check environment first
    if (!interpreter->getEnvironment()) {
        interpreter->setLastResult(HavelRuntimeError("Environment not available"));
        return;
    }

    // Evaluate object
    ExprEvaluator eval(interpreter);
    auto objectResult = eval.Evaluate(*node.object);
    if (eval.isError(objectResult)) {
        interpreter->setLastResult(objectResult);
        return;
    }
    HavelValue objectValue = eval.unwrap(objectResult);

    // Check for null object
    if (objectValue.isNull()) {
        interpreter->setLastResult(HavelRuntimeError("Cannot access member on null value"));
        return;
    }

    // Get property name
    auto *propId = dynamic_cast<const ast::Identifier *>(node.property.get());
    if (!propId) {
        interpreter->setLastResult(HavelRuntimeError("Invalid property access"));
        return;
    }
    std::string propName = propId->symbol;

    // Objects: o.b
    if (auto *objPtr = objectValue.get_if<HavelObject>()) {
        if (*objPtr) {
            auto it = (*objPtr)->find(propName);
            if (it != (*objPtr)->end()) {
                interpreter->setLastResult(it->second);
                return;
            }
        }
        interpreter->setLastResult(HavelValue(nullptr));
        return;
    }

    // Arrays: special properties like length and methods
    if (auto *arrPtr = objectValue.get_if<HavelArray>()) {
        if (propName == "length") {
            interpreter->setLastResult(static_cast<double>((*arrPtr) ? (*arrPtr)->size() : 0));
            return;
        }
        // Check for array methods (push, pop, etc.)
        if (!interpreter->getEnvironment()) {
            interpreter->setLastResult(HavelRuntimeError("Environment not available for method lookup"));
            return;
        }
        std::optional<HavelValue> methodValOpt = interpreter->getEnvironment()->Get(propName);
        if (methodValOpt.has_value() && methodValOpt->is<BuiltinFunction>()) {
            auto builtin = methodValOpt->get<BuiltinFunction>();
            // Create a bound function that captures the array as first argument
            auto array = objectValue;  // Capture the array value
            interpreter->setLastResult(HavelValue(BuiltinFunction([array, builtin](const std::vector<HavelValue> &args) -> HavelResult {
                std::vector<HavelValue> boundArgs;
                boundArgs.push_back(array);
                boundArgs.insert(boundArgs.end(), args.begin(), args.end());
                return builtin(boundArgs);
            })));
            return;
        }
    }

    // Strings: methods like lower, upper, replace, etc.
    if (auto *strPtr = objectValue.get_if<std::string>()) {
        if (!interpreter->getEnvironment()) {
            interpreter->setLastResult(HavelRuntimeError("Environment not available for method lookup"));
            return;
        }
        std::optional<HavelValue> methodValOpt = interpreter->getEnvironment()->Get(propName);
        if (methodValOpt.has_value() && methodValOpt->is<BuiltinFunction>()) {
            auto builtin = methodValOpt->get<BuiltinFunction>();
            // Create a bound function that captures the string as first argument
            auto str = objectValue;  // Capture the string value
            interpreter->setLastResult(HavelValue(BuiltinFunction([str, builtin](const std::vector<HavelValue> &args) -> HavelResult {
                std::vector<HavelValue> boundArgs;
                boundArgs.push_back(str);
                boundArgs.insert(boundArgs.end(), args.begin(), args.end());
                return builtin(boundArgs);
            })));
            return;
        }
    }

    // Struct instances: field access and method binding
    if (auto *structPtr = objectValue.get_if<HavelStructInstance>()) {
        // First check fields
        if (structPtr && structPtr->fields) {
            auto it = structPtr->fields->find(propName);
            if (it != structPtr->fields->end()) {
                interpreter->setLastResult(it->second);
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
                interpreter->setLastResult(HavelValue(BuiltinFunction([this, instance, methodPtr, &eval](const std::vector<HavelValue> &args) -> HavelResult {
                    // Check argument count
                    if (args.size() != methodPtr->parameters.size()) {
                        return HavelRuntimeError("Method expects " + std::to_string(methodPtr->parameters.size()) + " args but got " + std::to_string(args.size()));
                    }
                    // Create method environment with 'this' bound
                    auto methodEnv = std::make_shared<Environment>(interpreter->getEnvironment());
                    methodEnv->Define("this", instance);
                    // Bind parameters
                    for (size_t i = 0; i < methodPtr->parameters.size() && i < args.size(); ++i) {
                        methodEnv->Define(methodPtr->parameters[i]->paramName->symbol, args[i]);
                    }
                    // Execute method body
                    auto originalEnv = interpreter->getEnvironment();
                    interpreter->getEnvironment() = methodEnv;
                    auto res = eval.Evaluate(*methodPtr->body);
                    interpreter->getEnvironment() = originalEnv;
                    if (std::holds_alternative<ReturnValue>(res)) {
                        auto ret = std::get<ReturnValue>(res);
                        return ret.value ? *ret.value : HavelValue();
                    }
                    return res;
                })));
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
                    interpreter->setLastResult(HavelValue(BuiltinFunction([this, instance, traitMethod](const std::vector<HavelValue> &args) -> HavelResult {
                        // Create method environment with 'this' bound
                        auto methodEnv = std::make_shared<Environment>(interpreter->getEnvironment());
                        methodEnv->Define("this", instance);
                        auto originalEnv = interpreter->getEnvironment();
                        interpreter->getEnvironment() = methodEnv;
                        // Call the trait method with instance as first arg
                        std::vector<HavelValue> callArgs = args;
                        auto res = traitMethod(callArgs);
                        interpreter->getEnvironment() = originalEnv;
                        return res;
                    })));
                    return;
                }
            }
        }
        interpreter->setLastResult(HavelValue(nullptr));
        return;
    }

    // Default: member not found
    interpreter->setLastResult(HavelValue(nullptr));
}

} // namespace havel
