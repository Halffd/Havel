/*
 * SymbolTable.cpp
 *
 * Symbol table implementation.
 */
#include "SymbolTable.hpp"
#include <sstream>

namespace havel::semantic {

SymbolTable::SymbolTable() {
    // Enter global scope
    enterScope("global");
}

void SymbolTable::enterScope(const std::string& name) {
    scopes_.emplace_back(currentScopeLevel_, name);
    currentScopeLevel_++;
}

void SymbolTable::exitScope() {
    if (scopes_.size() > 1) {  // Keep global scope
        scopes_.pop_back();
        currentScopeLevel_--;
    }
}

bool SymbolTable::define(const Symbol& symbol) {
    // Check for duplicate in current scope only
    auto& symbols = symbols_[symbol.name];
    for (const auto& existing : symbols) {
        if (existing.scopeLevel == currentScopeLevel_) {
            return false;  // Duplicate in current scope
        }
    }
    
    symbols.push_back(symbol);
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
    auto it = symbols_.find(name);
    if (it == symbols_.end() || it->second.empty()) {
        return nullptr;
    }
    
    // Find symbol with highest scopeLevel <= currentScopeLevel
    // This implements proper lexical scoping with shadowing
    const Symbol* best = nullptr;
    for (const auto& sym : it->second) {
        if (sym.scopeLevel <= currentScopeLevel_) {
            if (!best || sym.scopeLevel > best->scopeLevel) {
                best = &sym;
            }
        }
    }
    
    return best;
}

const Symbol* SymbolTable::lookupInCurrentScope(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it == symbols_.end() || it->second.empty()) {
        return nullptr;
    }
    
    // Find symbol in current scope only
    for (const auto& sym : it->second) {
        if (sym.scopeLevel == currentScopeLevel_) {
            return &sym;
        }
    }
    
    return nullptr;
}

std::shared_ptr<HavelType> SymbolTable::lookupType(const std::string& name) const {
    const Symbol* sym = lookup(name);
    if (sym && sym->isType()) {
        return sym->attributes.type;
    }
    return nullptr;
}

std::vector<const Symbol*> SymbolTable::getAllInCurrentScope() const {
    std::vector<const Symbol*> result;
    
    for (const auto& [name, symbols] : symbols_) {
        for (const auto& sym : symbols) {
            if (sym.scopeLevel == currentScopeLevel_) {
                result.push_back(&sym);
                break;  // Only one symbol per name in current scope
            }
        }
    }
    
    return result;
}

std::vector<const Symbol*> SymbolTable::getAllSymbols() const {
    std::vector<const Symbol*> result;
    
    for (const auto& [name, symbols] : symbols_) {
        if (!symbols.empty()) {
            // Return innermost symbol for each name
            const Symbol* best = nullptr;
            for (const auto& sym : symbols) {
                if (!best || sym.scopeLevel > best->scopeLevel) {
                    best = &sym;
                }
            }
            result.push_back(best);
        }
    }
    
    return result;
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
    // Same type is always compatible
    if (expected.getKind() == actual.getKind()) {
        return TypeCompatibility::Compatible;
    }
    
    // Any type accepts everything
    if (expected.getKind() == HavelType::Kind::Any ||
        actual.getKind() == HavelType::Kind::Any) {
        return TypeCompatibility::Compatible;
    }
    
    // Numeric conversions
    if (isNumericType(expected) && isNumericType(actual)) {
        return TypeCompatibility::ImplicitConvertible;
    }
    
    // Bool to Num conversion
    if (isNumericType(expected) && isBoolType(actual)) {
        return TypeCompatibility::ImplicitConvertible;
    }
    
    // Num to String conversion
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
        return true;  // Untyped variable accepts anything
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
    
    // Check parameter count
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
    
    // TODO: Check individual parameter types when we have full signature info
    
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
