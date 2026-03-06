/*
 * Environment.cpp
 * 
 * Implementation of Environment and TraitRegistry classes.
 */
#include "Environment.hpp"

namespace havel {

// TraitRegistry implementation

void TraitRegistry::registerImpl(const std::string& traitName, const std::string& typeName,
                                  std::unordered_map<std::string, HavelValue> methods) {
  TraitImpl impl;
  impl.traitName = traitName;
  impl.typeName = typeName;
  impl.methods = std::move(methods);

  typeImpls[typeName].push_back(impl);
  implMap[typeName][traitName] = impl;
}

bool TraitRegistry::implements(const std::string& typeName, const std::string& traitName) const {
  auto typeIt = implMap.find(typeName);
  if (typeIt != implMap.end()) {
    return typeIt->second.find(traitName) != typeIt->second.end();
  }
  return false;
}

std::vector<const TraitImpl*> TraitRegistry::getImplsForType(const std::string& typeName) const {
  std::vector<const TraitImpl*> result;
  auto typeIt = typeImpls.find(typeName);
  if (typeIt != typeImpls.end()) {
    for (const auto& impl : typeIt->second) {
      result.push_back(&impl);
    }
  }
  return result;
}

HavelValue TraitRegistry::getMethod(const std::string& typeName, const std::string& traitName,
                                     const std::string& methodName) const {
  auto typeIt = implMap.find(typeName);
  if (typeIt != implMap.end()) {
    auto traitIt = typeIt->second.find(traitName);
    if (traitIt != typeIt->second.end()) {
      auto methodIt = traitIt->second.methods.find(methodName);
      if (methodIt != traitIt->second.methods.end()) {
        return methodIt->second;
      }
    }
  }
  return HavelValue(nullptr);
}

} // namespace havel
