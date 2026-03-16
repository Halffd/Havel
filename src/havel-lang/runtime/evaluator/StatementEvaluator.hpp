/*
 * StatementEvaluator.hpp
 * 
 * Statement evaluation for Havel interpreter.
 * Extracted from Interpreter.cpp as part of runtime refactoring.
 */
#pragma once

#include "../Environment.hpp"
#include "../../ast/AST.h"
#include "../../../core/ConfigManager.hpp"

namespace havel {

class Interpreter;

/**
 * StatementEvaluator - Statement evaluation helper
 * 
 * Handles evaluation of all statement types.
 * Separated from Interpreter to reduce file size.
 */
class StatementEvaluator {
public:
    explicit StatementEvaluator(Interpreter* interp) : interpreter(interp) {}
    
    // Statement visitors
    void visitProgram(const ast::Program& node);
    void visitLetDeclaration(const ast::LetDeclaration& node);
    void visitFunctionDeclaration(const ast::FunctionDeclaration& node);
    void visitFunctionParameter(const ast::FunctionParameter& node);
    void visitReturnStatement(const ast::ReturnStatement& node);
    void visitIfStatement(const ast::IfStatement& node);
    void visitBlockStatement(const ast::BlockStatement& node);
    void visitHotkeyBinding(const ast::HotkeyBinding& node);
    void visitSleepStatement(const ast::SleepStatement& node);
    void visitRepeatStatement(const ast::RepeatStatement& node);
    void visitShellCommandStatement(const ast::ShellCommandStatement& node);
    void visitInputStatement(const ast::InputStatement& node);
    void visitArrayPattern(const ast::ArrayPattern& node);
    void visitImportStatement(const ast::ImportStatement& node);
    void visitUseStatement(const ast::UseStatement& node);
    void visitWithStatement(const ast::WithStatement& node);
    void visitConfigBlock(const ast::ConfigBlock& node);
    void visitDevicesBlock(const ast::DevicesBlock& node);
    void visitModesBlock(const ast::ModesBlock& node);
    void visitConfigSection(const ast::ConfigSection& node);
    void visitWhileStatement(const ast::WhileStatement& node);
    void visitDoWhileStatement(const ast::DoWhileStatement& node);
    void visitSwitchStatement(const ast::SwitchStatement& node);
    void visitSwitchCase(const ast::SwitchCase& node);
    void visitObjectPattern(const ast::ObjectPattern& node);
    void visitThrowStatement(const ast::ThrowStatement& node);
    void visitStructFieldDef(const ast::StructFieldDef& node);
    void visitStructMethodDef(const ast::StructMethodDef& node);
    void visitStructDefinition(const ast::StructDefinition& node);
    void visitStructDeclaration(const ast::StructDeclaration& node);
    void visitEnumVariantDef(const ast::EnumVariantDef& node);
    void visitEnumDefinition(const ast::EnumDefinition& node);
    void visitEnumDeclaration(const ast::EnumDeclaration& node);
    void visitTraitDeclaration(const ast::TraitDeclaration& node);
    void visitTraitMethod(const ast::TraitMethod& node);
    void visitImplDeclaration(const ast::ImplDeclaration& node);
    void visitForStatement(const ast::ForStatement& node);
    void visitLoopStatement(const ast::LoopStatement& node);
    void visitBreakStatement(const ast::BreakStatement& node);
    void visitContinueStatement(const ast::ContinueStatement& node);
    void visitIdentifier(const ast::Identifier& node);
    void visitOnReloadStatement(const ast::OnReloadStatement& node);
    void visitOnStartStatement(const ast::OnStartStatement& node);
    void visitOnTapStatement(const ast::OnTapStatement& node);
    void visitOnComboStatement(const ast::OnComboStatement& node);
    void visitTypeDeclaration(const ast::TypeDeclaration& node);
    void visitTypeAnnotation(const ast::TypeAnnotation& node);
    void visitUnionType(const ast::UnionType& node);
    void visitRecordType(const ast::RecordType& node);
    void visitFunctionType(const ast::FunctionType& node);
    void visitTypeReference(const ast::TypeReference& node);
    
private:
    Interpreter* interpreter;
    
    // Helper methods
    HavelResult Evaluate(const ast::ASTNode& node);
    bool isError(const HavelResult& result);
    HavelValue unwrap(const HavelResult& result);
    std::string ValueToString(const HavelValue& value);
    double ValueToNumber(const HavelValue& value);
    bool ValueToBool(const HavelValue& value);
    
    // Config processing helper
    void processConfigPairs(
        const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs,
        Configs& config,
        const std::string& prefix);
};

} // namespace havel
