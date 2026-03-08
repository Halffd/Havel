/*
 * ExprEvaluator.hpp
 * 
 * Expression evaluation for Havel interpreter.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#pragma once

#include "../Environment.hpp"
#include "../../ast/AST.h"

namespace havel {

class Interpreter;

/**
 * ExprEvaluator - Expression evaluation helper
 * 
 * Handles evaluation of all expression types.
 * Separated from Interpreter to reduce file size.
 */
class ExprEvaluator {
public:
    explicit ExprEvaluator(Interpreter* interp) : interpreter(interp) {}
    
    // Expression visitors
    void visitBinaryExpression(const ast::BinaryExpression& node);
    void visitUnaryExpression(const ast::UnaryExpression& node);
    void visitUpdateExpression(const ast::UpdateExpression& node);
    void visitCallExpression(const ast::CallExpression& node);
    void visitMemberExpression(const ast::MemberExpression& node);
    void visitLambdaExpression(const ast::LambdaExpression& node);
    void visitSetExpression(const ast::SetExpression& node);
    void visitPipelineExpression(const ast::PipelineExpression& node);
    void visitStringLiteral(const ast::StringLiteral& node);
    void visitInterpolatedStringExpression(const ast::InterpolatedStringExpression& node);
    void visitNumberLiteral(const ast::NumberLiteral& node);
    void visitHotkeyLiteral(const ast::HotkeyLiteral& node);
    void visitAsyncExpression(const ast::AsyncExpression& node);
    void visitAwaitExpression(const ast::AwaitExpression& node);
    void visitArrayLiteral(const ast::ArrayLiteral& node);
    void visitSpreadExpression(const ast::SpreadExpression& node);
    void visitObjectLiteral(const ast::ObjectLiteral& node);
    void visitIndexExpression(const ast::IndexExpression& node);
    void visitTernaryExpression(const ast::TernaryExpression& node);
    void visitRangeExpression(const ast::RangeExpression& node);
    void visitAssignmentExpression(const ast::AssignmentExpression& node);
    void visitCastExpression(const ast::CastExpression& node);
    void visitTryExpression(const ast::TryExpression& node);
    void visitBlockExpression(const ast::BlockExpression& node);
    void visitIfExpression(const ast::IfExpression& node);
    void visitIdentifier(const ast::Identifier& node);
    void visitExpressionStatement(const ast::ExpressionStatement& node);
    void visitBacktickExpression(const ast::BacktickExpression& node);
    void visitShellCommandExpression(const ast::ShellCommandExpression& node);
    
private:
    Interpreter* interpreter;
    
    // Helper methods
    HavelResult Evaluate(const ast::ASTNode& node);
    bool isError(const HavelResult& result);
    HavelValue unwrap(const HavelResult& result);
    std::string ValueToString(const HavelValue& value);
    double ValueToNumber(const HavelValue& value);
    bool ValueToBool(const HavelValue& value);
};

} // namespace havel
