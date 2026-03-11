/*
 * SymbolTable.cpp
 *
 * O(1) symbol lookup using shadow stack design.
 */
#include "SymbolTable.hpp"
#include <sstream>

namespace havel::semantic {

SymbolTable::SymbolTable() {
    enterScope("global");
}

void SymbolTable::enterScope(const std::string& name) {
    (void)name;  // Scope names for debugging only
    scopeMarkers_.push_back(scopeStack_.size());
    currentScopeLevel_++;
}

void SymbolTable::exitScope() {
    if (scopeMarkers_.empty()) return;
    
    // Pop symbols back to the marker, restoring previous bindings
    size_t marker = scopeMarkers_.back();
    while (scopeStack_.size() > marker) {
        Symbol* sym = scopeStack_.back();
        scopeStack_.pop_back();
        
        // Restore previous binding or remove from table
        if (sym->previousBinding) {
            symbols_[sym->name] = sym->previousBinding;
        } else {
            symbols_.erase(sym->name);
        }
    }
    
    scopeMarkers_.pop_back();
    currentScopeLevel_--;
}

bool SymbolTable::define(const Symbol& symbol) {
    // Check for duplicate in current scope only
    auto it = symbols_.find(symbol.name);
    if (it != symbols_.end() && it->second->scopeLevel == currentScopeLevel_) {
        return false;  // Duplicate in current scope
    }

    // Allocate symbol in arena
    auto newSym = std::make_unique<Symbol>(symbol);
    newSym->scopeLevel = currentScopeLevel_;

    // Set up shadow link
    if (it != symbols_.end()) {
        newSym->previousBinding = it->second;
    }

    // Insert into table and shadow stack
    Symbol* symPtr = newSym.get();
    symbols_[symbol.name] = symPtr;
    scopeStack_.push_back(symPtr);
    symbolStorage_.push_back(std::move(newSym));

    return true;
}

bool SymbolTable::define(const std::string& name, SymbolKind kind,
                         std::shared_ptr<HavelType> type,
                         const SymbolAttributes& attrs) {
    Symbol symbol(name, kind, type, currentScopeLevel_);
    symbol.attributes = attrs;
    symbol.attributes.line = attrs.line;
    symbol.attributes.column = attrs.column;
    return define(symbol);
}

const Symbol* SymbolTable::lookup(const std::string& name) const {
    // O(1) direct hash table lookup
    auto it = symbols_.find(name);
    if (it == symbols_.end()) {
        return nullptr;
    }

    // Return the innermost visible symbol
    const Symbol* sym = it->second;
    if (sym->scopeLevel <= currentScopeLevel_) {
        return sym;
    }

    // Symbol is in a deeper scope (shouldn't happen in normal use)
    return nullptr;
}

const Symbol* SymbolTable::lookupInCurrentScope(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it == symbols_.end() || it->second->scopeLevel != currentScopeLevel_) {
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<HavelType> SymbolTable::lookupType(const std::string& name) const {
    const Symbol* sym = lookup(name);
    if (sym && sym->isType()) {
        return sym->attributes.type;
    }
    return nullptr;
}

std::string Symbol::toString() const {
    std::ostringstream oss;
    oss << name << " [";
    
    switch (kind) {
        case SymbolKind::Variable: oss << "var"; break;
        case SymbolKind::Constant: oss << "const"; break;
        case SymbolKind::Function: oss << "func"; break;
        case SymbolKind::Parameter: oss << "param"; break;
        case SymbolKind::Struct: oss << "struct"; break;
        case SymbolKind::Enum: oss << "enum"; break;
        case SymbolKind::Trait: oss << "trait"; break;
        case SymbolKind::Builtin: oss << "builtin"; break;
        case SymbolKind::Field: oss << "field"; break;
        case SymbolKind::Variant: oss << "variant"; break;
        case SymbolKind::Label: oss << "label"; break;
    }
    
    if (attributes.type) {
        oss << ": " << attributes.type->toString();
    }
    if (attributes.address >= 0) {
        oss << " @0x" << std::hex << attributes.address;
    }
    if (attributes.size > 0) {
        oss << " (" << std::dec << attributes.size << " bytes)";
    }
    oss << " scope:" << scopeLevel;
    if (!attributes.isMutable) oss << " immutable";
    if (attributes.isInitialized) oss << " initialized";
    oss << "]";
    
    return oss.str();
}

// ============================================================================
// TypeChecker Implementation
// ============================================================================

TypeCompatibility TypeChecker::checkCompatibility(
    const HavelType& expected,
    const HavelType& actual,
    std::string* errorMsg) const 
{
    if (expected.getKind() == actual.getKind()) {
        return TypeCompatibility::Compatible;
    }
    
    if (expected.getKind() == HavelType::Kind::Any ||
        actual.getKind() == HavelType::Kind::Any) {
        return TypeCompatibility::Compatible;
    }
    
    if (isNumericType(expected) && isNumericType(actual)) {
        return TypeCompatibility::ImplicitConvertible;
    }
    
    if (isNumericType(expected) && isBoolType(actual)) {
        return TypeCompatibility::ImplicitConvertible;
    }
    
    if (isStringType(expected) && isNumericType(actual)) {
        return TypeCompatibility::ImplicitConvertible;
    }
    
    if (errorMsg) {
        *errorMsg = "Type mismatch: expected " + expected.toString() + 
                    ", got " + actual.toString();
    }
    
    return TypeCompatibility::Incompatible;
}

bool TypeChecker::canAssign(const Symbol& var, const HavelType& valueType,
                            std::string* errorMsg) const {
    if (!var.attributes.type) {
        return true;
    }
    
    if (!var.attributes.isMutable) {
        if (errorMsg) {
            *errorMsg = "Cannot assign to immutable variable '" + var.name + "'";
        }
        return false;
    }
    
    auto compat = checkCompatibility(*var.attributes.type, valueType, errorMsg);
    return compat != TypeCompatibility::Incompatible;
}

bool TypeChecker::validateCall(const Symbol& func,
                               const std::vector<HavelType>& argTypes,
                               std::string* errorMsg) const {
    if (!func.isFunction()) {
        if (errorMsg) {
            *errorMsg = "'" + func.name + "' is not a function";
        }
        return false;
    }
    
    if (argTypes.size() != func.attributes.paramCount) {
        if (errorMsg) {
            std::ostringstream oss;
            oss << "Function '" << func.name << "' expects " 
                << func.attributes.paramCount << " arguments, got "
                << argTypes.size();
            *errorMsg = oss.str();
        }
        return false;
    }
    
    return true;
}

bool TypeChecker::isNumericType(const HavelType& t) const {
    return t.getKind() == HavelType::Kind::Num;
}

bool TypeChecker::isStringType(const HavelType& t) const {
    return t.getKind() == HavelType::Kind::Str;
}

bool TypeChecker::isBoolType(const HavelType& t) const {
    return t.getKind() == HavelType::Kind::Bool;
}

} // namespace havel::semantic
