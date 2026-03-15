/*
 * Environment.hpp
 * 
 * Variable scoping and environment management for Havel interpreter.
 * 
 * Note: This header includes Interpreter.hpp because Environment needs
 * the full HavelValue definition. This is a stepping stone toward
 * better organization - in the future, HavelValue should be moved to
 * a separate Value.hpp that both this file and Interpreter.hpp can include.
 */
#pragma once

#include "Interpreter.hpp"  // For HavelValue
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace havel {

/**
 * Environment - Variable scoping for interpreter
 * 
 * Supports:
 * - Nested scopes via parent references
 * - Const variable tracking
 * - Variable assignment and lookup
 */
class Environment {
public:
  Environment(std::shared_ptr<Environment> parentEnv = nullptr)
      : parent(parentEnv) {}

  void Define(const std::string &name, const HavelValue &value, bool isConst = false) {
    values[name] = value;
    if (isConst) {
      constVars.insert(name);
    }
  }

  std::optional<HavelValue> Get(const std::string &name) const {
    auto it = values.find(name);
    if (it != values.end()) {
      return it->second;
    }
    if (parent) {
      return parent->Get(name);
    }
    return std::nullopt;
  }

  bool Assign(const std::string &name, const HavelValue &value) {
    // Check if this is a const variable
    if (constVars.find(name) != constVars.end()) {
      return false; // Cannot assign to const
    }

    auto it = values.find(name);
    if (it != values.end()) {
      values[name] = value;
      return true;
    }
    if (parent) {
      return parent->Assign(name, value);
    }
    return false; // Variable not found
  }

  bool IsConst(const std::string &name) const {
    return constVars.find(name) != constVars.end();
  }

  // Clear all variables (call on shutdown to free memory)
  void clear() {
    values.clear();
    constVars.clear();
    parent.reset();
  }

private:
  std::shared_ptr<Environment> parent;
  std::unordered_map<std::string, HavelValue> values;
  std::unordered_set<std::string> constVars;
};

/**
 * TraitImpl - Runtime trait implementation record
 */
struct TraitImpl {
  std::string traitName;
  std::string typeName;
  std::unordered_map<std::string, HavelValue> methods;  // method name -> bound function
};

/**
 * TraitRegistry - Tracks which types implement which traits
 */
class TraitRegistry {
public:
  static TraitRegistry& getInstance() {
    static TraitRegistry instance;
    return instance;
  }

  // Register an impl block - injects methods into type's method map
  void registerImpl(const std::string& traitName, const std::string& typeName,
                    std::unordered_map<std::string, HavelValue> methods);

  // Check if a type implements a trait
  bool implements(const std::string& typeName, const std::string& traitName) const;

  // Get all trait impls for a type
  std::vector<const TraitImpl*> getImplsForType(const std::string& typeName) const;

  // Get a trait method for a type
  HavelValue getMethod(const std::string& typeName, const std::string& traitName,
                       const std::string& methodName) const;

private:
  TraitRegistry() = default;

  // typeName -> list of trait impls
  std::unordered_map<std::string, std::vector<TraitImpl>> typeImpls;

  // (typeName, traitName) -> impl for quick lookup
  std::unordered_map<std::string, std::unordered_map<std::string, TraitImpl>> implMap;
};

} // namespace havel
