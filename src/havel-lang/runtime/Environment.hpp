/*
 * Environment.hpp - Stubbed (interpreter removed)
 * 
 * Was used for interpreter variable scoping.
 * VM uses stack/registers instead.
 */
#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace havel {

// Forward declare for backward compatibility
struct HavelValue;

// Stubbed - interpreter removed
class Environment {
public:
  Environment(std::shared_ptr<Environment> = nullptr) {}
  
  void Define(const std::string&, const HavelValue&, bool = false) {}
  std::optional<HavelValue> Get(const std::string&) const { return std::nullopt; }
  bool Assign(const std::string&, const HavelValue&) { return false; }
  bool IsConst(const std::string&) const { return false; }
  
private:
  std::weak_ptr<Environment> parent;
  std::unordered_map<std::string, HavelValue> values;
  std::unordered_set<std::string> constVars;
};

} // namespace havel
