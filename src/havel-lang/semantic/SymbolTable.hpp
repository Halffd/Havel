/*
 * SymbolTable.hpp
 *
 * Enhanced symbol table implementation for Havel language.
 * Supports scoping, type attributes, memory addresses, and semantic validation.
 */
#pragma once

#include "../types/HavelType.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <cstdint>

namespace havel::semantic {

/**
 * Symbol categories
 */
enum class SymbolKind {
    Variable,       // Regular variable
    Constant,       // Constant value
    Function,       // Function/procedure
    Parameter,      // Function parameter
    Struct,         // Struct type definition
    Enum,           // Enum type definition
    Trait,          // Trait/interface definition
    Builtin,        // Built-in function
    Field,          // Struct field
    Variant,        // Enum variant
    Label           // Jump label
};

/**
 * Storage class for variables
 */
enum class StorageClass {
    Automatic,      // Stack variable (default)
    Static,         // Static storage
    Extern,         // External linkage
    Register,       // Hint for register allocation
    Constant        // Compile-time constant
};

/**
 * Parameter passing mechanism
 */
enum class ParamPass {
    ByValue,        // Pass by value (default)
    ByRef,          // Pass by reference
    ByConstRef,     // Pass by const reference
    Out             // Output parameter
};

/**
 * Symbol attributes
 */
struct SymbolAttributes {
    // Type information
    std::shared_ptr<HavelType> type;
    
    // Memory layout (for code generation)
    int64_t address = -1;           // Memory address or offset
    size_t size = 0;                // Size in bytes
    size_t alignment = 1;           // Alignment requirement
    
    // Array/vector information
    bool isArray = false;
    std::vector<int64_t> dimensions;  // Array dimensions
    int64_t arrayBase = 0;            // Base index (for 0-based or 1-based)
    
    // Constant value (for constants)
    std::optional<std::string> constValue;
    
    // Function information
    std::vector<ParamPass> paramPassModes;  // Parameter passing modes
    size_t paramCount = 0;                   // Number of parameters
    
    // Visibility and linkage
    bool isPublic = true;             // Exported from module
    bool isMutable = true;            // Can be modified
    bool isInitialized = false;       // Has been assigned
    StorageClass storageClass = StorageClass::Automatic;
    
    // Source location for error reporting
    size_t line = 0;
    size_t column = 0;
    
    // Additional metadata
    std::unordered_map<std::string, std::string> metadata;
};

/**
 * Symbol table entry
 */
struct Symbol {
    std::string name;
    SymbolKind kind;
    SymbolAttributes attributes;
    size_t scopeLevel;
    size_t scopeId;
    
    Symbol() : kind(SymbolKind::Variable), scopeLevel(0), scopeId(0) {}
    
    Symbol(const std::string& name, SymbolKind kind, 
           std::shared_ptr<HavelType> type, size_t scopeLevel, size_t scopeId)
        : name(name), kind(kind), scopeLevel(scopeLevel), scopeId(scopeId) {
        attributes.type = type;
    }
    
    // Quick accessors
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
 * Scope information
 */
struct Scope {
    size_t id;
    size_t level;
    Scope* parent;
    std::string name;  // Optional: function name, block name, etc.
    
    Scope(size_t id, size_t level, Scope* parent, const std::string& name = "")
        : id(id), level(level), parent(parent), name(name) {}
};

/**
 * Symbol Table with scope support
 * 
 * Uses hash map for O(1) average lookup, with scope stack for nesting.
 * Supports symbol shadowing in nested scopes.
 */
class SymbolTable {
public:
    SymbolTable();
    ~SymbolTable();
    
    // Disable copying to prevent double-free
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;
    
    // Allow moving
    SymbolTable(SymbolTable&&) = default;
    SymbolTable& operator=(SymbolTable&&) = default;
    
    // Scope management
    void enterScope(const std::string& name = "");
    void exitScope();
    size_t getCurrentScopeLevel() const { return currentScopeLevel_; }
    Scope* getCurrentScope() { return currentScope_; }
    
    // Symbol insertion
    bool define(const Symbol& symbol);
    bool define(const std::string& name, SymbolKind kind, 
                std::shared_ptr<HavelType> type,
                const SymbolAttributes& attrs = SymbolAttributes());
    
    // Symbol lookup (returns innermost matching symbol)
    const Symbol* lookup(const std::string& name) const;
    const Symbol* lookupInCurrentScope(const std::string& name) const;
    const Symbol* lookupInParentScope(const std::string& name) const;
    
    // Type lookup (convenience for type symbols)
    std::shared_ptr<HavelType> lookupType(const std::string& name) const;
    
    // All symbols in current scope
    std::vector<const Symbol*> getAllInCurrentScope() const;
    
    // All symbols (for debugging)
    std::vector<const Symbol*> getAllSymbols() const;
    
    // Statistics
    size_t getSymbolCount() const { return symbols_.size(); }
    size_t getScopeCount() const { return scopes_.size(); }
    
    // Memory address allocation
    int64_t allocateAddress(size_t size, size_t alignment = 1);
    void resetAddressCounter() { nextAddress_ = 0; }
    
    // Validation
    std::vector<std::string> validate() const;

private:
    // All symbols, keyed by name + scope
    std::unordered_map<std::string, std::vector<Symbol>> symbols_;

    // Scope storage (raw pointers, manually managed)
    std::vector<Scope*> scopeStorage_;
    
    // Scope stack (non-owning pointers)
    std::vector<Scope*> scopes_;
    Scope* currentScope_ = nullptr;
    size_t currentScopeLevel_ = 0;
    size_t nextScopeId_ = 0;

    // Memory address allocation
    int64_t nextAddress_ = 0;

    // Helper to create unique key
    static std::string makeKey(const std::string& name, size_t scopeId);
};

/**
 * Type compatibility result
 */
enum class TypeCompatibility {
    Compatible,           // Types match exactly
    ImplicitConvertible,  // Can convert implicitly (e.g., int → double)
    ExplicitConvertible,  // Requires explicit cast
    Incompatible          // Cannot convert
};

/**
 * Type checker - validates type compatibility
 */
class TypeChecker {
public:
    static TypeChecker& getInstance() {
        static TypeChecker instance;
        return instance;
    }
    
    /**
     * Check if two types are compatible
     */
    TypeCompatibility checkCompatibility(
        const HavelType& expected,
        const HavelType& actual,
        std::string* errorMsg = nullptr) const;
    
    /**
     * Check if value can be assigned to variable
     */
    bool canAssign(const Symbol& var, const HavelType& valueType,
                   std::string* errorMsg = nullptr) const;
    
    /**
     * Check if function call is valid
     */
    bool validateCall(const Symbol& func, 
                      const std::vector<HavelType>& argTypes,
                      std::string* errorMsg = nullptr) const;
    
    /**
     * Get implicit conversion chain
     */
    std::vector<std::string> getImplicitConversions() const {
        return {"Num → Str", "Bool → Num", "Int → Double"};
    }

private:
    TypeChecker() = default;
    
    bool isNumericType(const HavelType& t) const;
    bool isStringType(const HavelType& t) const;
    bool isBoolType(const HavelType& t) const;
};

} // namespace havel::semantic
