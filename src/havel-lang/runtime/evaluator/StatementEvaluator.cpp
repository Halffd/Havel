/*
 * StatementEvaluator.cpp
 * 
 * Statement evaluation for Havel interpreter.
 * Implementation delegates to Interpreter methods.
 */
#include "StatementEvaluator.hpp"
#include "../Interpreter.hpp"

namespace havel {

// Note: Statement evaluation methods are currently implemented in Interpreter.cpp
// This file serves as a placeholder for future extraction.
// 
// To complete the extraction:
// 1. Copy each visit*Statement method from Interpreter.cpp to this file
// 2. Replace 'this->' references with 'interpreter->'
// 3. Update Interpreter.cpp to call evaluator methods instead

void StatementEvaluator::visitProgram(const ast::Program& node) {
    interpreter->visitProgram(node);
}

void StatementEvaluator::visitLetDeclaration(const ast::LetDeclaration& node) {
    interpreter->visitLetDeclaration(node);
}

void StatementEvaluator::visitFunctionDeclaration(const ast::FunctionDeclaration& node) {
    interpreter->visitFunctionDeclaration(node);
}

void StatementEvaluator::visitFunctionParameter(const ast::FunctionParameter& node) {
    interpreter->visitFunctionParameter(node);
}

void StatementEvaluator::visitReturnStatement(const ast::ReturnStatement& node) {
    interpreter->visitReturnStatement(node);
}

void StatementEvaluator::visitIfStatement(const ast::IfStatement& node) {
    interpreter->visitIfStatement(node);
}

void StatementEvaluator::visitBlockStatement(const ast::BlockStatement& node) {
    interpreter->visitBlockStatement(node);
}

void StatementEvaluator::visitHotkeyBinding(const ast::HotkeyBinding& node) {
    interpreter->visitHotkeyBinding(node);
}

void StatementEvaluator::visitSleepStatement(const ast::SleepStatement& node) {
    interpreter->visitSleepStatement(node);
}

void StatementEvaluator::visitRepeatStatement(const ast::RepeatStatement& node) {
    interpreter->visitRepeatStatement(node);
}

void StatementEvaluator::visitShellCommandStatement(const ast::ShellCommandStatement& node) {
    interpreter->visitShellCommandStatement(node);
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

void StatementEvaluator::visitWhileStatement(const ast::WhileStatement& node) {
    interpreter->visitWhileStatement(node);
}

void StatementEvaluator::visitDoWhileStatement(const ast::DoWhileStatement& node) {
    interpreter->visitDoWhileStatement(node);
}

void StatementEvaluator::visitSwitchStatement(const ast::SwitchStatement& node) {
    interpreter->visitSwitchStatement(node);
}

void StatementEvaluator::visitSwitchCase(const ast::SwitchCase& node) {
    interpreter->visitSwitchCase(node);
}

void StatementEvaluator::visitObjectPattern(const ast::ObjectPattern& node) {
    interpreter->visitObjectPattern(node);
}

void StatementEvaluator::visitThrowStatement(const ast::ThrowStatement& node) {
    interpreter->visitThrowStatement(node);
}

void StatementEvaluator::visitStructFieldDef(const ast::StructFieldDef& node) {
    interpreter->visitStructFieldDef(node);
}

void StatementEvaluator::visitStructMethodDef(const ast::StructMethodDef& node) {
    interpreter->visitStructMethodDef(node);
}

void StatementEvaluator::visitStructDefinition(const ast::StructDefinition& node) {
    interpreter->visitStructDefinition(node);
}

void StatementEvaluator::visitStructDeclaration(const ast::StructDeclaration& node) {
    interpreter->visitStructDeclaration(node);
}

void StatementEvaluator::visitEnumVariantDef(const ast::EnumVariantDef& node) {
    interpreter->visitEnumVariantDef(node);
}

void StatementEvaluator::visitEnumDefinition(const ast::EnumDefinition& node) {
    interpreter->visitEnumDefinition(node);
}

void StatementEvaluator::visitEnumDeclaration(const ast::EnumDeclaration& node) {
    interpreter->visitEnumDeclaration(node);
}

void StatementEvaluator::visitTraitDeclaration(const ast::TraitDeclaration& node) {
    interpreter->visitTraitDeclaration(node);
}

void StatementEvaluator::visitTraitMethod(const ast::TraitMethod& node) {
    interpreter->visitTraitMethod(node);
}

void StatementEvaluator::visitImplDeclaration(const ast::ImplDeclaration& node) {
    interpreter->visitImplDeclaration(node);
}

void StatementEvaluator::visitForStatement(const ast::ForStatement& node) {
    interpreter->visitForStatement(node);
}

void StatementEvaluator::visitLoopStatement(const ast::LoopStatement& node) {
    interpreter->visitLoopStatement(node);
}

void StatementEvaluator::visitBreakStatement(const ast::BreakStatement& node) {
    interpreter->visitBreakStatement(node);
}

void StatementEvaluator::visitContinueStatement(const ast::ContinueStatement& node) {
    interpreter->visitContinueStatement(node);
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
HavelValue StatementEvaluator::Evaluate(ast::Expression& expr) {
    return interpreter->Evaluate(expr);
}

bool StatementEvaluator::isError(const HavelResult& result) {
    return interpreter->isError(result);
}

HavelValue StatementEvaluator::unwrap(HavelResult& result) {
    return interpreter->unwrap(result);
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
