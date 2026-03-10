/*
 * SemanticAnalyzer.cpp
 *
 * Semantic analysis implementation for Havel language.
 */
#include "SemanticAnalyzer.hpp"
#include "../ast/AST.h"
#include <algorithm>

namespace havel::semantic {

SemanticAnalyzer::SemanticAnalyzer() = default;

bool SemanticAnalyzer::analyze(const ast::Program& program) {
  errors_.clear();

  registerStructTypes(program);
  registerEnumTypes(program);
  validateTypeAnnotations(program);
  buildSymbolTable(program);
  inferTypes(program);
  checkFunctionSignatures(program);

  return errors_.empty();
}

void SemanticAnalyzer::registerStructTypes(const ast::Program& program) {
  auto& registry = TypeRegistry::getInstance();

  for (const auto& stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::StructDeclaration) continue;

    const auto& structDecl = static_cast<const ast::StructDeclaration&>(*stmt);
    auto structType = std::make_shared<HavelStructType>(structDecl.name);

    for (const auto& field : structDecl.definition.fields) {
      std::optional<std::shared_ptr<HavelType>> fieldType;
      if (field.type) {
        fieldType = resolveTypeDefinition(**field.type);
      }
      structType->addField(StructField(field.name, fieldType));
    }

    registry.registerStructType(structType);
  }
}

void SemanticAnalyzer::registerEnumTypes(const ast::Program& program) {
  auto& registry = TypeRegistry::getInstance();

  for (const auto& stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::EnumDeclaration) continue;

    const auto& enumDecl = static_cast<const ast::EnumDeclaration&>(*stmt);
    auto enumType = std::make_shared<HavelEnumType>(enumDecl.name);

    for (const auto& variant : enumDecl.definition.variants) {
      std::optional<std::shared_ptr<HavelType>> payloadType;
      bool hasPayload = variant.payloadType.has_value();
      if (hasPayload && variant.payloadType) {
        payloadType = resolveTypeDefinition(**variant.payloadType);
      }
      enumType->addVariant(EnumVariant(variant.name, hasPayload, payloadType));
    }

    registry.registerEnumType(enumType);
  }
}

void SemanticAnalyzer::validateTypeAnnotations(const ast::Program& program) {
  for (const auto& stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::LetDeclaration) continue;

    const auto& letDecl = static_cast<const ast::LetDeclaration&>(*stmt);
    if (letDecl.typeAnnotation) {
      // Type annotation validation would go here
    }
  }
}

void SemanticAnalyzer::buildSymbolTable(const ast::Program& program) {
  symbolTable_ = SymbolTable();
  symbolTable_.enterScope();

  for (const auto& stmt : program.body) {
    if (!stmt) continue;

    if (stmt->kind == ast::NodeType::FunctionDeclaration) {
      const auto& funcDecl = static_cast<const ast::FunctionDeclaration&>(*stmt);
      if (funcDecl.name) {
        Symbol symbol(funcDecl.name->symbol, Symbol::Kind::Function, HavelType::any(), 0);
        symbolTable_.define(symbol);
      }
    } else if (stmt->kind == ast::NodeType::StructDeclaration) {
      const auto& structDecl = static_cast<const ast::StructDeclaration&>(*stmt);
      auto structType = std::make_shared<HavelStructType>(structDecl.name);
      Symbol symbol(structDecl.name, Symbol::Kind::Struct, structType, 0, false);
      symbolTable_.define(symbol);
    } else if (stmt->kind == ast::NodeType::EnumDeclaration) {
      const auto& enumDecl = static_cast<const ast::EnumDeclaration&>(*stmt);
      auto enumType = std::make_shared<HavelEnumType>(enumDecl.name);
      Symbol symbol(enumDecl.name, Symbol::Kind::Enum, enumType, 0, false);
      symbolTable_.define(symbol);
    } else if (stmt->kind == ast::NodeType::LetDeclaration) {
      const auto& letDecl = static_cast<const ast::LetDeclaration&>(*stmt);
      std::shared_ptr<HavelType> varType = HavelType::any();
      if (letDecl.typeAnnotation) {
        varType = resolveType(**letDecl.typeAnnotation);
      }
      
      if (letDecl.pattern && letDecl.pattern->kind == ast::NodeType::Identifier) {
        const auto& ident = static_cast<const ast::Identifier&>(*letDecl.pattern);
        Symbol symbol(ident.symbol, Symbol::Kind::Variable, varType, 0, !letDecl.isConst, letDecl.value != nullptr);
        symbolTable_.define(symbol);
      }
    }
  }

  symbolTable_.exitScope();
}

void SemanticAnalyzer::inferTypes(const ast::Program&) {
  // TODO: Implement type inference
}

void SemanticAnalyzer::checkFunctionSignatures(const ast::Program& program) {
  for (const auto& stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::FunctionDeclaration) continue;
    // TODO: Validate function body
  }
}

bool SemanticAnalyzer::isValidType(const std::string& typeName) const {
  static const std::unordered_set<std::string> builtinTypes = {
    "Num", "Str", "Bool", "Any", "Null", "Array", "Object", "Func"
  };
  if (builtinTypes.count(typeName) > 0) return true;

  const auto& registry = TypeRegistry::getInstance();
  return registry.hasStructType(typeName) || registry.hasEnumType(typeName);
}

void SemanticAnalyzer::reportError(SemanticError::Kind kind, const std::string& message,
                                   size_t line, size_t column) {
  errors_.emplace_back(kind, message, line, column);
}

void SemanticAnalyzer::visitStatement(const ast::Statement&) {
  // TODO: Implement full visitor
}

void SemanticAnalyzer::visitExpression(const ast::Expression&) {
  // TODO: Implement full visitor
}

std::shared_ptr<HavelType> SemanticAnalyzer::resolveType(const ast::TypeAnnotation& annotation) {
  if (!annotation.type) return HavelType::any();
  return resolveTypeDefinition(*annotation.type);
}

std::shared_ptr<HavelType> SemanticAnalyzer::resolveTypeDefinition(const ast::TypeDefinition& typeDef) {
  if (typeDef.kind == ast::NodeType::TypeAnnotation) {  // TypeReference uses TypeAnnotation node type
    const auto& typeRef = static_cast<const ast::TypeReference&>(typeDef);
    const std::string& typeName = typeRef.name;

    if (typeName == "Num" || typeName == "Number") return HavelType::num();
    if (typeName == "Str" || typeName == "String") return HavelType::str();
    if (typeName == "Bool" || typeName == "Boolean") return HavelType::boolean();
    if (typeName == "Any") return HavelType::any();
    if (typeName == "Null") return HavelType::null();

    auto& registry = TypeRegistry::getInstance();
    if (auto structType = registry.getStructType(typeName)) return structType;
    if (auto enumType = registry.getEnumType(typeName)) return enumType;
  }

  return HavelType::any();
}

std::shared_ptr<HavelType> SemanticAnalyzer::inferExpressionType(const ast::Expression& expr) {
  switch (expr.kind) {
    case ast::NodeType::NumberLiteral: return HavelType::num();
    case ast::NodeType::StringLiteral: return HavelType::str();
    case ast::NodeType::BooleanLiteral: return HavelType::boolean();
    case ast::NodeType::ArrayLiteral: return std::make_shared<HavelArrayType>();
    default: return HavelType::any();
  }
}

} // namespace havel::semantic
