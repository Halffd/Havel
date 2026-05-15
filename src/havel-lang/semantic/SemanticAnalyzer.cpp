/*
 * SemanticAnalyzer.cpp
 *
 * Enhanced semantic analysis implementation.
 */
#include "SemanticAnalyzer.hpp"
#include <algorithm>
#include <sstream>

namespace havel::semantic {
namespace {
std::shared_ptr<HavelType> resolveTypeDefinition(
    const ast::TypeDefinition *typeDef, ::havel::TypeRegistry &registry) {
  if (!typeDef) {
    return HavelType::any();
  }
  if (const auto *ref = dynamic_cast<const ast::TypeReference *>(typeDef)) {
    const std::string &n = ref->name;
    if (n == "num" || n == "int" || n == "float" || n == "number" || n == "Num") return HavelType::num();
    if (n == "str" || n == "string" || n == "String" || n == "Str") return HavelType::str();
    if (n == "bool" || n == "boolean" || n == "Bool") return HavelType::boolean();
    if (n == "null" || n == "Null") return HavelType::null();
    if (n == "any" || n == "Any") return HavelType::any();
    if (auto t = registry.getStructType(n)) return t;
    if (auto t = registry.getEnumType(n)) return t;
  }
  return HavelType::any();
}
}

SemanticAnalyzer::SemanticAnalyzer()
    : typeChecker_(TypeChecker::getInstance()) {
  // Initialize known modules and builtins
  initializeKnownModules();
  initializeKnownBuiltins();
}

bool SemanticAnalyzer::analyze(const ast::Program &program, bool resetSymbols) {
  errors_.clear();
  constantAddresses_.clear();
  nextConstantAddress_ = 0;

  // Reset symbol table when requested (keep existing symbols for REPL)
  if (resetSymbols) {
    symbolTable_ = SymbolTable();
  }

  
  registerStructTypes(program);
  registerEnumTypes(program);
  registerTraitTypes(program);

  
  buildSymbolTable(program);

  
  checkTypes(program);

  
  validateAssignments(program);
  validateFunctionCalls(program);
  validateControlFlow(program);
  validateMemberAccess(program);
  validateInitialization(program);

  
  optimizeConstants();

  return errors_.empty();
}

// ============================================================================

// ============================================================================

void SemanticAnalyzer::registerStructTypes(const ast::Program &program) {
  auto &registry = getTypeRegistry();

  for (const auto &stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::StructDeclaration)
      continue;

    const auto &structDecl = static_cast<const ast::StructDeclaration &>(*stmt);
    auto structType = std::make_shared<HavelStructType>(structDecl.name);

    // Register fields
    for (const auto &field : structDecl.definition.fields) {
      std::optional<std::shared_ptr<HavelType>> fieldType;
      if (field.type) fieldType = resolveTypeDefinition(field.type->get(), registry);
      structType->addField(StructField(field.name, fieldType));

      // Add field to symbol table
      Symbol fieldSym;
      fieldSym.name = field.name;
      fieldSym.kind = SymbolKind::Field;
      fieldSym.attributes.type = fieldType.value_or(HavelType::any());
      fieldSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
      fieldSym.attributes.size = 8; // Default size
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

void SemanticAnalyzer::registerEnumTypes(const ast::Program &program) {
  auto &registry = getTypeRegistry();

  for (const auto &stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::EnumDeclaration)
      continue;

    const auto &enumDecl = static_cast<const ast::EnumDeclaration &>(*stmt);
    auto enumType = std::make_shared<HavelEnumType>(enumDecl.name);

    // Register variants
    for (const auto &variant : enumDecl.definition.variants) {
      std::optional<std::shared_ptr<HavelType>> payloadType;
      bool hasPayload = variant.payloadType.has_value();
      if (hasPayload) payloadType = resolveTypeDefinition(variant.payloadType->get(), registry);
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

void SemanticAnalyzer::registerTraitTypes(const ast::Program &program) {
  for (const auto &stmt : program.body) {
    if (!stmt || stmt->kind != ast::NodeType::TraitDeclaration) continue;
    const auto &traitDecl = static_cast<const ast::TraitDeclaration &>(*stmt);
    if (!traitDecl.name) continue;
    Symbol traitSym;
    traitSym.name = traitDecl.name->symbol;
    traitSym.kind = SymbolKind::Trait;
    traitSym.attributes.type = HavelType::any();
    traitSym.scopeLevel = symbolTable_.getCurrentScopeLevel();
    symbolTable_.define(traitSym);
  }
}

// ============================================================================

// ============================================================================

void SemanticAnalyzer::buildSymbolTable(const ast::Program &program) {
  // First pass: register all top-level declarations
  for (const auto &stmt : program.body) {
    if (!stmt)
      continue;
    visitStatement(*stmt);
  }
}

void SemanticAnalyzer::visitStatement(const ast::Statement &stmt) {
  switch (stmt.kind) {
  case ast::NodeType::LetDeclaration: {
    const auto &letDecl = static_cast<const ast::LetDeclaration &>(stmt);

    std::shared_ptr<HavelType> varType = HavelType::any();
    if (letDecl.typeAnnotation) {
      // TODO: Resolve type from annotation
    }

    // Get variable name from pattern
    if (letDecl.pattern && letDecl.pattern->kind == ast::NodeType::Identifier) {
      const auto &ident =
          static_cast<const ast::Identifier &>(*letDecl.pattern);

      SymbolAttributes attrs;
      attrs.isMutable = !letDecl.isConst;
      attrs.isInitialized = (letDecl.value != nullptr);
      attrs.line = stmt.line;
      attrs.column = stmt.column;

      SymbolKind kind =
          letDecl.isConst ? SymbolKind::Constant : SymbolKind::Variable;

      // Check for duplicate definition
      if (!symbolTable_.define(ident.symbol, kind, varType, attrs)) {
        reportError(SemanticErrorKind::DuplicateDefinition,
                    "Variable '" + ident.symbol +
                        "' already defined in this scope",
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
    const auto &funcDecl = static_cast<const ast::FunctionDeclaration &>(stmt);

    if (funcDecl.name) {
      SymbolAttributes attrs;
      attrs.paramCount = funcDecl.parameters.size();
      attrs.line = stmt.line;
      attrs.column = stmt.column;

      auto retType = HavelType::any();
      if (funcDecl.returnType && (*funcDecl.returnType) && (*funcDecl.returnType)->type) {
        retType = resolveTypeDefinition((*funcDecl.returnType)->type.get(), getTypeRegistry());
      }
      symbolTable_.define(funcDecl.name->symbol, SymbolKind::Function, retType, attrs);
    }

    // Process function body in new scope
    enterScope(funcDecl.name ? funcDecl.name->symbol : "anonymous");

    // Push function context (supports nested functions)
    context_.functionStack.push_back(context_.inFunction);
    context_.inFunction = true;
    const Symbol *prevFunction = context_.currentFunction;
    if (funcDecl.name) context_.currentFunction = symbolTable_.lookup(funcDecl.name->symbol);

    // Register parameters
    for (const auto &param : funcDecl.parameters) {
      if (param && param->pattern) {
        registerParameterPattern(*param->pattern);
      }
    }

    // Visit function body
    if (funcDecl.body) {
      visitStatement(*funcDecl.body);
    }

    // Pop function context
    context_.inFunction = context_.functionStack.back();
    context_.functionStack.pop_back();
    context_.currentFunction = prevFunction;

 exitScope();
 break;
 }

 case ast::NodeType::DecoratorStatement: {
 const auto &dec = static_cast<const ast::DecoratorStatement &>(stmt);
 for (const auto &decoExpr : dec.decorators) {
 if (decoExpr) visitExpression(*decoExpr);
 }
 if (dec.target) visitStatement(*dec.target);
 break;
 }

 case ast::NodeType::BlockStatement: {
    const auto &block = static_cast<const ast::BlockStatement &>(stmt);

    enterScope("block");
    for (const auto &s : block.body) {
      if (s)
        visitStatement(*s);
    }
    exitScope();
    break;
  }

  case ast::NodeType::IfStatement: {
    const auto &ifStmt = static_cast<const ast::IfStatement &>(stmt);

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
    const auto &whileStmt = static_cast<const ast::WhileStatement &>(stmt);

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
    const auto &forStmt = static_cast<const ast::ForStatement &>(stmt);

    enterScope("for");

    // Register loop variable(s)
    for (const auto &iter : forStmt.iterators) {
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
    const auto &retStmt = static_cast<const ast::ReturnStatement &>(stmt);

    if (!context_.inFunction) {
      reportError(SemanticErrorKind::ReturnOutsideFunction,
                  "return statement outside function", stmt.line, stmt.column);
    }

    if (retStmt.argument) {
      visitExpression(*retStmt.argument);
      if (context_.currentFunction && context_.currentFunction->attributes.type &&
          context_.currentFunction->attributes.type->getKind() != HavelType::Kind::Any) {
        auto actual = inferType(*retStmt.argument);
        checkTypeCompatibility(*context_.currentFunction->attributes.type, *actual,
                               stmt.line, stmt.column);
      }
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
      const char *msg = (stmt.kind == ast::NodeType::BreakStatement)
                            ? "break statement outside loop"
                            : "continue statement outside loop";

      reportError(kind, msg, stmt.line, stmt.column);
    }
    break;
  }

  case ast::NodeType::ExpressionStatement: {
    const auto &exprStmt = static_cast<const ast::ExpressionStatement &>(stmt);
    if (exprStmt.expression) {
      visitExpression(*exprStmt.expression);
    }
    break;
  }

  case ast::NodeType::HotkeyBinding: {
    const auto &hotkey = static_cast<const ast::HotkeyBinding &>(stmt);

    // Create isolated scope for hotkey action
    enterScope("hotkey");

    if (hotkey.action) {
      visitStatement(*hotkey.action);
    }

    exitScope();
    break;
  }

  case ast::NodeType::ConditionalHotkey: {
    const auto &condHotkey = static_cast<const ast::ConditionalHotkey &>(stmt);

    // Visit the condition expression
    if (condHotkey.condition) {
      visitExpression(*condHotkey.condition);
    }

    // Visit the wrapped hotkey binding (which will create its own scope)
    if (condHotkey.binding) {
      visitStatement(*condHotkey.binding);
    }
    break;
  }

  case ast::NodeType::SignalDefinition: {
    const auto &signalDef = static_cast<const ast::SignalDefinition &>(stmt);

    // Register signal name in symbol table
    SymbolAttributes attrs;
    attrs.isMutable = false;
    attrs.isInitialized = true;
    attrs.line = stmt.line;
    attrs.column = stmt.column;
    symbolTable_.define(signalDef.name, SymbolKind::Signal,
                        HavelType::boolean(), attrs);

    // Visit signal condition expression
    if (signalDef.condition) {
      visitExpression(*signalDef.condition);
    }
    break;
  }

  case ast::NodeType::WhenStatement: {
    const auto &whenStmt = static_cast<const ast::WhenStatement &>(stmt);

    // Visit the trigger condition
    if (whenStmt.trigger) {
      visitExpression(*whenStmt.trigger);
    }

    // Visit the body in a new scope
    enterScope("when");
    if (whenStmt.body) {
      visitStatement(*whenStmt.body);
    }
    exitScope();
    break;
  }

  default:
    break;
  }
}

void SemanticAnalyzer::visitExpression(const ast::Expression &expr) {
  switch (expr.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(expr);
    const Symbol *sym = symbolTable_.lookup(ident.symbol);

    if (!sym) {
      // Reading undefined variable is always an error
      // (implicit declaration only applies to assignments)
      reportError(SemanticErrorKind::UndefinedVariable,
                  "Undefined variable: " + ident.symbol, expr.line,
                  expr.column);
    } else if (sym->kind == SymbolKind::Function) {
      // Using function name without call - might be valid (function pointer)
      // or error depending on context
    }
    break;
  }

  case ast::NodeType::AssignmentExpression: {
    const auto &assign = static_cast<const ast::AssignmentExpression &>(expr);

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
    const auto &call = static_cast<const ast::CallExpression &>(expr);

    if (call.callee) {
      if (call.callee->kind == ast::NodeType::Identifier) {
        const auto &ident = static_cast<const ast::Identifier &>(*call.callee);
        const Symbol *sym = symbolTable_.lookup(ident.symbol);

        if (sym && sym->kind == SymbolKind::Function) {
          validateProcedureCall(*sym, call);
        }
        // If not found, treat as global callable (resolved at runtime).
      } else if (call.callee->kind == ast::NodeType::MemberExpression) {
        const auto &member =
            static_cast<const ast::MemberExpression &>(*call.callee);
        if (member.object) {
          if (member.object->kind == ast::NodeType::Identifier) {
            const auto &moduleIdent =
                static_cast<const ast::Identifier &>(*member.object);
            const Symbol *sym = symbolTable_.lookup(moduleIdent.symbol);
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

    for (const auto &arg : call.args) {
      if (arg)
        visitExpression(*arg);
    }
    break;
  }

  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expr);

    if (member.object) {
      if (member.object->kind == ast::NodeType::Identifier) {
        const auto &ident =
            static_cast<const ast::Identifier &>(*member.object);
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
    const auto &binary = static_cast<const ast::BinaryExpression &>(expr);
    if (binary.left)
      visitExpression(*binary.left);
    if (binary.right)
      visitExpression(*binary.right);
    break;
  }

  case ast::NodeType::TernaryExpression: {
    const auto &ternary = static_cast<const ast::TernaryExpression &>(expr);
    if (ternary.condition)
      visitExpression(*ternary.condition);
    if (ternary.trueValue)
      visitExpression(*ternary.trueValue);
    if (ternary.falseValue)
      visitExpression(*ternary.falseValue);
    break;
  }

  case ast::NodeType::UnaryExpression: {
    const auto &unary = static_cast<const ast::UnaryExpression &>(expr);
    if (unary.operand)
      visitExpression(*unary.operand);
    break;
  }

  default:
    // Literals and other expressions don't need symbol table updates
    break;
  }
}

// ============================================================================

// ============================================================================

void SemanticAnalyzer::checkTypes(const ast::Program &program) {
  // Type checking is done during visitExpression via inferType()
}

std::shared_ptr<HavelType>
SemanticAnalyzer::inferType(const ast::Expression &expr) {
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

TypeCompatibility
SemanticAnalyzer::checkTypeCompatibility(const HavelType &expected,
                                         const HavelType &actual, size_t line,
                                         size_t column) {
  std::string errorMsg;
  auto result = typeChecker_.checkCompatibility(expected, actual, &errorMsg);

  if (result == TypeCompatibility::Incompatible) {
    reportError(SemanticErrorKind::TypeMismatch, errorMsg, line, column);
  }

  return result;
}

// ============================================================================

// ============================================================================

void SemanticAnalyzer::validateAssignments(const ast::Program &program) {
  // Implemented in visitExpression for AssignmentExpression
}

void SemanticAnalyzer::validateFunctionCalls(const ast::Program &program) {
  // Implemented in visitExpression for CallExpression
}

void SemanticAnalyzer::validateControlFlow(const ast::Program &program) {
  // Implemented in visitStatement for break/continue/return
}

void SemanticAnalyzer::validateMemberAccess(const ast::Program &program) {
  std::function<void(const ast::Expression&)> walkExpr;
  std::function<void(const ast::Statement&)> walkStmt;
  walkExpr = [&](const ast::Expression &expr) {
    if (expr.kind == ast::NodeType::MemberExpression) {
      const auto &m = static_cast<const ast::MemberExpression &>(expr);
      if (m.object && m.property && m.object->kind == ast::NodeType::Identifier &&
          m.property->kind == ast::NodeType::Identifier) {
        const auto &obj = static_cast<const ast::Identifier &>(*m.object);
        const auto &prop = static_cast<const ast::Identifier &>(*m.property);
        if (const Symbol *s = symbolTable_.lookup(obj.symbol)) {
          auto t = s->attributes.type;
          if (t && t->getKind() == HavelType::Kind::Struct) {
            auto st = std::dynamic_pointer_cast<HavelStructType>(t);
            if (st && !st->getField(prop.symbol) && !st->hasMethod(prop.symbol)) {
              reportError(SemanticErrorKind::UnknownField,
                          "Unknown field '" + prop.symbol + "' on struct '" + st->getName() + "'",
                          expr.line, expr.column);
            }
          }
        }
      }
    }
  };
  walkStmt = [&](const ast::Statement &stmt) {
    if (stmt.kind == ast::NodeType::ExpressionStatement) {
      const auto &e = static_cast<const ast::ExpressionStatement &>(stmt);
      if (e.expression) walkExpr(*e.expression);
    } else if (stmt.kind == ast::NodeType::BlockStatement) {
      const auto &b = static_cast<const ast::BlockStatement &>(stmt);
      for (const auto &s : b.body) if (s) walkStmt(*s);
    }
  };
  for (const auto &stmt : program.body) if (stmt) walkStmt(*stmt);
}

void SemanticAnalyzer::validateInitialization(const ast::Program &program) {
  std::function<void(const ast::Statement&)> walkStmt;
  walkStmt = [&](const ast::Statement &stmt) {
    if (stmt.kind == ast::NodeType::LetDeclaration) {
      const auto &letDecl = static_cast<const ast::LetDeclaration &>(stmt);
      if (!letDecl.value && letDecl.pattern && letDecl.pattern->kind == ast::NodeType::Identifier) {
        const auto &id = static_cast<const ast::Identifier &>(*letDecl.pattern);
        reportError(SemanticErrorKind::UninitializedVariable,
                    "Variable '" + id.symbol + "' declared without initializer",
                    stmt.line, stmt.column);
      }
    } else if (stmt.kind == ast::NodeType::BlockStatement) {
      const auto &b = static_cast<const ast::BlockStatement &>(stmt);
      for (const auto &s : b.body) if (s) walkStmt(*s);
    }
  };
  for (const auto &stmt : program.body) if (stmt) walkStmt(*stmt);
}

// ============================================================================
// Specific Validation Rules
// ============================================================================

void SemanticAnalyzer::validateSymbolUsage(const Symbol &sym,
                                           const ast::ASTNode &usage) {
  // Rule 1: Differentiate variable from subroutine
  if (sym.kind == SymbolKind::Function) {
    // Function used in expression context - check if it's a call
    if (usage.kind != ast::NodeType::CallExpression) {
      // Might be function pointer usage - valid in some contexts
    }
  }
}

void SemanticAnalyzer::validateProcedureCall(const Symbol &proc,
                                             const ast::CallExpression &call) {
  // Rule 2: Prevent procedure name without argument list
  if (proc.attributes.paramCount > 0 && call.args.empty()) {
    reportError(SemanticErrorKind::MissingArguments,
                "Procedure '" + proc.name + "' requires " +
                    std::to_string(proc.attributes.paramCount) + " arguments",
                call.line, call.column);
  }
}

void SemanticAnalyzer::validateAssignmentTarget(const ast::Expression &target) {
  // Rule 3: Prevent procedure name on left side of assignment
  if (target.kind == ast::NodeType::Identifier) {
    const auto &ident = static_cast<const ast::Identifier &>(target);

    // Check if variable exists in current scope (for implicit declaration)
    const Symbol *currentScopeSym =
        symbolTable_.lookupInCurrentScope(ident.symbol);

    // Also check outer scopes (for reassignment of outer variables)
    const Symbol *anyScopeSym = symbolTable_.lookup(ident.symbol);

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

      symbolTable_.define(ident.symbol, SymbolKind::Variable, HavelType::any(),
                          attrs);
    } else if (currentScopeSym->kind == SymbolKind::Function) {
      reportError(SemanticErrorKind::InvalidAssignment,
                  "Cannot assign to function '" + ident.symbol + "'",
                  target.line, target.column);
    }
    // else: variable exists in current scope, reassignment is OK
  }
}

void SemanticAnalyzer::validateAssignmentDestination(
    const ast::Expression &dest) {
  // Rule 4: Avoid assigning numeral as destination (e.g., 5 = x)
  switch (dest.kind) {
  case ast::NodeType::NumberLiteral:
  case ast::NodeType::StringLiteral:
  case ast::NodeType::BooleanLiteral:
    reportError(SemanticErrorKind::InvalidAssignment,
                "Cannot assign to literal value", dest.line, dest.column);
    break;
  default:
    break;
  }
}

void SemanticAnalyzer::validateTypedAssignment(const Symbol &var,
                                               const HavelType &valueType,
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
  // Minimal constant folding scaffolding for literal binary arithmetic.
  // Full AST rewrite is deferred to codegen/optimizer pass.
}

void SemanticAnalyzer::validateUserDefinedType(const std::string &typeName,
                                               const ast::ASTNode &usage) {
  // Rule 7: Validate user-defined type usage
  auto &registry = getTypeRegistry();

  if (!registry.hasStructType(typeName) && !registry.hasEnumType(typeName)) {
    reportError(SemanticErrorKind::UndefinedType, "Undefined type: " + typeName,
                usage.line, usage.column);
  }
}

// ============================================================================
// Helpers
// ============================================================================

void SemanticAnalyzer::reportError(SemanticErrorKind kind,
                                   const std::string &message, size_t line,
                                   size_t column, const std::string &context) {
  errors_.emplace_back(kind, message, line, column, context);
}

// ============================================================================
// Module and Builtin Registry
// ============================================================================

void SemanticAnalyzer::initializeKnownModules() {
  // Audio module
  knownModules_["audio"] = {
      "getVolume",       "setVolume",         "increaseVolume",
      "decreaseVolume",  "toggleMute",        "setMute",
      "isMuted",         "getDevices",        "setDeviceVolume",
      "getDeviceVolume", "getApplications",   "setAppVolume",
      "getAppVolume",    "increaseAppVolume", "decreaseAppVolume"};

  // Brightness module
  knownModules_["brightnessManager"] = {
      "getBrightness",       "setBrightness",       "increaseBrightness",
      "decreaseBrightness",  "getTemperature",      "setTemperature",
      "increaseTemperature", "decreaseTemperature", "getShadowLift",
      "setShadowLift",       "setGammaRGB",         "increaseGamma",
      "decreaseGamma"};

  // Math module
  knownModules_["math"] = {
      "abs",     "ceil", "floor", "round", "sin",      "cos",     "tan",
      "asin",    "acos", "atan",  "atan2", "sinh",     "cosh",    "tanh",
      "exp",     "log",  "log10", "log2",  "sqrt",     "cbrt",    "pow",
      "min",     "max",  "clamp", "lerp",  "random",   "randint", "deg2rad",
      "rad2deg", "sign", "fract", "mod",   "distance", "hypot"};

  // String module
  knownModules_["string"] = {"upper", "lower",   "trim",   "replace",
                             "split", "join",    "length", "substr",
                             "find",  "contains"};

  // File module
  knownModules_["file"] = {"read",   "write",  "exists", "size",
                           "delete", "copy",   "move",   "listDir",
                           "mkdir",  "isFile", "isDir"};

  // Process module
  knownModules_["process"] = {"run",  "find",   "list",
                              "kill", "getPid", "getName"};

  // Window module
  knownModules_["window"] = {"active",
                             "list",
                             "focus",
                             "minimize",
                             "maximize",
                             "close",
                             "move",
                             "resize",
                             "getMonitors",
                             "getMonitorArea",
                             "moveToNextMonitor",
                             "title",
                             "class",
                             "pid",
                             "getActiveWindow",
                             "getNextMonitor"};

  // Mouse module
  knownModules_["mouse"] = {"move",        "moveTo",         "moveRel",
                            "click",       "clickAt",        "doubleClick",
                            "press",       "release",        "scroll",
                            "getPosition", "setSensitivity", "getSensitivity"};

  // Pixel module
  knownModules_["pixel"] = {"get",    "match",    "wait",
                            "region", "find",     "exists",
                            "count",  "getColor", "waitForColor"};

  // OCR module
  knownModules_["ocr"] = {"recognize", "findText",   "waitForText",
                          "read",      "fromScreen", "fromFile"};

  // Config module
  knownModules_["config"] = {"get", "set", "load", "save", "reload"};

  // Hotkey module
  knownModules_["hotkey"] = {"register", "unregister", "grab", "ungrab"};

  // Clipboard module
  knownModules_["clipboard"] = {"get",        "set",          "clear",
                                "getHistory", "clearHistory", "getCount"};

  // Timer module
  knownModules_["timer"] = {"setTimeout", "setInterval", "clear", "activeCount", "clearAll"};

  // App module
  knownModules_["app"] = {"enableReload", "disableReload", "toggleReload",
                          "getScriptPath"};

  // HTTP module
  knownModules_["http"] = {"get", "post", "download"};

  // Browser module
  knownModules_["browser"] = {"connect",   "open",  "setZoom", "getZoom",
                              "resetZoom", "click", "type",    "eval"};

  // Help module
  knownModules_["help"] = {"list", "show"};

  // Mode module
  knownModules_["mode"] = {"get", "set", "previous", "is"};

  // IO module
  knownModules_["io"] = {"send",
                         "sendKey",
                         "keyDown",
                         "keyUp",
                         "map",
                         "remap",
                         "block",
                         "unblock",
                         "suspend",
                         "resume",
                         "grab",
                         "ungrab",
                         "keyTap",
                         "mouseMove",
                         "mouseMoveTo",
                         "mouseClick",
                         "mouseDoubleClick",
                         "mousePress",
                         "mouseRelease",
                         "mouseScroll",
                         "mouseGetPosition",
                         "mouseSetSensitivity",
                         "mouseGetSensitivity",
                         "getCurrentModifiers"};

  // System module
  knownModules_["system"] = {"notify", "run", "beep", "sleep"};
}

void SemanticAnalyzer::initializeKnownBuiltins() {
  // Core builtins - AHK-style global functions
  knownBuiltins_ = {// Output
                    "print", "println", "error", "warn", "info", "debug",
                    // Utility
                    "len", "type", "ord", "char",
                    // Math
                    "sqrt", "abs", "sin", "cos", "tan", "PI", "E", "min", "max",
                    "round", "floor", "ceil",
                    // String
                    "lower", "upper", "trim", "replace", "split", "join",
                    // Process
                    "run", "runDetached", "runWait",
                    // IO/Input
                    "send", "click", "clickAt", "keyDown", "keyUp", "mouseMove",
                    "mouseMoveTo", "mouseClick", "mouseDoubleClick", "moveRel",
                    "wheelUp", "wheelDown",
                    // System detection
                    "detectSystem", "detectDisplay", "detectMonitorConfig",
                    "detectWindowManager",
                    // Hotkey
                    "Hotkey",
                    // Window properties
                    "title", "class", "pid", "hwnd",
                    // OCR
                    "ocrRead", "ocrFindText",
                    // Clipboard
                    "getClipboard", "setClipboard", "clipboard",
                    // File
                    "readFile", "writeFile", "fileExists", "read", "write",
                    "exists",
                    // Time
                    "time", "date", "now",
    // Thread/sleep
    "sleep", "wait",
    // Async
    "spawn", "await", "channel", "yield",
    // Concurrency
    "thread", "interval", "timeout",
    // Exit
    "exit"};
}

bool SemanticAnalyzer::isKnownModuleFunction(const std::string &module,
                                             const std::string &func) const {
  auto it = knownModules_.find(module);
  if (it == knownModules_.end()) {
    return false;
  }
  return it->second.count(func) > 0;
}

bool SemanticAnalyzer::isKnownBuiltin(const std::string &name) const {
  return knownBuiltins_.count(name) > 0;
}

// Register all identifiers in a parameter pattern as function parameters
void SemanticAnalyzer::registerParameterPattern(
    const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    SymbolAttributes attrs;
    attrs.isMutable = false; // Parameters are immutable
    attrs.isInitialized = true;
    attrs.line = ident.line;
    attrs.column = ident.column;
    symbolTable_.define(ident.symbol, SymbolKind::Parameter, HavelType::any(),
                        attrs);
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      registerParameterPattern(*prop.second);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (const auto &elem : arrPat.elements) {
      if (elem) {
        registerParameterPattern(*elem);
      }
    }
    break;
  }
  default:
    break;
  }
}

void SemanticAnalyzer::enterScope(const std::string &name) {
  symbolTable_.enterScope(name);
}

void SemanticAnalyzer::exitScope() { symbolTable_.exitScope(); }

} // namespace havel::semantic
