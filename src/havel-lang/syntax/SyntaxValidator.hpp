/*
 * SyntaxValidator.hpp
 *
 * Syntax-level validation for Havel language.
 */
#pragma once

#include "../ast/AST.h"
#include "../common/Debug.hpp"
#include <string>
#include <vector>

namespace havel::syntax {

struct SyntaxError {
  enum class Kind {
    InvalidStructField,
    InvalidEnumVariant,
    InvalidTypeAnnotation,
    InvalidStructConstruction,
    InvalidEnumConstruction,
    DuplicateField,
    InvalidIdentifier
  };

  Kind kind;
  std::string message;
  size_t line;
  size_t column;

  SyntaxError(Kind k, const std::string& msg, size_t l, size_t c)
    : kind(k), message(msg), line(l), column(c) {}
};

class SyntaxValidator {
public:
  SyntaxValidator() = default;
  ~SyntaxValidator() = default;

  bool validate(const ast::Program& program);
  const std::vector<SyntaxError>& getErrors() const { return errors_; }

  void validateStructSyntax(const ast::StructDeclaration& structDecl);
  void validateEnumSyntax(const ast::EnumDeclaration& enumDecl);
  void validateTypeAnnotationSyntax(const ast::TypeAnnotation& annotation);
  void validateStructConstruction(const ast::ObjectLiteral& construction,
                                  const ast::StructDeclaration& structDef);
  void validateEnumConstruction(const ast::CallExpression& construction,
                                const ast::EnumDeclaration& enumDef);
  void validateHotkeySyntax(const ast::HotkeyBinding& binding);
  void validatePipelineSyntax(const ast::PipelineExpression& pipeline);
  void validateMatchSyntax(const ast::MatchExpression& matchExpr);
  void validateLambdaSyntax(const ast::LambdaExpression& lambda);
  void validateDestructuringSyntax(const ast::Expression& pattern);

  static bool isValidIdentifier(const std::string& name);
  static bool isValidTypeName(const std::string& name);
  void reportError(SyntaxError::Kind kind, const std::string& message,
                   size_t line, size_t column);

private:
  std::vector<SyntaxError> errors_;

  void checkDuplicateFields(const std::vector<std::string>& names,
                           const std::string& context,
                           size_t line, size_t column);
  void checkDuplicateVariants(const std::vector<ast::EnumVariantDef>& variants,
                             size_t line, size_t column);
};

} // namespace havel::syntax
