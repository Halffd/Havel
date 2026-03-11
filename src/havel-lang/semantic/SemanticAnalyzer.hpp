/*
 * SemanticAnalyzer.hpp
 *
 * Enhanced semantic analysis with full validation rules.
 */
#pragma once

#include "../ast/AST.h"
#include "SymbolTable.hpp"
#include "../types/HavelType.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace havel::semantic {

/**
 * Semantic analysis mode
 */
enum class SemanticMode {
    None,        // Skip semantic analysis (AHK-style, runtime errors only)
    Basic,       // Check variables, functions, duplicates (no module checking)
    Strict       // Full checking including modules (default)
};

/**
 * Semantic error types
 */
enum class SemanticErrorKind {
    // Declaration errors
    UndefinedVariable,
    UndefinedFunction,
    UndefinedType,
    DuplicateDefinition,
    ForwardReference,
    
    // Module errors (only checked in Strict mode)
    UndefinedModule,
    UndefinedModuleFunction,
    UndefinedBuiltin,
    
    // Type errors
    TypeMismatch,
    InvalidAssignment,
    InvalidOperation,
    MissingReturnType,
    WrongArgumentCount,
    WrongArgumentType,
    
    // Scope errors
    OutOfScope,
    ShadowingWarning,
    
    // Control flow errors
    ReturnOutsideFunction,
    BreakOutsideLoop,
    ContinueOutsideLoop,
    
    // Member access errors
    UnknownField,
    UnknownMethod,
    PrivateAccess,
    
    // Initialization errors
    UninitializedVariable,
    ConstReassignment,
    
    // Procedure call errors
    ProcedureCalledAsFunction,
    FunctionCalledAsProcedure,
    MissingArguments
};

/**
 * Semantic error
 */
struct SemanticError {
    SemanticErrorKind kind;
    std::string message;
    size_t line;
    size_t column;
    std::string context;  // Additional context
    
    SemanticError(SemanticErrorKind k, const std::string& msg, 
                  size_t l, size_t c, const std::string& ctx = "")
        : kind(k), message(msg), line(l), column(c), context(ctx) {}
};

/**
 * Semantic Analysis Context
 */
struct AnalysisContext {
    bool inFunction = false;
    bool inLoop = false;
    bool inSwitch = false;
    const Symbol* currentFunction = nullptr;
    std::shared_ptr<HavelType> expectedReturnType;
};

/**
 * Enhanced Semantic Analyzer
 * 
 * Performs comprehensive semantic analysis:
 * 1. Type registration (structs, enums, traits)
 * 2. Symbol table construction with scoping
 * 3. Type checking and inference
 * 4. Semantic validation rules
 * 5. Control flow analysis
 */
class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    ~SemanticAnalyzer() = default;
    
    /**
     * Set semantic analysis mode
     * None = skip analysis (AHK-style)
     * Basic = check variables/functions only
     * Strict = full checking (default)
     */
    void setMode(SemanticMode mode) { mode_ = mode; }
    SemanticMode getMode() const { return mode_; }
    
    /**
     * Analyze AST and collect semantic information
     */
    bool analyze(const ast::Program& program);
    
    /**
     * Get collected errors
     */
    const std::vector<SemanticError>& getErrors() const { return errors_; }
    
    /**
     * Get symbol table
     */
    SymbolTable& getSymbolTable() { return symbolTable_; }
    const SymbolTable& getSymbolTable() const { return symbolTable_; }
    
    /**
     * Get type registry
     */
    TypeRegistry& getTypeRegistry() { return TypeRegistry::getInstance(); }
    
    // =========================================================================
    // Phase 1: Type Registration
    // =========================================================================
    void registerStructTypes(const ast::Program& program);
    void registerEnumTypes(const ast::Program& program);
    void registerTraitTypes(const ast::Program& program);
    
    // =========================================================================
    // Phase 2: Symbol Table Construction
    // =========================================================================
    void buildSymbolTable(const ast::Program& program);
    void visitStatement(const ast::Statement& stmt);
    void visitExpression(const ast::Expression& expr);
    
    // =========================================================================
    // Phase 3: Type Checking
    // =========================================================================
    void checkTypes(const ast::Program& program);
    std::shared_ptr<HavelType> inferType(const ast::Expression& expr);
    TypeCompatibility checkTypeCompatibility(
        const HavelType& expected, 
        const HavelType& actual,
        size_t line, size_t column);
    
    // =========================================================================
    // Phase 4: Semantic Validation
    // =========================================================================
    void validateAssignments(const ast::Program& program);
    void validateFunctionCalls(const ast::Program& program);
    void validateControlFlow(const ast::Program& program);
    void validateMemberAccess(const ast::Program& program);
    void validateInitialization(const ast::Program& program);
    
    // =========================================================================
    // Specific Validation Rules (from theory)
    // =========================================================================
    
    /**
     * Rule 1: Differentiate variable name from subroutine name
     */
    void validateSymbolUsage(const Symbol& sym, const ast::ASTNode& usage);
    
    /**
     * Rule 2: Prevent procedure name without argument list
     */
    void validateProcedureCall(const Symbol& proc, const ast::CallExpression& call);
    
    /**
     * Rule 3: Prevent procedure name on left side of assignment
     */
    void validateAssignmentTarget(const ast::Expression& target);
    
    /**
     * Rule 4: Avoid assigning numeral as destination of variable
     * (i.e., prevent: 5 = x)
     */
    void validateAssignmentDestination(const ast::Expression& dest);
    
    /**
     * Rule 5: Type-safe assignments (prevent letter to numeric variable)
     */
    void validateTypedAssignment(const Symbol& var, const HavelType& valueType,
                                 size_t line, size_t column);
    
    /**
     * Rule 6: Constant folding - same address for identical constants
     */
    void optimizeConstants();
    
    /**
     * Rule 7: Validate user-defined type usage
     */
    void validateUserDefinedType(const std::string& typeName, 
                                 const ast::ASTNode& usage);

private:
    SymbolTable symbolTable_;
    std::vector<SemanticError> errors_;
    AnalysisContext context_;
    TypeChecker& typeChecker_;
    SemanticMode mode_ = SemanticMode::Basic;  // Default to Basic (no module checking)
    
    // Constant folding
    std::unordered_map<std::string, int64_t> constantAddresses_;
    int64_t nextConstantAddress_ = 0;
    
    // Known modules and their functions
    std::unordered_map<std::string, std::unordered_set<std::string>> knownModules_;
    std::unordered_set<std::string> knownBuiltins_;
    
    // Initialize known modules and builtins
    void initializeKnownModules();
    void initializeKnownBuiltins();
    
    // Check if a module function exists
    bool isKnownModuleFunction(const std::string& module, const std::string& func) const;
    bool isKnownBuiltin(const std::string& name) const;

    // Helpers
    void reportError(SemanticErrorKind kind, const std::string& message,
                     size_t line, size_t column, const std::string& context = "");

    void enterScope(const std::string& name = "");
    void exitScope();

    // Expression type inference
    std::shared_ptr<HavelType> inferLiteralType(const ast::Expression& expr);
    std::shared_ptr<HavelType> inferBinaryType(const ast::BinaryExpression& expr);
    std::shared_ptr<HavelType> inferCallType(const ast::CallExpression& expr);
};

} // namespace havel::semantic
