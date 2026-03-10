/*
 * SymbolTable.cpp
 *
 * Enhanced symbol table implementation.
 */
#include "SymbolTable.hpp"
#include <sstream>
#include <algorithm>

namespace havel::semantic {

// ============================================================================
// Symbol implementation
// ============================================================================

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
// SymbolTable implementation
// ============================================================================

SymbolTable::SymbolTable() {
    // Create global scope
    enterScope("global");
}

std::string SymbolTable::makeKey(const std::string& name, size_t scopeId) {
    return name + "#" + std::to_string(scopeId);
}

void SymbolTable::enterScope(const std::string& name) {
    auto* scope = new Scope(
        nextScopeId_++, 
        currentScopeLevel_, 
        currentScope_,
        name
    );
    scopeStorage_.push_back(scope);
    scopes_.push_back(scope);
    currentScope_ = scope;
    currentScopeLevel_++;
}

void SymbolTable::exitScope() {
    if (scopes_.size() > 1) {  // Keep global scope
        scopes_.pop_back();
        currentScope_ = scopes_.back();
        currentScopeLevel_--;
    }
}

SymbolTable::~SymbolTable() {
    // Scopes are raw pointers - clean them up
    for (auto* scope : scopeStorage_) {
        delete scope;
    }
    scopeStorage_.clear();
}

bool SymbolTable::define(const Symbol& symbol) {
    std::string key = makeKey(symbol.name, symbol.scopeId);
    
    // Check for duplicate in same scope
    auto it = symbols_.find(key);
    if (it != symbols_.end()) {
        for (const auto& existing : it->second) {
            if (existing.name == symbol.name) {
                return false;  // Duplicate in same scope
            }
        }
    }
    
    symbols_[key].push_back(symbol);
    return true;
}

bool SymbolTable::define(const std::string& name, SymbolKind kind,
                         std::shared_ptr<HavelType> type,
                         const SymbolAttributes& attrs) {
    Symbol symbol(name, kind, type, currentScopeLevel_, 
                  currentScope_ ? currentScope_->id : 0);
    symbol.attributes = attrs;
    symbol.attributes.line = attrs.line;
    symbol.attributes.column = attrs.column;
    return define(symbol);
}

const Symbol* SymbolTable::lookup(const std::string& name) const {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        Scope* scope = *it;
        std::string key = makeKey(name, scope->id);
        
        auto symIt = symbols_.find(key);
        if (symIt != symbols_.end() && !symIt->second.empty()) {
            return &symIt->second.back();  // Return most recent definition
        }
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupInCurrentScope(const std::string& name) const {
    if (!currentScope_) return nullptr;
    
    std::string key = makeKey(name, currentScope_->id);
    auto it = symbols_.find(key);
    if (it != symbols_.end() && !it->second.empty()) {
        return &it->second.back();
    }
    return nullptr;
}

const Symbol* SymbolTable::lookupInParentScope(const std::string& name) const {
    if (!currentScope_ || !currentScope_->parent) return nullptr;
    
    std::string key = makeKey(name, currentScope_->parent->id);
    auto it = symbols_.find(key);
    if (it != symbols_.end() && !it->second.empty()) {
        return &it->second.back();
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
    if (!currentScope_) return result;
    
    std::string prefix = "#" + std::to_string(currentScope_->id);
    for (const auto& [key, symbols] : symbols_) {
        if (key.find(prefix) != std::string::npos && !symbols.empty()) {
            result.push_back(&symbols.back());
        }
    }
    return result;
}

std::vector<const Symbol*> SymbolTable::getAllSymbols() const {
    std::vector<const Symbol*> result;
    for (const auto& [key, symbols] : symbols_) {
        if (!symbols.empty()) {
            result.push_back(&symbols.back());
        }
    }
    return result;
}

int64_t SymbolTable::allocateAddress(size_t size, size_t alignment) {
    // Align address
    if (alignment > 1) {
        int64_t mask = alignment - 1;
        nextAddress_ = (nextAddress_ + mask) & ~mask;
    }
    
    int64_t addr = nextAddress_;
    nextAddress_ += size;
    return addr;
}

std::vector<std::string> SymbolTable::validate() const {
    std::vector<std::string> errors;
    
    for (const auto* sym : getAllSymbols()) {
        // Check uninitialized variables
        if (sym->isVariable() && !sym->attributes.isInitialized) {
            // Warning only - might be assigned later
        }
        
        // Check function with no parameters listed
        if (sym->isFunction() && sym->attributes.paramCount == 0) {
            // Valid for functions with no parameters
        }
    }
    
    return errors;
}

// ============================================================================
// TypeChecker implementation
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
