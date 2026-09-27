/*
 * SyntaxValidator.cpp
 *
 * Syntax-level validation implementation for Havel language.
 */
#include "SyntaxValidator.hpp"
#include "../ast/AST.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace havel::syntax {

bool SyntaxValidator::validate(const ast::Program& program) {
  errors_.clear();

  for (const auto& stmt : program.body) {
    if (!stmt) continue;

    switch (stmt->kind) {
      case ast::NodeType::StructDeclaration:
        validateStructSyntax(static_cast<const ast::StructDeclaration&>(*stmt));
        break;
      case ast::NodeType::EnumDeclaration:
        validateEnumSyntax(static_cast<const ast::EnumDeclaration&>(*stmt));
        break;
      default:
        break;
    }
  }

  return errors_.empty();
}

void SyntaxValidator::validateStructSyntax(const ast::StructDeclaration& structDecl) {
  if (!isValidTypeName(structDecl.name)) {
    reportError(SyntaxError::Kind::InvalidIdentifier,
                "Invalid struct name: '" + structDecl.name + "'",
                structDecl.line, structDecl.column);
  }

  std::vector<std::string> fieldNames;
  for (const auto& field : structDecl.definition.fields) {
    if (!isValidIdentifier(field.name)) {
      reportError(SyntaxError::Kind::InvalidStructField,
                  "Invalid field name: '" + field.name + "'",
                  field.line, field.column);
    }
    fieldNames.push_back(field.name);
  }

  checkDuplicateFields(fieldNames, "struct", structDecl.line, structDecl.column);
}

void SyntaxValidator::validateEnumSyntax(const ast::EnumDeclaration& enumDecl) {
  if (!isValidTypeName(enumDecl.name)) {
    reportError(SyntaxError::Kind::InvalidIdentifier,
                "Invalid enum name: '" + enumDecl.name + "'",
                enumDecl.line, enumDecl.column);
  }

  checkDuplicateVariants(enumDecl.definition.variants, enumDecl.line, enumDecl.column);

  for (const auto& variant : enumDecl.definition.variants) {
    if (!isValidIdentifier(variant.name)) {
      reportError(SyntaxError::Kind::InvalidEnumVariant,
                  "Invalid variant name: '" + variant.name + "'",
                  variant.line, variant.column);
    }
  }
}

void SyntaxValidator::validateTypeAnnotationSyntax(const ast::TypeAnnotation&) {}
void SyntaxValidator::validateStructConstruction(const ast::ObjectLiteral&, const ast::StructDeclaration&) {}
void SyntaxValidator::validateEnumConstruction(const ast::CallExpression&, const ast::EnumDeclaration&) {}
void SyntaxValidator::validateHotkeySyntax(const ast::HotkeyBinding&) {}
void SyntaxValidator::validatePipelineSyntax(const ast::PipelineExpression&) {}
void SyntaxValidator::validateMatchSyntax(const ast::MatchExpression&) {}
void SyntaxValidator::validateLambdaSyntax(const ast::LambdaExpression&) {}
void SyntaxValidator::validateDestructuringSyntax(const ast::Expression&) {}

bool SyntaxValidator::isValidIdentifier(const std::string& name) {
  if (name.empty()) return false;
  unsigned char first = static_cast<unsigned char>(name[0]);
  // ASCII-safe check: a-z, A-Z, or _
  if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
    return false;
  }
  for (size_t i = 1; i < name.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    // ASCII-safe check: a-z, A-Z, 0-9, or _
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
          (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  static const std::unordered_set<std::string> keywords = {
    "let", "fn", "if", "else", "while", "for", "return", "break", "continue",
    "struct", "enum", "match", "when", "with", "use", "import", "async", "await"
  };
  return keywords.count(name) == 0;
}

bool SyntaxValidator::isValidTypeName(const std::string& name) {
  if (name.empty()) return false;
  unsigned char first = static_cast<unsigned char>(name[0]);
  // ASCII-safe check: A-Z only (type names must start with uppercase)
  if (!(first >= 'A' && first <= 'Z')) {
    return false;
  }
  for (size_t i = 1; i < name.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    // ASCII-safe check: a-z, A-Z, 0-9, or _
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
          (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

void SyntaxValidator::reportError(SyntaxError::Kind kind, const std::string& message,
                                  size_t line, size_t column) {
  errors_.emplace_back(kind, message, line, column);
}

void SyntaxValidator::checkDuplicateFields(const std::vector<std::string>& names,
                                           const std::string& context,
                                           size_t line, size_t column) {
  std::unordered_set<std::string> seen;
  for (const auto& name : names) {
    if (seen.count(name) > 0) {
      reportError(SyntaxError::Kind::DuplicateField,
                  "Duplicate field '" + name + "' in " + context, line, column);
    }
    seen.insert(name);
  }
}

void SyntaxValidator::checkDuplicateVariants(const std::vector<ast::EnumVariantDef>& variants,
                                             size_t line, size_t column) {
  std::unordered_set<std::string> seen;
  for (const auto& variant : variants) {
    if (seen.count(variant.name) > 0) {
      reportError(SyntaxError::Kind::InvalidEnumVariant,
                  "Duplicate variant '" + variant.name + "' in enum", line, column);
    }
    seen.insert(variant.name);
  }
}

} // namespace havel::syntax
