/*
 * ExprEvaluator.cpp
 * 
 * Expression evaluation for Havel interpreter.
 * Implementation delegates to Interpreter methods.
 */
#include "ExprEvaluator.hpp"
#include "../Interpreter.hpp"

namespace havel {

// Note: Expression evaluation methods are currently implemented in Interpreter.cpp
// This file serves as a placeholder for future extraction.
// 
// To complete the extraction:
// 1. Copy each visit*Expression method from Interpreter.cpp to this file
// 2. Replace 'this->' references with 'interpreter->'
// 3. Update Interpreter.cpp to call evaluator methods instead

void ExprEvaluator::visitBinaryExpression(const ast::BinaryExpression& node) {
    interpreter->visitBinaryExpression(node);
}

void ExprEvaluator::visitUnaryExpression(const ast::UnaryExpression& node) {
    interpreter->visitUnaryExpression(node);
}

void ExprEvaluator::visitUpdateExpression(const ast::UpdateExpression& node) {
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
    interpreter->visitStringLiteral(node);
}

void ExprEvaluator::visitInterpolatedStringExpression(const ast::InterpolatedStringExpression& node) {
    interpreter->visitInterpolatedStringExpression(node);
}

void ExprEvaluator::visitNumberLiteral(const ast::NumberLiteral& node) {
    interpreter->visitNumberLiteral(node);
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
    interpreter->visitExpressionStatement(node);
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
