/*
 * TraitRegistry.cpp
 *
 * Trait system implementation for Havel language.
 */
#include "Environment.hpp"

namespace havel {

void TraitRegistry::registerImpl(const std::string& traitName, const std::string& typeName,
                                  std::unordered_map<std::string, HavelValue> methods) {
    TraitImpl impl;
    impl.traitName = traitName;
    impl.typeName = typeName;
    impl.methods = std::move(methods);
    
    // Add to typeImpls
    typeImpls[typeName].push_back(impl);
    
    // Add to implMap for quick lookup
    implMap[typeName][traitName] = impl;
}

bool TraitRegistry::implements(const std::string& typeName, const std::string& traitName) const {
    auto typeIt = implMap.find(typeName);
    if (typeIt == implMap.end()) {
        return false;
    }
    
    auto traitIt = typeIt->second.find(traitName);
    return traitIt != typeIt->second.end();
}

std::vector<const TraitImpl*> TraitRegistry::getImplsForType(const std::string& typeName) const {
    std::vector<const TraitImpl*> result;
    
    auto it = typeImpls.find(typeName);
    if (it != typeImpls.end()) {
        for (const auto& impl : it->second) {
            result.push_back(&impl);
        }
    }
    
    return result;
}

HavelValue TraitRegistry::getMethod(const std::string& typeName, const std::string& traitName,
                                     const std::string& methodName) const {
    auto typeIt = implMap.find(typeName);
    if (typeIt == implMap.end()) {
        return HavelValue(nullptr);
    }
    
    auto traitIt = typeIt->second.find(traitName);
    if (traitIt == typeIt->second.end()) {
        return HavelValue(nullptr);
    }
    
    auto methodIt = traitIt->second.methods.find(methodName);
    if (methodIt == traitIt->second.methods.end()) {
        return HavelValue(nullptr);
    }
    
    return methodIt->second;
}

} // namespace havel
