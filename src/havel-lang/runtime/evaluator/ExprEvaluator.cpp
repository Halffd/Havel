/*
 * ExprEvaluator.cpp
 * 
 * Expression evaluation for Havel interpreter.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#include "ExprEvaluator.hpp"
#include "../Interpreter.hpp"
#include "core/ConfigManager.hpp"

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
    // Delegate to Interpreter for now
    interpreter->visitUpdateExpression(node);
}

void ExprEvaluator::visitCallExpression(const ast::CallExpression& node) {
    interpreter->visitCallExpression(node);
}

void ExprEvaluator::visitMemberExpression(const ast::MemberExpression& node) {
    interpreter->visitMemberExpression(node);
}

void ExprEvaluator::visitLambdaExpression(const ast::LambdaExpression& node) {
    interpreter->visitLambdaExpression(node);
}

void ExprEvaluator::visitSetExpression(const ast::SetExpression& node) {
    interpreter->visitSetExpression(node);
}

void ExprEvaluator::visitPipelineExpression(const ast::PipelineExpression& node) {
    interpreter->visitPipelineExpression(node);
}

void ExprEvaluator::visitStringLiteral(const ast::StringLiteral& node) {
    interpreter->lastResult = HavelValue(node.value);
}

void ExprEvaluator::visitInterpolatedStringExpression(const ast::InterpolatedStringExpression& node) {
    interpreter->visitInterpolatedStringExpression(node);
}

void ExprEvaluator::visitNumberLiteral(const ast::NumberLiteral& node) {
    interpreter->lastResult = HavelValue(node.value);
}

void ExprEvaluator::visitHotkeyLiteral(const ast::HotkeyLiteral& node) {
    interpreter->visitHotkeyLiteral(node);
}

void ExprEvaluator::visitAsyncExpression(const ast::AsyncExpression& node) {
    interpreter->visitAsyncExpression(node);
}

void ExprEvaluator::visitAwaitExpression(const ast::AwaitExpression& node) {
    interpreter->visitAwaitExpression(node);
}

void ExprEvaluator::visitArrayLiteral(const ast::ArrayLiteral& node) {
    interpreter->visitArrayLiteral(node);
}

void ExprEvaluator::visitSpreadExpression(const ast::SpreadExpression& node) {
    interpreter->visitSpreadExpression(node);
}

void ExprEvaluator::visitObjectLiteral(const ast::ObjectLiteral& node) {
    interpreter->visitObjectLiteral(node);
}

void ExprEvaluator::visitIndexExpression(const ast::IndexExpression& node) {
    interpreter->visitIndexExpression(node);
}

void ExprEvaluator::visitTernaryExpression(const ast::TernaryExpression& node) {
    interpreter->visitTernaryExpression(node);
}

void ExprEvaluator::visitRangeExpression(const ast::RangeExpression& node) {
    interpreter->visitRangeExpression(node);
}

void ExprEvaluator::visitAssignmentExpression(const ast::AssignmentExpression& node) {
    interpreter->visitAssignmentExpression(node);
}

void ExprEvaluator::visitTryExpression(const ast::TryExpression& node) {
    interpreter->visitTryExpression(node);
}

void ExprEvaluator::visitBlockExpression(const ast::BlockExpression& node) {
    interpreter->visitBlockExpression(node);
}

void ExprEvaluator::visitIfExpression(const ast::IfExpression& node) {
    interpreter->visitIfExpression(node);
}

void ExprEvaluator::visitExpressionStatement(const ast::ExpressionStatement& node) {
    Evaluate(*node.expression);
}

void ExprEvaluator::visitBacktickExpression(const ast::BacktickExpression& node) {
    interpreter->visitBacktickExpression(node);
}

void ExprEvaluator::visitShellCommandExpression(const ast::ShellCommandExpression& node) {
    interpreter->visitShellCommandExpression(node);
}

// Helper method implementations
HavelValue ExprEvaluator::Evaluate(ast::Expression& expr) {
    return interpreter->Evaluate(expr);
}

bool ExprEvaluator::isError(const HavelResult& result) {
    return interpreter->isError(result);
}

HavelValue ExprEvaluator::unwrap(HavelResult& result) {
    return interpreter->unwrap(result);
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

} // namespace havel
