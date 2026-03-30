/*
 * SymbolTable.hpp
 *
 * Symbol table with O(1) lookup using shadow stack design.
 * Based on Lua/early GCC approach - perfect for scripting languages.
 */
#pragma once

#include "../types/HavelType.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::semantic {

/**
 * Symbol categories
 */
enum class SymbolKind {
  Variable,
  Constant,
  Function,
  Parameter,
  Struct,
  Enum,
  Trait,
  Builtin,
  Field,
  Variant,
  Label,
  Signal
};

/**
 * Storage class for variables
 */
enum class StorageClass { Automatic, Static, Extern, Register, Constant };

/**
 * Parameter passing mechanism
 */
enum class ParamPass { ByValue, ByRef, ByConstRef, Out };

/**
 * Symbol attributes
 */
struct SymbolAttributes {
  std::shared_ptr<HavelType> type;
  int64_t address = -1;
  size_t size = 0;
  size_t alignment = 1;
  bool isArray = false;
  std::vector<int64_t> dimensions;
  int64_t arrayBase = 0;
  std::optional<std::string> constValue;
  size_t paramCount = 0;
  std::vector<ParamPass> paramPassModes;
  bool isPublic = true;
  bool isMutable = true;
  bool isInitialized = false;
  StorageClass storageClass = StorageClass::Automatic;
  size_t line = 0;
  size_t column = 0;
  std::unordered_map<std::string, std::string> metadata;
};

/**
 * Symbol with shadow link for O(1) lookup
 */
struct Symbol {
  std::string name;
  SymbolKind kind;
  SymbolAttributes attributes;
  size_t scopeLevel;
  Symbol *previousBinding = nullptr; // Shadow link for restoration

  Symbol() : kind(SymbolKind::Variable), scopeLevel(0) {}

  Symbol(const std::string &name, SymbolKind kind,
         std::shared_ptr<HavelType> type, size_t scopeLevel)
      : name(name), kind(kind), scopeLevel(scopeLevel) {
    attributes.type = type;
  }

  bool isFunction() const { return kind == SymbolKind::Function; }
  bool isVariable() const { return kind == SymbolKind::Variable; }
  bool isConstant() const { return kind == SymbolKind::Constant; }
  bool isParameter() const { return kind == SymbolKind::Parameter; }
  bool isType() const {
    return kind == SymbolKind::Struct || kind == SymbolKind::Enum ||
           kind == SymbolKind::Trait;
  }

  std::string toString() const;
};

/**
 * Symbol Table with O(1) lookup using shadow stack
 *
 * Design:
 * - Single hash table for all symbols
 * - Shadow links track previous bindings
 * - Scope markers track scope boundaries
 * - Lookup is direct table[name] - O(1)
 * - Exit scope restores previous bindings via shadow links
 */
class SymbolTable {
public:
  SymbolTable();
  ~SymbolTable() = default;

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;
  SymbolTable(SymbolTable &&) = default;
  SymbolTable &operator=(SymbolTable &&) = default;

  // Scope management
  void enterScope(const std::string &name = "");
  void exitScope();
  size_t getCurrentScopeLevel() const { return currentScopeLevel_; }

  // Symbol operations - all O(1)
  bool define(const Symbol &symbol);
  bool define(const std::string &name, SymbolKind kind,
              std::shared_ptr<HavelType> type,
              const SymbolAttributes &attrs = SymbolAttributes());

  // Lookup - O(1) direct hash table access
  const Symbol *lookup(const std::string &name) const;
  const Symbol *lookupInCurrentScope(const std::string &name) const;

  // Type lookup
  std::shared_ptr<HavelType> lookupType(const std::string &name) const;

  // Statistics
  size_t getSymbolCount() const { return symbols_.size(); }
  size_t getScopeCount() const { return scopeMarkers_.size(); }

private:
  // Main symbol table - O(1) lookup
  std::unordered_map<std::string, Symbol *> symbols_;

  // Shadow stack - tracks all defined symbols for scope exit
  std::vector<Symbol *> scopeStack_;

  // Scope markers - track scope boundaries in shadow stack
  std::vector<size_t> scopeMarkers_;

  // Current scope level
  size_t currentScopeLevel_ = 0;

  // Memory address allocation
  int64_t nextAddress_ = 0;

  // Symbol storage (arena-style)
  std::vector<std::unique_ptr<Symbol>> symbolStorage_;
};

/**
 * Type compatibility result
 */
enum class TypeCompatibility {
  Compatible,
  ImplicitConvertible,
  ExplicitConvertible,
  Incompatible
};

/**
 * Type checker
 */
class TypeChecker {
public:
  static TypeChecker &getInstance() {
    static TypeChecker instance;
    return instance;
  }

  TypeCompatibility checkCompatibility(const HavelType &expected,
                                       const HavelType &actual,
                                       std::string *errorMsg = nullptr) const;

  bool canAssign(const Symbol &var, const HavelType &valueType,
                 std::string *errorMsg = nullptr) const;

  bool validateCall(const Symbol &func, const std::vector<HavelType> &argTypes,
                    std::string *errorMsg = nullptr) const;

  std::vector<std::string> getImplicitConversions() const {
    return {"Num → Str", "Bool → Num", "Int → Double"};
  }

private:
  TypeChecker() = default;
  bool isNumericType(const HavelType &t) const;
  bool isStringType(const HavelType &t) const;
  bool isBoolType(const HavelType &t) const;
};

} // namespace havel::semantic
