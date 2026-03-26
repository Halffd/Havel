/*
 * SemanticAnalyzer.cpp
 *
 * Enhanced semantic analysis implementation.
 */
#include "SemanticAnalyzer.hpp"
#include <algorithm>
#include <sstream>

namespace havel::semantic {

SemanticAnalyzer::SemanticAnalyzer()
    : typeChecker_(TypeChecker::getInstance()) {
    // Initialize known modules and builtins
    initializeKnownModules();
    initializeKnownBuiltins();
}

bool SemanticAnalyzer::analyze(const ast::Program& program, bool resetSymbols) {
    errors_.clear();
    constantAddresses_.clear();
    nextConstantAddress_ = 0;
    
    // Reset symbol table when requested (keep existing symbols for REPL)
    if (resetSymbols) {
        symbolTable_ = SymbolTable();
    }
    
    // Phase 1: Register all type definitions
    registerStructTypes(program);
    registerEnumTypes(program);
    
    // Phase 2: Build symbol table
    buildSymbolTable(program);
    
    // Phase 3: Type checking
    checkTypes(program);
    
    // Phase 4: Semantic validation
    validateAssignments(program);
    validateFunctionCalls(program);
    validateControlFlow(program);
    validateMemberAccess(program);
    validateInitialization(program);
    
    // Phase 5: Optimization
    optimizeConstants();
    
    return errors_.empty();
}

// ============================================================================
// Phase 1: Type Registration
// ============================================================================

void SemanticAnalyzer::registerStructTypes(const ast::Program& program) {
    auto& registry = getTypeRegistry();
    
    for (const auto& stmt : program.body) {
        if (!stmt || stmt->kind != ast::NodeType::StructDeclaration) continue;
        
        const auto& structDecl = static_cast<const ast::StructDeclaration&>(*stmt);
        auto structType = std::make_shared<HavelStructType>(structDecl.name);
        
        // Register fields
        for (const auto& field : structDecl.definition.fields) {
            std::optional<std::shared_ptr<HavelType>> fieldType;
            if (field.type) {
                fieldType = HavelType::any();  // TODO: Resolve actual type
            }
            structType->addField(StructField(field.name, fieldType));
            
            // Add field to symbol table
            Symbol fieldSym;
            fieldSym.name = field.name;
            fieldSym.kind = SymbolKind::Field;
            fieldSym.attributes.type = fieldType.value_or(HavelType::any());
            fieldSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
            fieldSym.attributes.size = 8;  // Default size
            symbolTable_.define(fieldSym);
        }

        registry.registerStructType(structType);

        // Register struct name in symbol table
        Symbol structSym;
        structSym.name = structDecl.name;
        structSym.kind = SymbolKind::Struct;
        structSym.attributes.type = structType;
        structSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
        symbolTable_.define(structSym);
    }
}

void SemanticAnalyzer::registerEnumTypes(const ast::Program& program) {
    auto& registry = getTypeRegistry();

    for (const auto& stmt : program.body) {
        if (!stmt || stmt->kind != ast::NodeType::EnumDeclaration) continue;

        const auto& enumDecl = static_cast<const ast::EnumDeclaration&>(*stmt);
        auto enumType = std::make_shared<HavelEnumType>(enumDecl.name);

        // Register variants
        for (const auto& variant : enumDecl.definition.variants) {
            std::optional<std::shared_ptr<HavelType>> payloadType;
            bool hasPayload = variant.payloadType.has_value();
            if (hasPayload) {
                payloadType = HavelType::any();  // TODO: Resolve actual type
            }
            enumType->addVariant(EnumVariant(variant.name, hasPayload, payloadType));

            // Add variant to symbol table
            Symbol variantSym;
            variantSym.name = variant.name;
            variantSym.kind = SymbolKind::Variant;
            variantSym.attributes.type = HavelType::any();
            variantSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
            symbolTable_.define(variantSym);
        }

        registry.registerEnumType(enumType);

        // Register enum name in symbol table
        Symbol enumSym;
        enumSym.name = enumDecl.name;
        enumSym.kind = SymbolKind::Enum;
        enumSym.attributes.type = enumType;
        enumSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
        symbolTable_.define(enumSym);
    }
}

void SemanticAnalyzer::registerTraitTypes(const ast::Program& program) {
    // TODO: Implement trait registration
}

// ============================================================================
// Phase 2: Symbol Table Construction
// ============================================================================

void SemanticAnalyzer::buildSymbolTable(const ast::Program& program) {
    // First pass: register all top-level declarations
    for (const auto& stmt : program.body) {
        if (!stmt) continue;
        visitStatement(*stmt);
    }
}

void SemanticAnalyzer::visitStatement(const ast::Statement& stmt) {
    switch (stmt.kind) {
        case ast::NodeType::LetDeclaration: {
            const auto& letDecl = static_cast<const ast::LetDeclaration&>(stmt);

            std::shared_ptr<HavelType> varType = HavelType::any();
            if (letDecl.typeAnnotation) {
                // TODO: Resolve type from annotation
            }

            // Get variable name from pattern
            if (letDecl.pattern && letDecl.pattern->kind == ast::NodeType::Identifier) {
                const auto& ident = static_cast<const ast::Identifier&>(*letDecl.pattern);

                SymbolAttributes attrs;
                attrs.isMutable = !letDecl.isConst;
                attrs.isInitialized = (letDecl.value != nullptr);
                attrs.line = stmt.line;
                attrs.column = stmt.column;

                SymbolKind kind = letDecl.isConst ? SymbolKind::Constant : SymbolKind::Variable;
                
                // Check for duplicate definition
                if (!symbolTable_.define(ident.symbol, kind, varType, attrs)) {
                    reportError(SemanticErrorKind::DuplicateDefinition,
                               "Variable '" + ident.symbol + "' already defined in this scope",
                               stmt.line, stmt.column);
                }
            }

            // Visit initializer
            if (letDecl.value) {
                visitExpression(*letDecl.value);
            }
            break;
        }
        
        case ast::NodeType::FunctionDeclaration: {
            const auto& funcDecl = static_cast<const ast::FunctionDeclaration&>(stmt);
            
            if (funcDecl.name) {
                SymbolAttributes attrs;
                attrs.paramCount = funcDecl.parameters.size();
                attrs.line = stmt.line;
                attrs.column = stmt.column;
                
                symbolTable_.define(funcDecl.name->symbol, SymbolKind::Function, 
                                   HavelType::any(), attrs);
            }
            
            // Process function body in new scope
            enterScope(funcDecl.name ? funcDecl.name->symbol : "anonymous");
            
            // Push function context (supports nested functions)
            context_.functionStack.push_back(context_.inFunction);
            context_.inFunction = true;
            
            // Register parameters
            for (const auto& param : funcDecl.parameters) {
                if (param->paramName) {
                    SymbolAttributes attrs;
                    attrs.isMutable = false;  // Parameters are immutable
                    attrs.isInitialized = true;
                    symbolTable_.define(param->paramName->symbol, SymbolKind::Parameter,
                                       HavelType::any(), attrs);
                }
            }
            
            // Visit function body
            if (funcDecl.body) {
                visitStatement(*funcDecl.body);
            }
            
            // Pop function context
            context_.inFunction = context_.functionStack.back();
            context_.functionStack.pop_back();
            
            exitScope();
            break;
        }
        
        case ast::NodeType::BlockStatement: {
            const auto& block = static_cast<const ast::BlockStatement&>(stmt);
            
            enterScope("block");
            for (const auto& s : block.body) {
                if (s) visitStatement(*s);
            }
            exitScope();
            break;
        }
        
        case ast::NodeType::IfStatement: {
            const auto& ifStmt = static_cast<const ast::IfStatement&>(stmt);
            
            if (ifStmt.condition) {
                visitExpression(*ifStmt.condition);
            }
            if (ifStmt.consequence) {
                visitStatement(*ifStmt.consequence);
            }
            if (ifStmt.alternative) {
                visitStatement(*ifStmt.alternative);
            }
            break;
        }
        
        case ast::NodeType::WhileStatement: {
            const auto& whileStmt = static_cast<const ast::WhileStatement&>(stmt);

            enterScope("while");
            
            if (whileStmt.condition) {
                visitExpression(*whileStmt.condition);
            }
            if (whileStmt.body) {
                // Push loop context (supports nested loops)
                context_.loopDepth++;
                visitStatement(*whileStmt.body);
                context_.loopDepth--;
            }

            exitScope();
            break;
        }

        case ast::NodeType::ForStatement: {
            const auto& forStmt = static_cast<const ast::ForStatement&>(stmt);

            enterScope("for");

            // Register loop variable(s)
            for (const auto& iter : forStmt.iterators) {
                if (iter) {
                    SymbolAttributes attrs;
                    attrs.isMutable = false;
                    attrs.isInitialized = true;
                    symbolTable_.define(iter->symbol, SymbolKind::Variable,
                                       HavelType::any(), attrs);
                }
            }

            if (forStmt.iterable) {
                visitExpression(*forStmt.iterable);
            }
            if (forStmt.body) {
                // Push loop context (supports nested loops)
                context_.loopDepth++;
                visitStatement(*forStmt.body);
                context_.loopDepth--;
            }

            exitScope();
            break;
        }
        
        case ast::NodeType::ReturnStatement: {
            const auto& retStmt = static_cast<const ast::ReturnStatement&>(stmt);
            
            if (!context_.inFunction) {
                reportError(SemanticErrorKind::ReturnOutsideFunction,
                           "return statement outside function",
                           stmt.line, stmt.column);
            }
            
            if (retStmt.argument) {
                visitExpression(*retStmt.argument);
            }
            break;
        }
        
        case ast::NodeType::BreakStatement:
        case ast::NodeType::ContinueStatement: {
            // Check if we're in a loop (loopDepth > 0)
            if (context_.loopDepth == 0) {
                SemanticErrorKind kind = (stmt.kind == ast::NodeType::BreakStatement)
                    ? SemanticErrorKind::BreakOutsideLoop
                    : SemanticErrorKind::ContinueOutsideLoop;
                const char* msg = (stmt.kind == ast::NodeType::BreakStatement)
                    ? "break statement outside loop"
                    : "continue statement outside loop";

                reportError(kind, msg, stmt.line, stmt.column);
            }
            break;
        }
        
        case ast::NodeType::ExpressionStatement: {
            const auto& exprStmt = static_cast<const ast::ExpressionStatement&>(stmt);
            if (exprStmt.expression) {
                visitExpression(*exprStmt.expression);
            }
            break;
        }
        
        default:
            break;
    }
}

void SemanticAnalyzer::visitExpression(const ast::Expression& expr) {
    switch (expr.kind) {
        case ast::NodeType::Identifier: {
            const auto& ident = static_cast<const ast::Identifier&>(expr);
            const Symbol* sym = symbolTable_.lookup(ident.symbol);

            if (!sym) {
                // Reading undefined variable is always an error
                // (implicit declaration only applies to assignments)
                reportError(SemanticErrorKind::UndefinedVariable,
                           "Undefined variable: " + ident.symbol,
                           expr.line, expr.column);
            } else if (sym->kind == SymbolKind::Function) {
                // Using function name without call - might be valid (function pointer)
                // or error depending on context
            }
            break;
        }
        
        case ast::NodeType::AssignmentExpression: {
            const auto& assign = static_cast<const ast::AssignmentExpression&>(expr);
            
            // Validate assignment target (Rule 3 & 4)
            validateAssignmentTarget(*assign.target);
            validateAssignmentDestination(*assign.target);
            
            // Visit right side
            if (assign.value) {
                visitExpression(*assign.value);
            }
            break;
        }
        
        case ast::NodeType::CallExpression: {
            const auto& call = static_cast<const ast::CallExpression&>(expr);

            if (call.callee) {
                if (call.callee->kind == ast::NodeType::Identifier) {
                    const auto& ident = static_cast<const ast::Identifier&>(*call.callee);
                    const Symbol* sym = symbolTable_.lookup(ident.symbol);

                    if (sym && sym->kind == SymbolKind::Function) {
                        validateProcedureCall(*sym, call);
                    }
                    // If not found, treat as global callable (resolved at runtime).
                } else if (call.callee->kind == ast::NodeType::MemberExpression) {
                    const auto& member = static_cast<const ast::MemberExpression&>(*call.callee);
                    if (member.object) {
                        if (member.object->kind == ast::NodeType::Identifier) {
                            const auto& moduleIdent =
                                static_cast<const ast::Identifier&>(*member.object);
                            const Symbol* sym = symbolTable_.lookup(moduleIdent.symbol);
                            if (sym) {
                                visitExpression(*member.object);
                            }
                            // If not found, treat as global module reference.
                        } else {
                            visitExpression(*member.object);
                        }
                    }
                } else {
                    visitExpression(*call.callee);
                }
            }

            for (const auto& arg : call.args) {
                if (arg) visitExpression(*arg);
            }
            break;
        }
        
        case ast::NodeType::MemberExpression: {
            const auto& member = static_cast<const ast::MemberExpression&>(expr);
            
            if (member.object) {
                if (member.object->kind == ast::NodeType::Identifier) {
                    const auto& ident = static_cast<const ast::Identifier&>(*member.object);
                    if (symbolTable_.lookup(ident.symbol)) {
                        visitExpression(*member.object);
                    }
                } else {
                    visitExpression(*member.object);
                }
            }
            // Member validation happens in validateMemberAccess()
            break;
        }
        
        case ast::NodeType::BinaryExpression: {
            const auto& binary = static_cast<const ast::BinaryExpression&>(expr);
            if (binary.left) visitExpression(*binary.left);
            if (binary.right) visitExpression(*binary.right);
            break;
        }
        
        case ast::NodeType::UnaryExpression: {
            const auto& unary = static_cast<const ast::UnaryExpression&>(expr);
            if (unary.operand) visitExpression(*unary.operand);
            break;
        }
        
        default:
            // Literals and other expressions don't need symbol table updates
            break;
    }
}

// ============================================================================
// Phase 3: Type Checking
// ============================================================================

void SemanticAnalyzer::checkTypes(const ast::Program& program) {
    // Type checking is done during visitExpression via inferType()
}

std::shared_ptr<HavelType> SemanticAnalyzer::inferType(const ast::Expression& expr) {
    switch (expr.kind) {
        case ast::NodeType::NumberLiteral:
            return HavelType::num();
        case ast::NodeType::StringLiteral:
            return HavelType::str();
        case ast::NodeType::BooleanLiteral:
            return HavelType::boolean();
        case ast::NodeType::ArrayLiteral:
            return std::make_shared<HavelArrayType>();
        default:
            return HavelType::any();
    }
}

TypeCompatibility SemanticAnalyzer::checkTypeCompatibility(
    const HavelType& expected, const HavelType& actual,
    size_t line, size_t column) 
{
    std::string errorMsg;
    auto result = typeChecker_.checkCompatibility(expected, actual, &errorMsg);
    
    if (result == TypeCompatibility::Incompatible) {
        reportError(SemanticErrorKind::TypeMismatch, errorMsg, line, column);
    }
    
    return result;
}

// ============================================================================
// Phase 4: Semantic Validation
// ============================================================================

void SemanticAnalyzer::validateAssignments(const ast::Program& program) {
    // Implemented in visitExpression for AssignmentExpression
}

void SemanticAnalyzer::validateFunctionCalls(const ast::Program& program) {
    // Implemented in visitExpression for CallExpression
}

void SemanticAnalyzer::validateControlFlow(const ast::Program& program) {
    // Implemented in visitStatement for break/continue/return
}

void SemanticAnalyzer::validateMemberAccess(const ast::Program& program) {
    // TODO: Validate struct field access
}

void SemanticAnalyzer::validateInitialization(const ast::Program& program) {
    // Check for uninitialized variables
    // TODO: Implement proper traversal when needed
    (void)program;  // Suppress unused warning
    // Note: Shadow stack design doesn't support getAllSymbols efficiently
    // This would require a separate symbol list if needed
}

// ============================================================================
// Specific Validation Rules
// ============================================================================

void SemanticAnalyzer::validateSymbolUsage(const Symbol& sym, const ast::ASTNode& usage) {
    // Rule 1: Differentiate variable from subroutine
    if (sym.kind == SymbolKind::Function) {
        // Function used in expression context - check if it's a call
        if (usage.kind != ast::NodeType::CallExpression) {
            // Might be function pointer usage - valid in some contexts
        }
    }
}

void SemanticAnalyzer::validateProcedureCall(const Symbol& proc, 
                                              const ast::CallExpression& call) {
    // Rule 2: Prevent procedure name without argument list
    if (proc.attributes.paramCount > 0 && call.args.empty()) {
        reportError(SemanticErrorKind::MissingArguments,
                   "Procedure '" + proc.name + "' requires " + 
                   std::to_string(proc.attributes.paramCount) + " arguments",
                   call.line, call.column);
    }
}

void SemanticAnalyzer::validateAssignmentTarget(const ast::Expression& target) {
    // Rule 3: Prevent procedure name on left side of assignment
    if (target.kind == ast::NodeType::Identifier) {
        const auto& ident = static_cast<const ast::Identifier&>(target);
        
        // Check if variable exists in current scope (for implicit declaration)
        const Symbol* currentScopeSym = symbolTable_.lookupInCurrentScope(ident.symbol);
        
        // Also check outer scopes (for reassignment of outer variables)
        const Symbol* anyScopeSym = symbolTable_.lookup(ident.symbol);

        if (!currentScopeSym) {
            // Variable not in current scope - check if it's a function in outer scope
            if (anyScopeSym && anyScopeSym->kind == SymbolKind::Function) {
                reportError(SemanticErrorKind::InvalidAssignment,
                           "Cannot assign to function '" + ident.symbol + "'",
                           target.line, target.column);
                return;
            }
            
            // Implicit declaration on first assignment (Option C)
            // Declare as mutable variable in current scope
            SymbolAttributes attrs;
            attrs.isMutable = true;
            attrs.isInitialized = true;
            attrs.line = target.line;
            attrs.column = target.column;
            
            symbolTable_.define(ident.symbol, SymbolKind::Variable, 
                               HavelType::any(), attrs);
        } else if (currentScopeSym->kind == SymbolKind::Function) {
            reportError(SemanticErrorKind::InvalidAssignment,
                       "Cannot assign to function '" + ident.symbol + "'",
                       target.line, target.column);
        }
        // else: variable exists in current scope, reassignment is OK
    }
}

void SemanticAnalyzer::validateAssignmentDestination(const ast::Expression& dest) {
    // Rule 4: Avoid assigning numeral as destination (e.g., 5 = x)
    switch (dest.kind) {
        case ast::NodeType::NumberLiteral:
        case ast::NodeType::StringLiteral:
        case ast::NodeType::BooleanLiteral:
            reportError(SemanticErrorKind::InvalidAssignment,
                       "Cannot assign to literal value",
                       dest.line, dest.column);
            break;
        default:
            break;
    }
}

void SemanticAnalyzer::validateTypedAssignment(const Symbol& var, 
                                                const HavelType& valueType,
                                                size_t line, size_t column) {
    // Rule 5: Type-safe assignments
    std::string errorMsg;
    if (!typeChecker_.canAssign(var, valueType, &errorMsg)) {
        reportError(SemanticErrorKind::InvalidAssignment, errorMsg, line, column);
    }
}

void SemanticAnalyzer::optimizeConstants() {
    // Rule 6: Constant pooling - same address for identical constants
    // (Note: This is constant pooling, not folding.
    //  Folding is: 2 + 3 → 5
    //  Pooling is: "hello" + "hello" → same memory)
    // This would be done during code generation
}

void SemanticAnalyzer::validateUserDefinedType(const std::string& typeName,
                                                const ast::ASTNode& usage) {
    // Rule 7: Validate user-defined type usage
    auto& registry = getTypeRegistry();
    
    if (!registry.hasStructType(typeName) && !registry.hasEnumType(typeName)) {
        reportError(SemanticErrorKind::UndefinedType,
                   "Undefined type: " + typeName,
                   usage.line, usage.column);
    }
}

// ============================================================================
// Helpers
// ============================================================================

void SemanticAnalyzer::reportError(SemanticErrorKind kind, const std::string& message,
                                   size_t line, size_t column,
                                   const std::string& context) {
    errors_.emplace_back(kind, message, line, column, context);
}

// ============================================================================
// Module and Builtin Registry
// ============================================================================

void SemanticAnalyzer::initializeKnownModules() {
    // Audio module
    knownModules_["audio"] = {
        "getVolume", "setVolume", "increaseVolume", "decreaseVolume",
        "toggleMute", "setMute", "isMuted",
        "getDevices", "setDeviceVolume", "getDeviceVolume",
        "getApplications", "setAppVolume", "getAppVolume",
        "increaseAppVolume", "decreaseAppVolume"
    };
    
    // Brightness module
    knownModules_["brightnessManager"] = {
        "getBrightness", "setBrightness", "increaseBrightness", "decreaseBrightness",
        "getTemperature", "setTemperature", "increaseTemperature", "decreaseTemperature",
        "getShadowLift", "setShadowLift",
        "setGammaRGB", "increaseGamma", "decreaseGamma"
    };
    
    // Math module
    knownModules_["math"] = {
        "abs", "ceil", "floor", "round",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh",
        "exp", "log", "log10", "log2",
        "sqrt", "cbrt", "pow",
        "min", "max", "clamp", "lerp",
        "random", "randint",
        "deg2rad", "rad2deg",
        "sign", "fract", "mod",
        "distance", "hypot"
    };
    
    // String module
    knownModules_["string"] = {
        "upper", "lower", "trim", "replace", "split", "join",
        "length", "substr", "find", "contains"
    };
    
    // File module
    knownModules_["file"] = {
        "read", "write", "exists", "size",
        "delete", "copy", "move",
        "listDir", "mkdir", "isFile", "isDir"
    };
    
    // Process module
    knownModules_["process"] = {
        "run", "find", "list", "kill",
        "getPid", "getName"
    };
    
    // Window module
    knownModules_["window"] = {
        "active", "list", "focus", "minimize", "maximize",
        "close", "move", "resize",
        "getMonitors", "getMonitorArea",
        "moveToNextMonitor", "title", "class", "pid",
        "getActiveWindow", "getNextMonitor"
    };
    
    // Mouse module
    knownModules_["mouse"] = {
        "move", "moveTo", "moveRel", "click", "clickAt", "doubleClick",
        "press", "release", "scroll",
        "getPosition", "setSensitivity", "getSensitivity"
    };
    
    // Pixel module
    knownModules_["pixel"] = {
        "get", "match", "wait", "region",
        "find", "exists", "count",
        "getColor", "waitForColor"
    };
    
    // OCR module
    knownModules_["ocr"] = {
        "recognize", "findText", "waitForText",
        "read", "fromScreen", "fromFile"
    };
    
    // Config module
    knownModules_["config"] = {
        "get", "set", "load", "save", "reload"
    };
    
    // Hotkey module
    knownModules_["hotkey"] = {
        "register", "unregister", "grab", "ungrab"
    };
    
    // Clipboard module
    knownModules_["clipboard"] = {
        "get", "set", "clear",
        "getHistory", "clearHistory", "getCount"
    };
    
    // Timer module
    knownModules_["timer"] = {
        "setTimeout", "setInterval", "clear"
    };
    
    // App module
    knownModules_["app"] = {
        "enableReload", "disableReload", "toggleReload",
        "getScriptPath"
    };
    
    // HTTP module
    knownModules_["http"] = {
        "get", "post", "download"
    };
    
    // Browser module
    knownModules_["browser"] = {
        "connect", "open", "setZoom", "getZoom", "resetZoom",
        "click", "type", "eval"
    };
    
    // Help module
    knownModules_["help"] = {
        "list", "show"
    };
    
    // Mode module
    knownModules_["mode"] = {
        "get", "set", "previous", "is"
    };
    
    // IO module
    knownModules_["io"] = {
        "send", "sendKey", "keyDown", "keyUp",
        "map", "remap",
        "block", "unblock", "suspend", "resume",
        "grab", "ungrab",
        "keyTap",
        "mouseMove", "mouseMoveTo", "mouseClick", "mouseDoubleClick",
        "mousePress", "mouseRelease", "mouseScroll",
        "mouseGetPosition", "mouseSetSensitivity", "mouseGetSensitivity",
        "getCurrentModifiers"
    };
    
    // System module
    knownModules_["system"] = {
        "notify", "run", "beep", "sleep"
    };
}

void SemanticAnalyzer::initializeKnownBuiltins() {
    // Core builtins - AHK-style global functions
    knownBuiltins_ = {
        // Output
        "print", "println", "error", "warn", "info", "debug",
        // Utility
        "len", "type",
        // Math
        "sqrt", "abs", "sin", "cos", "tan", "PI", "E",
        "min", "max", "round", "floor", "ceil",
        // String
        "lower", "upper", "trim", "replace", "split", "join",
        // Process
        "run", "runDetached", "runWait",
        // IO/Input
        "send", "click", "clickAt",
        "keyDown", "keyUp",
        "mouseMove", "mouseMoveTo", "mouseClick", "mouseDoubleClick",
        "moveRel", "wheelUp", "wheelDown",
        // System detection
        "detectSystem", "detectDisplay", "detectMonitorConfig", "detectWindowManager",
        // Hotkey
        "Hotkey",
        // Window properties
        "title", "class", "pid", "hwnd",
        // OCR
        "ocrRead", "ocrFindText",
        // Clipboard
        "getClipboard", "setClipboard", "clipboard",
        // File
        "readFile", "writeFile", "fileExists", "read", "write", "exists",
        // Time
        "time", "date", "now",
        // Thread/sleep
        "sleep", "wait",
        // Async
        "spawn", "await", "channel", "yield",
        // Exit
        "exit"
    };
}

bool SemanticAnalyzer::isKnownModuleFunction(const std::string& module, const std::string& func) const {
    auto it = knownModules_.find(module);
    if (it == knownModules_.end()) {
        return false;
    }
    return it->second.count(func) > 0;
}

bool SemanticAnalyzer::isKnownBuiltin(const std::string& name) const {
    return knownBuiltins_.count(name) > 0;
}

void SemanticAnalyzer::enterScope(const std::string& name) {
    symbolTable_.enterScope(name);
}

void SemanticAnalyzer::exitScope() {
    symbolTable_.exitScope();
}

} // namespace havel::semantic
