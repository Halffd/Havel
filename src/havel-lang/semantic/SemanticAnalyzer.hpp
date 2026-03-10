/*
 * SemanticAnalyzer.hpp
 *
 * Semantic analysis phase for Havel language.
 * Performs type checking, symbol table construction, and type inference.
 */
#pragma once

#include "../ast/AST.h"
#include "../types/HavelType.hpp"
#include "../common/Debug.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace havel::semantic {

/**
 * Symbol table entry representing a variable, function, or type
 */
struct Symbol {
  enum class Kind {
    Variable,
    Function,
    Parameter,
    Struct,
    Enum,
    Trait,
    Builtin
  };

  std::string name;
  Kind kind;
  std::shared_ptr<HavelType> type;
  size_t scopeLevel;
  bool isMutable;
  bool isInitialized;

  Symbol() = default;
  Symbol(const std::string& name, Kind kind, std::shared_ptr<HavelType> type,
         size_t scopeLevel, bool isMutable = true, bool isInitialized = false)
    : name(name), kind(kind), type(type), scopeLevel(scopeLevel),
      isMutable(isMutable), isInitialized(isInitialized) {}
};

/**
 * Symbol table for tracking identifiers and their types
 */
class SymbolTable {
public:
  SymbolTable() = default;

  void enterScope() { scopeLevel_++; }
  void exitScope() {
    if (scopeLevel_ > 0) {
      for (auto it = symbols_.begin(); it != symbols_.end();) {
        if (it->second.scopeLevel == scopeLevel_) {
          it = symbols_.erase(it);
        } else {
          ++it;
        }
      }
      scopeLevel_--;
    }
  }

  bool define(const Symbol& symbol) {
    auto it = symbols_.find(symbol.name);
    if (it != symbols_.end() && it->second.scopeLevel == scopeLevel_) {
      return false;
    }
    symbols_[symbol.name] = symbol;
    return true;
  }

  const Symbol* lookup(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
      return &it->second;
    }
    return nullptr;
  }

  bool isDefined(const std::string& name) const {
    return symbols_.find(name) != symbols_.end();
  }

  size_t getScopeLevel() const { return scopeLevel_; }
  const std::unordered_map<std::string, Symbol>& getSymbols() const { return symbols_; }

private:
  std::unordered_map<std::string, Symbol> symbols_;
  size_t scopeLevel_ = 0;
};

/**
 * Semantic analysis error
 */
struct SemanticError {
  enum class Kind {
    UndefinedVariable,
    UndefinedFunction,
    UndefinedType,
    TypeMismatch,
    DuplicateDefinition,
    MissingField
  };

  Kind kind;
  std::string message;
  size_t line;
  size_t column;

  SemanticError(Kind kind, const std::string& message, size_t line, size_t column)
    : kind(kind), message(message), line(line), column(column) {}
};

/**
 * Semantic Analyzer - performs type checking and symbol table construction
 */
class SemanticAnalyzer {
public:
  SemanticAnalyzer();
  ~SemanticAnalyzer() = default;

  bool analyze(const ast::Program& program);
  const std::vector<SemanticError>& getErrors() const { return errors_; }
  const SymbolTable& getSymbolTable() const { return symbolTable_; }
  TypeRegistry& getTypeRegistry() { return TypeRegistry::getInstance(); }

  // Phase 1: Type Registration
  void registerStructTypes(const ast::Program& program);
  void registerEnumTypes(const ast::Program& program);

  // Phase 2: Type Validation
  void validateTypeAnnotations(const ast::Program& program);

  // Phase 3: Symbol Table Construction
  void buildSymbolTable(const ast::Program& program);

  // Phase 4: Type Inference
  void inferTypes(const ast::Program& program);

  // Phase 5: Function Signature Checking
  void checkFunctionSignatures(const ast::Program& program);

  // Utility
  bool isValidType(const std::string& typeName) const;
  void reportError(SemanticError::Kind kind, const std::string& message,
                   size_t line, size_t column);

private:
  SymbolTable symbolTable_;
  std::vector<SemanticError> errors_;

  void visitStatement(const ast::Statement& stmt);
  void visitExpression(const ast::Expression& expr);
  std::shared_ptr<HavelType> resolveType(const ast::TypeAnnotation& annotation);
  std::shared_ptr<HavelType> resolveTypeDefinition(const ast::TypeDefinition& typeDef);
  std::shared_ptr<HavelType> inferExpressionType(const ast::Expression& expr);
};

} // namespace havel::semantic
