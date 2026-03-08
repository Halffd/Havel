/*
 * StatementEvaluator.cpp
 *
 * Statement evaluation for Havel interpreter.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "StatementEvaluator.hpp"
#include "../Interpreter.hpp"
#include <regex>
#include <thread>

namespace havel {

void StatementEvaluator::visitProgram(const ast::Program& node) {
    HavelValue lastValue = nullptr;
    for (const auto& stmt : node.body) {
        auto result = Evaluate(*stmt);
        if (isError(result)) {
            interpreter->lastResult = result;
            return;
        }
        if (std::holds_alternative<ReturnValue>(result)) {
            auto ret = std::get<ReturnValue>(result);
            interpreter->lastResult = ret.value ? *ret.value : HavelValue();
            return;
        }
        lastValue = unwrap(result);
    }
    interpreter->lastResult = lastValue;
}

void StatementEvaluator::visitLetDeclaration(const ast::LetDeclaration& node) {
    HavelValue value = nullptr;
    if (node.value) {
        auto result = Evaluate(*node.value);
        if (isError(result)) {
            interpreter->lastResult = result;
            return;
        }
        value = unwrap(result);
    }

    // Handle destructuring patterns
    if (auto *ident = dynamic_cast<const ast::Identifier *>(node.pattern.get())) {
        // Simple variable declaration: let x = value or const x = value
        interpreter->environment->Define(ident->symbol, value, node.isConst);
        interpreter->lastResult = value; // Set result for potential chaining
    } else if (auto *arrayPattern =
                 dynamic_cast<const ast::ArrayPattern *>(node.pattern.get())) {
        // Array destructuring: let [a, b] = arr
        if (!node.value) {
            interpreter->lastResult =
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
                        interpreter->environment->Define(ident->symbol, element, node.isConst);
                    }
                    // TODO: Handle nested patterns
                }
            }
        } else {
            interpreter->lastResult = HavelRuntimeError("Cannot destructure non-array value");
            return;
        }
    } else if (auto *objectPattern =
                 dynamic_cast<const ast::ObjectPattern *>(node.pattern.get())) {
        // Object destructuring: let {x, y} = obj
        if (!node.value) {
            interpreter->lastResult =
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
                            interpreter->environment->Define(ident->symbol, it->second);
                        }
                        // TODO: Handle renamed patterns and nested patterns
                    }
                }
            }
        } else {
            interpreter->lastResult = HavelRuntimeError("Cannot destructure non-object value");
            return;
        }
    }

    interpreter->lastResult = value;
}

void StatementEvaluator::visitFunctionDeclaration(const ast::FunctionDeclaration& node) {
    // Create function with current environment
    auto func = std::make_shared<HavelFunction>(
        HavelFunction{interpreter->environment, // Capture closure
                      &node});
    // Define the function name in the current environment FIRST
    interpreter->environment->Define(node.name->symbol, func);
    // Then update the function's closure to include itself for recursion
    func->closure = interpreter->environment;
}

void StatementEvaluator::visitFunctionParameter(const ast::FunctionParameter& node) {
    // Parameters are metadata for function construction.
    // Default values are evaluated at CALL time, not definition time.
    // This allows: let a = 5; let f = fn(x = a) {...}; a = 10; f() => x = 10
    interpreter->lastResult = HavelValue(nullptr);
}

void StatementEvaluator::visitReturnStatement(const ast::ReturnStatement& node) {
    HavelValue value = nullptr;
    if (node.argument) {
        auto result = Evaluate(*node.argument);
        if (isError(result)) {
            interpreter->lastResult = result;
            return;
        }
        value = unwrap(result);
    }
    interpreter->lastResult = ReturnValue{std::make_shared<HavelValue>(value)};
}

void StatementEvaluator::visitIfStatement(const ast::IfStatement& node) {
    auto conditionResult = Evaluate(*node.condition);
    if (isError(conditionResult)) {
        interpreter->lastResult = conditionResult;
        return;
    }

    if (ValueToBool(unwrap(conditionResult))) {
        interpreter->lastResult = Evaluate(*node.consequence);
    } else if (node.alternative) {
        interpreter->lastResult = Evaluate(*node.alternative);
    } else {
        interpreter->lastResult = nullptr;
    }
}

void StatementEvaluator::visitBlockStatement(const ast::BlockStatement& node) {
    auto blockEnv = std::make_shared<Environment>(interpreter->environment);
    auto originalEnv = interpreter->environment;
    interpreter->environment = blockEnv;

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

    interpreter->environment = originalEnv;
    interpreter->lastResult = blockResult;
}

void StatementEvaluator::visitHotkeyBinding(const ast::HotkeyBinding& node) {
    interpreter->visitHotkeyBinding(node);
}

void StatementEvaluator::visitSleepStatement(const ast::SleepStatement& node) {
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
    interpreter->lastResult = HavelValue(nullptr);
}

void StatementEvaluator::visitRepeatStatement(const ast::RepeatStatement& node) {
    // Evaluate count expression
    auto countResult = Evaluate(*node.countExpr);
    if (isError(countResult)) {
        interpreter->lastResult = countResult;
        return;
    }
    int count = static_cast<int>(ValueToNumber(unwrap(countResult)));

    // Execute body 'count' times
    for (int i = 0; i < count; i++) {
        if (node.body) {
            Evaluate(*node.body);
        }
    }
    interpreter->lastResult = HavelValue(nullptr);
}

void StatementEvaluator::visitShellCommandStatement(const ast::ShellCommandStatement& node) {
    interpreter->visitShellCommandStatement(node);
}

void StatementEvaluator::visitForStatement(const ast::ForStatement& node) {
    // Evaluate the iterable expression
    auto iterableResult = Evaluate(*node.iterable);
    if (isError(iterableResult)) {
        interpreter->lastResult = iterableResult;
        return;
    }

    // Unwrap the result to get the HavelValue
    auto iterableValue = unwrap(iterableResult);

    // Create a new environment for the for loop
    auto loopEnv = std::make_shared<Environment>(interpreter->environment);
    auto originalEnv = interpreter->environment;
    interpreter->environment = loopEnv;

    // Handle different types of iterable
    if (iterableValue.is<HavelArray>()) {
        // Array iteration
        auto array = iterableValue.get<HavelArray>();
        if (!array) {
            interpreter->lastResult = HavelRuntimeError("Cannot iterate over null array");
            interpreter->environment = originalEnv;
            return;
        }

        for (const auto &element : *array) {
            // Set loop variable (use first iterator if available)
            if (!node.iterators.empty()) {
                interpreter->environment->Define(node.iterators[0]->symbol, element);
            }

            // Execute loop body
            node.body->accept(*interpreter);

            // Check for break/continue
            if (std::holds_alternative<BreakValue>(interpreter->lastResult)) {
                interpreter->lastResult = nullptr;
                break;
            }
            if (std::holds_alternative<ContinueValue>(interpreter->lastResult)) {
                interpreter->lastResult = nullptr;
                continue;
            }
            if (isError(interpreter->lastResult)) {
                break;
            }
        }
    } else if (iterableValue.is<HavelObject>()) {
        // Object iteration
        auto object = iterableValue.get<HavelObject>();
        if (!object) {
            interpreter->lastResult = HavelRuntimeError("Cannot iterate over null object");
            interpreter->environment = originalEnv;
            return;
        }

        for (const auto &[key, value] : *object) {
            // Set loop variable to the key (use first iterator if available)
            if (!node.iterators.empty()) {
                interpreter->environment->Define(node.iterators[0]->symbol, HavelValue(key));
            }

            // Execute loop body
            node.body->accept(*interpreter);

            // Check for break/continue
            if (std::holds_alternative<BreakValue>(interpreter->lastResult)) {
                interpreter->lastResult = nullptr;
                break;
            }
            if (std::holds_alternative<ContinueValue>(interpreter->lastResult)) {
                interpreter->lastResult = nullptr;
                continue;
            }
            if (isError(interpreter->lastResult)) {
                break;
            }
        }
    } else {
        interpreter->lastResult = HavelRuntimeError("Cannot iterate over value");
    }

    // Restore original environment
    interpreter->environment = originalEnv;
}

void StatementEvaluator::visitLoopStatement(const ast::LoopStatement& node) {
    // Loop with optional condition
    while (true) {
        // Check condition if present (loop while condition {})
        if (node.condition) {
            auto condResult = Evaluate(*node.condition);
            if (isError(condResult)) {
                interpreter->lastResult = condResult;
                return;
            }
            if (!interpreter->ExecResultToBool(condResult)) {
                break;  // Condition is false, exit loop
            }
        }

        // Execute loop body
        auto bodyResult = Evaluate(*node.body);

        // Handle errors and return statements
        if (isError(bodyResult)) {
            interpreter->lastResult = bodyResult;
            return;
        }

        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            interpreter->lastResult = bodyResult;
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

    interpreter->lastResult = nullptr;
}

void StatementEvaluator::visitBreakStatement(const ast::BreakStatement& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = BreakValue{};
}

void StatementEvaluator::visitContinueStatement(const ast::ContinueStatement& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = ContinueValue{};
}

void StatementEvaluator::visitWhileStatement(const ast::WhileStatement& node) {
    // Evaluate condition and loop while true
    while (true) {
        auto conditionResult = Evaluate(*node.condition);
        if (isError(conditionResult)) {
            interpreter->lastResult = conditionResult;
            return;
        }

        if (!ValueToBool(unwrap(conditionResult))) {
            break; // Exit loop when condition is false
        }

        // Execute loop body
        auto bodyResult = Evaluate(*node.body);

        // Handle errors and return statements
        if (isError(bodyResult)) {
            interpreter->lastResult = bodyResult;
            return;
        }

        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            interpreter->lastResult = bodyResult;
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

    interpreter->lastResult = nullptr;
}

void StatementEvaluator::visitDoWhileStatement(const ast::DoWhileStatement& node) {
    // Execute body first, then check condition
    while (true) {
        // Execute loop body
        auto bodyResult = Evaluate(*node.body);

        // Handle errors and return statements
        if (isError(bodyResult)) {
            interpreter->lastResult = bodyResult;
            return;
        }

        if (std::holds_alternative<ReturnValue>(bodyResult)) {
            interpreter->lastResult = bodyResult;
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
            interpreter->lastResult = conditionResult;
            return;
        }

        if (!ValueToBool(unwrap(conditionResult))) {
            break; // Exit loop when condition is false
        }
    }

    interpreter->lastResult = nullptr;
}

void StatementEvaluator::visitInputStatement(const ast::InputStatement& node) {
    interpreter->visitInputStatement(node);
}

void StatementEvaluator::visitArrayPattern(const ast::ArrayPattern& node) {
    interpreter->visitArrayPattern(node);
}

void StatementEvaluator::visitImportStatement(const ast::ImportStatement& node) {
    interpreter->visitImportStatement(node);
}

void StatementEvaluator::visitUseStatement(const ast::UseStatement& node) {
    interpreter->visitUseStatement(node);
}

void StatementEvaluator::visitWithStatement(const ast::WithStatement& node) {
    interpreter->visitWithStatement(node);
}

void StatementEvaluator::visitConfigBlock(const ast::ConfigBlock& node) {
    interpreter->visitConfigBlock(node);
}

void StatementEvaluator::visitDevicesBlock(const ast::DevicesBlock& node) {
    interpreter->visitDevicesBlock(node);
}

void StatementEvaluator::visitModesBlock(const ast::ModesBlock& node) {
    interpreter->visitModesBlock(node);
}

void StatementEvaluator::visitConfigSection(const ast::ConfigSection& node) {
    interpreter->visitConfigSection(node);
}

void StatementEvaluator::visitSwitchStatement(const ast::SwitchStatement& node) {
    // Evaluate the switch expression
    auto expressionResult = Evaluate(*node.expression);
    if (isError(expressionResult)) {
        interpreter->lastResult = expressionResult;
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
                interpreter->lastResult = caseResult;
                return;
            }

            if (std::holds_alternative<ReturnValue>(caseResult)) {
                interpreter->lastResult = caseResult;
                return;
            }

            if (std::holds_alternative<BreakValue>(caseResult)) {
                // Break exits the switch
                interpreter->lastResult = caseResult;
                return;
            }

            // Continue is not meaningful in switch, treat as normal completion
            if (std::holds_alternative<ContinueValue>(caseResult)) {
                interpreter->lastResult = caseResult;
                return;
            }

            interpreter->lastResult = caseResult;
            return;
        }

        // Evaluate case test
        auto testResult = Evaluate(*caseNode->test);
        if (isError(testResult)) {
            interpreter->lastResult = testResult;
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
                interpreter->lastResult = caseResult;
                return;
            }

            if (std::holds_alternative<ReturnValue>(caseResult)) {
                interpreter->lastResult = caseResult;
                return;
            }

            if (std::holds_alternative<BreakValue>(caseResult)) {
                // Break exits the switch
                interpreter->lastResult = caseResult;
                return;
            }

            // Continue is not meaningful in switch, treat as normal completion
            if (std::holds_alternative<ContinueValue>(caseResult)) {
                interpreter->lastResult = caseResult;
                return;
            }

            interpreter->lastResult = caseResult;
            return;
        }
    }

    // No case matched and no else case
    interpreter->lastResult = nullptr;
}

void StatementEvaluator::visitSwitchCase(const ast::SwitchCase& node) {
    (void)node;  // Suppress unused warning
    // This should not be called directly - switch cases are handled by
    // visitSwitchStatement
    interpreter->lastResult = HavelRuntimeError("SwitchCase should not be visited directly");
}

void StatementEvaluator::visitObjectPattern(const ast::ObjectPattern& node) {
    // This is typically handled during assignment/let declaration
    // For now, just evaluate pattern properties (identifiers)
    for (const auto &[key, pattern] : node.properties) {
        if (pattern) {
            auto result = Evaluate(*pattern);
            if (isError(result)) {
                interpreter->lastResult = result;
                return;
            }
        }
    }
    interpreter->lastResult = HavelValue(nullptr); // Patterns don't produce values
}

void StatementEvaluator::visitThrowStatement(const ast::ThrowStatement& node) {
    if (!node.value) {
        interpreter->lastResult = HavelRuntimeError("Thrown value is null");
        return;
    }

    auto valueResult = Evaluate(*node.value);
    if (isError(valueResult)) {
        interpreter->lastResult = valueResult;
        return;
    }

    HavelValue thrownValue = unwrap(valueResult);

    // Store the thrown value - preserve the original type
    // If it's a string, use it directly; otherwise convert to string
    if (thrownValue.isString()) {
        interpreter->lastResult = HavelRuntimeError(thrownValue.asString());
    } else {
        interpreter->lastResult = HavelRuntimeError(ValueToString(thrownValue));
    }
}

void StatementEvaluator::visitStructFieldDef(const ast::StructFieldDef& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = HavelRuntimeError("StructFieldDef evaluation not implemented");
}

void StatementEvaluator::visitStructMethodDef(const ast::StructMethodDef& node) {
    (void)node;  // Suppress unused warning
    // Methods are handled when accessed on struct instances
    interpreter->lastResult = HavelValue(nullptr);
}

void StatementEvaluator::visitStructDefinition(const ast::StructDefinition& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = HavelRuntimeError("StructDefinition evaluation not implemented");
}

void StatementEvaluator::visitStructDeclaration(const ast::StructDeclaration& node) {
    (void)node;  // Suppress unused warning
    // Delegate to Interpreter for full implementation
    interpreter->visitStructDeclaration(node);
}

void StatementEvaluator::visitEnumVariantDef(const ast::EnumVariantDef& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = HavelRuntimeError("EnumVariantDef evaluation not implemented");
}

void StatementEvaluator::visitEnumDefinition(const ast::EnumDefinition& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = HavelRuntimeError("EnumDefinition evaluation not implemented");
}

void StatementEvaluator::visitEnumDeclaration(const ast::EnumDeclaration& node) {
    (void)node;  // Suppress unused warning
    // Delegate to Interpreter for full implementation
    interpreter->visitEnumDeclaration(node);
}

void StatementEvaluator::visitTraitDeclaration(const ast::TraitDeclaration& node) {
    (void)node;  // Suppress unused warning
    // Delegate to Interpreter for full implementation
    interpreter->visitTraitDeclaration(node);
}

void StatementEvaluator::visitTraitMethod(const ast::TraitMethod& node) {
    (void)node;  // Suppress unused warning
    interpreter->lastResult = HavelRuntimeError("TraitMethod evaluation not implemented");
}

void StatementEvaluator::visitImplDeclaration(const ast::ImplDeclaration& node) {
    (void)node;  // Suppress unused warning
    // Delegate to Interpreter for full implementation
    interpreter->visitImplDeclaration(node);
}

void StatementEvaluator::visitIdentifier(const ast::Identifier& node) {
    interpreter->visitIdentifier(node);
}

void StatementEvaluator::visitOnReloadStatement(const ast::OnReloadStatement& node) {
    interpreter->visitOnReloadStatement(node);
}

void StatementEvaluator::visitOnStartStatement(const ast::OnStartStatement& node) {
    interpreter->visitOnStartStatement(node);
}

void StatementEvaluator::visitTypeDeclaration(const ast::TypeDeclaration& node) {
    interpreter->visitTypeDeclaration(node);
}

void StatementEvaluator::visitTypeAnnotation(const ast::TypeAnnotation& node) {
    interpreter->visitTypeAnnotation(node);
}

void StatementEvaluator::visitUnionType(const ast::UnionType& node) {
    interpreter->visitUnionType(node);
}

void StatementEvaluator::visitRecordType(const ast::RecordType& node) {
    interpreter->visitRecordType(node);
}

void StatementEvaluator::visitFunctionType(const ast::FunctionType& node) {
    interpreter->visitFunctionType(node);
}

void StatementEvaluator::visitTypeReference(const ast::TypeReference& node) {
    interpreter->visitTypeReference(node);
}

// Helper method implementations
HavelResult StatementEvaluator::Evaluate(const ast::ASTNode& node) {
    return interpreter->Evaluate(node);
}

bool StatementEvaluator::isError(const HavelResult& result) {
    return std::holds_alternative<HavelRuntimeError>(result);
}

HavelValue StatementEvaluator::unwrap(const HavelResult& result) {
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

std::string StatementEvaluator::ValueToString(const HavelValue& value) {
    return interpreter->ValueToString(value);
}

double StatementEvaluator::ValueToNumber(const HavelValue& value) {
    return interpreter->ValueToNumber(value);
}

bool StatementEvaluator::ValueToBool(const HavelValue& value) {
    return interpreter->ValueToBool(value);
}

} // namespace havel
