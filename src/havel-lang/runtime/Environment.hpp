/*
 * Environment.hpp - Completely stubbed (interpreter removed)
 *
 * Was used for interpreter variable scoping.
 * VM uses stack/registers instead.
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace havel {

// Completely stubbed - interpreter removed
class Environment {
public:
  Environment(std::shared_ptr<Environment> = nullptr) {}

  // All methods stubbed - no HavelValue usage
  void Define(const std::string&, bool = false) {}
  bool Get(const std::string&) const { return false; }
  bool Assign(const std::string&, bool) { return false; }
  bool IsConst(const std::string&) const { return false; }

private:
  std::weak_ptr<Environment> parent;
  std::unordered_map<std::string, bool> values;  // Stub type
  std::unordered_set<std::string> constVars;
};

} // namespace havel
