#include "LexicalResolver.hpp"
#include <iostream>

namespace havel::compiler {

LexicalResolutionResult LexicalResolver::resolve(const ast::Program &program) {
  result_ = {};
  errors_.clear();
  top_level_functions_.clear();
  top_level_structs_.clear();
  global_variables_.clear();
  function_stack_.clear();

  collectTopLevelFunctions(program);
  collectTopLevelStructs(program);

  // Enter global scope
  beginFunction(nullptr);

  // Pre-scan: collect all top-level assignment targets as implicit globals
  // This ensures lambdas can reference variables that are assigned at top level
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::ExpressionStatement) {
      continue;
    }
    const auto &exprStmt =
        static_cast<const ast::ExpressionStatement &>(*statement);
    if (!exprStmt.expression ||
        exprStmt.expression->kind != ast::NodeType::AssignmentExpression) {
      continue;
    }
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(*exprStmt.expression);
    if (assignment.target &&
        assignment.target->kind == ast::NodeType::Identifier) {
      const auto &ident =
          static_cast<const ast::Identifier &>(*assignment.target);
      // Pre-declare as global variable
      global_variables_.insert(ident.symbol);
    }
  }

  // First pass: declare ALL top-level bindings (let and fn names)
  // This ensures function bodies can see global variables
  for (const auto &statement : program.body) {
    if (!statement)
      continue;

    if (statement->kind == ast::NodeType::LetDeclaration) {
      // Declare the variable and track as global
      const auto &let = static_cast<const ast::LetDeclaration &>(*statement);
      if (let.value) {
        resolveExpression(*let.value);
      }
      if (auto *identifier =
              dynamic_cast<const ast::Identifier *>(let.pattern.get())) {
        declareLocal(identifier->symbol, identifier, let.isConst);
        global_variables_.insert(identifier->symbol);
        noteIdentifierBinding(
            *identifier,
            ResolvedBinding{ResolvedBindingKind::Global, 0, 0, identifier->symbol,
                            let.isConst});
      } else if (let.pattern && (let.pattern->kind == ast::NodeType::ListPattern ||
                                 let.pattern->kind == ast::NodeType::ArrayPattern)) {
        const auto &arrPat = static_cast<const ast::ArrayPattern &>(*let.pattern);
        for (const auto &elem : arrPat.elements) {
          auto *elem_id = elem ? dynamic_cast<const ast::Identifier *>(elem.get()) : nullptr;
          if (elem_id) {
            declareLocal(elem_id->symbol, elem_id, let.isConst);
            global_variables_.insert(elem_id->symbol);
            noteIdentifierBinding(
                *elem_id,
                ResolvedBinding{ResolvedBindingKind::Global, 0, 0,
                                elem_id->symbol, let.isConst});
          }
        }
      } else if (let.pattern && let.pattern->kind == ast::NodeType::ObjectPattern) {
        const auto &objPat = static_cast<const ast::ObjectPattern &>(*let.pattern);
        for (const auto &prop : objPat.properties) {
          collectPatternIdentifiers(*prop.second);
          // Also add to globals for top-level let
          if (auto *id = dynamic_cast<const ast::Identifier *>(prop.second.get())) {
            global_variables_.insert(id->symbol);
            noteIdentifierBinding(
                *id,
                ResolvedBinding{ResolvedBindingKind::Global, 0, 0, id->symbol,
                                let.isConst});
          }
        }
      }
    } else if (statement->kind == ast::NodeType::FunctionDeclaration) {
      // Just add to top_level_functions_ - don't declare as local
      const auto &fn =
          static_cast<const ast::FunctionDeclaration &>(*statement);
      if (fn.name) {
        top_level_functions_.insert(fn.name->symbol);
      }
    }
  }

  // Second pass: resolve function bodies (can now see globals)
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      continue;
    }
    const auto &fn = static_cast<const ast::FunctionDeclaration &>(*statement);
    resolveFunctionDeclaration(fn);
  }

  // Third pass: resolve top-level non-function, non-let statements
  // LetDeclaration was already handled in the first pass
  for (const auto &statement : program.body) {
    if (!statement || statement->kind == ast::NodeType::FunctionDeclaration ||
        statement->kind == ast::NodeType::LetDeclaration) {
      continue;
    }
    resolveStatement(*statement);
  }

  // Copy to result for bytecode compiler
  result_.global_variables = global_variables_;
  for (const auto &[ident, slot] : result_.declaration_slots) {
  }

  endFunction();
  return result_;
}

void LexicalResolver::collectTopLevelFunctions(const ast::Program &program) {
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      continue;
    }

    const auto &fn = static_cast<const ast::FunctionDeclaration &>(*statement);
    if (fn.name) {
      top_level_functions_.insert(fn.name->symbol);
    }
  }
}

void LexicalResolver::collectTopLevelStructs(const ast::Program &program) {
  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }
    if (statement->kind == ast::NodeType::StructDeclaration) {
      const auto &decl = static_cast<const ast::StructDeclaration &>(*statement);
      top_level_structs_.insert(decl.name);
      continue;
    }
    if (statement->kind == ast::NodeType::ClassDeclaration) {
      const auto &decl = static_cast<const ast::ClassDeclaration &>(*statement);
      top_level_structs_.insert(decl.name);
    }
  }
}

void LexicalResolver::beginFunction(const ast::ASTNode *function) {
  function_stack_.push_back(FunctionContext{});
  auto &ctx = function_stack_.back();
  ctx.owner = function;
  beginScope();
}

void LexicalResolver::endFunction() {
  if (function_stack_.empty()) {
    return;
  }

  const auto &ctx = function_stack_.back();
  if (ctx.owner) {
    if (ctx.owner->kind == ast::NodeType::FunctionDeclaration) {
      result_.function_upvalues[static_cast<const ast::FunctionDeclaration *>(
          ctx.owner)] = ctx.upvalues;
    } else if (ctx.owner->kind == ast::NodeType::LambdaExpression) {
      result_.lambda_upvalues[static_cast<const ast::LambdaExpression *>(
          ctx.owner)] = ctx.upvalues;
    }
    // ThreadExpression/IntervalExpression/TimeoutExpression: identifiers in
    // their bodies are resolved and stored in identifier_bindings, so
    // ByteCompiler can find them via bindingFor().
  }

  function_stack_.pop_back();
}

void LexicalResolver::beginScope() {
  if (function_stack_.empty()) {
    return;
  }
  function_stack_.back().scopes.emplace_back();
}

void LexicalResolver::endScope() {
  if (function_stack_.empty() || function_stack_.back().scopes.empty()) {
    return;
  }
  function_stack_.back().scopes.pop_back();
}

uint32_t LexicalResolver::declareLocal(const std::string &name,
                                       const ast::Identifier *declaration,
                                       bool is_const) {
  auto &ctx = function_stack_.back();
  if (ctx.scopes.empty()) {
    beginScope();
  }

  auto &scope = ctx.scopes.back();
  auto it = scope.find(name);
  if (it != scope.end()) {
    // Duplicate declaration in same scope - report error with source location
    std::string msg =
        "Duplicate declaration: '" + name + "' already defined in this scope";
    if (declaration) {
      msg += " at " + std::to_string(declaration->line) + ":" +
             std::to_string(declaration->column);
    }
    errors_.push_back(msg);
    return it->second.slot;
  }

  uint32_t slot = ctx.next_slot++;
  scope[name] =
      FunctionContext::LocalSymbol{.slot = slot, .is_const = is_const};
  if (declaration) {
    result_.declaration_slots[declaration] = slot;
    // Also add to identifier_bindings so ByteCompiler::bindingFor() can find it
    ResolvedBinding binding;
    binding.kind = ResolvedBindingKind::Local;
    binding.slot = slot;
    binding.name = name;
    binding.is_const = is_const;
    result_.identifier_bindings[declaration] = binding;
  }
  return slot;
}

// Declare local without duplicate checking (for match pattern bindings)
uint32_t LexicalResolver::declareLocalUnchecked(const std::string &name,
                                                const ast::Identifier *declaration,
                                                bool is_const) {
  auto &ctx = function_stack_.back();
  if (ctx.scopes.empty()) {
    beginScope();
  }

  auto &scope = ctx.scopes.back();
  auto it = scope.find(name);
  if (it != scope.end()) {
    // Already declared - return existing slot
    return it->second.slot;
  }

  uint32_t slot = ctx.next_slot++;
  scope[name] =
      FunctionContext::LocalSymbol{.slot = slot, .is_const = is_const};
  if (declaration) {
    result_.declaration_slots[declaration] = slot;
    // Also add to identifier_bindings so ByteCompiler::bindingFor() can find it
    ResolvedBinding binding;
    binding.kind = ResolvedBindingKind::Local;
    binding.slot = slot;
    binding.name = name;
    binding.is_const = is_const;
    result_.identifier_bindings[declaration] = binding;
  }
  return slot;
}

void LexicalResolver::resolveFunctionDeclaration(
    const ast::FunctionDeclaration &function) {
  beginFunction(&function);


  for (const auto &param : function.parameters) {
    if (param && param->pattern) {
      collectPatternIdentifiers(*param->pattern);
    }
  }

  for (const auto &scope : function_stack_.back().scopes) {
    for (const auto &[name, sym] : scope) {
    }
  }

  if (function.body) {
    for (const auto &statement : function.body->body) {
      if (statement) {
        resolveStatement(*statement);
      }
    }
  }

  endFunction();
}

void LexicalResolver::resolveLambdaExpression(
    const ast::LambdaExpression &lambda) {
  beginFunction(&lambda);

  // For lambda parameters, allocate slots in order:
  // - Each parameter gets a slot (0, 1, 2... for params)
  // - Pattern params also allocate additional slots for extracted values
  for (size_t i = 0; i < lambda.parameters.size(); i++) {
    const auto &param = lambda.parameters[i];
    if (param && param->pattern) {
      if (param->pattern->kind == ast::NodeType::Identifier) {
        // Simple identifier - allocate one slot for this param
        const auto &ident =
            static_cast<const ast::Identifier &>(*param->pattern);
        declareLocal(ident.symbol, &ident, false);
      } else {
        // Pattern - allocate slot for parameter value (using temp name)
        // The extracted values will get subsequent slots
        declareLocal("_p" + std::to_string(i), nullptr, false);
        // Then allocate slots for extracted values
        collectPatternIdentifiers(*param->pattern);
      }
    }
  }

  if (lambda.body) {
    resolveStatement(*lambda.body);
  }

  endFunction();
}

// Collect all identifiers from a pattern and declare them as locals
void LexicalResolver::collectPatternIdentifiers(
    const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    declareLocal(ident.symbol, &ident, false);
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      collectPatternIdentifiers(*prop.second);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (const auto &elem : arrPat.elements) {
      if (elem) {
        collectPatternIdentifiers(*elem);
      }
    }
    if (arrPat.rest) {
      collectPatternIdentifiers(*arrPat.rest);
    }
    break;
  }
  case ast::NodeType::OrPattern: {
    const auto &orPat = static_cast<const ast::OrPattern &>(pattern);
    // For or patterns, collect from first alternative only
    // (all alternatives should bind the same variables)
    if (!orPat.alternatives.empty() && orPat.alternatives[0]) {
      collectPatternIdentifiers(*orPat.alternatives[0]);
    }
    break;
  }
  case ast::NodeType::SpreadPattern: {
    const auto &spreadPat = static_cast<const ast::SpreadPattern &>(pattern);
    if (spreadPat.target) {
      collectPatternIdentifiers(*spreadPat.target);
    }
    break;
  }
  case ast::NodeType::RangePattern: {
    const auto &rangePat = static_cast<const ast::RangePattern &>(pattern);
    if (rangePat.start) collectPatternIdentifiers(*rangePat.start);
    if (rangePat.end) collectPatternIdentifiers(*rangePat.end);
    break;
  }
  default:
    break;
  }
}

// Resolve a pattern in match context - declares identifiers as locals
// without duplicate checking (each match arm gets its own scope logically)
void LexicalResolver::resolvePatternWithBindings(const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    // Check if already bound in current scope (e.g., from a previous match arm)
    auto &ctx = function_stack_.back();
    if (ctx.scopes.empty()) {
      beginScope();
    }
    auto &scope = ctx.scopes.back();
    auto it = scope.find(ident.symbol);
    if (it == scope.end()) {
      // New binding - allocate slot
      declareLocalUnchecked(ident.symbol, &ident, false);
      it = scope.find(ident.symbol);
    }
    // Note the binding with the existing or new slot
    uint32_t slot = (it != scope.end()) ? it->second.slot : 0;
    noteIdentifierBinding(ident, ResolvedBinding{
        ResolvedBindingKind::Local, slot, 0, ident.symbol, false});
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      if (prop.second) resolvePatternWithBindings(*prop.second);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (const auto &elem : arrPat.elements) {
      if (elem) resolvePatternWithBindings(*elem);
    }
    if (arrPat.rest) resolvePatternWithBindings(*arrPat.rest);
    break;
  }
  case ast::NodeType::OrPattern: {
    const auto &orPat = static_cast<const ast::OrPattern &>(pattern);
    // Only resolve first alternative for binding purposes
    if (!orPat.alternatives.empty() && orPat.alternatives[0]) {
      resolvePatternWithBindings(*orPat.alternatives[0]);
    }
    break;
  }
  case ast::NodeType::SpreadPattern: {
    const auto &spreadPat = static_cast<const ast::SpreadPattern &>(pattern);
    if (spreadPat.target) resolvePatternWithBindings(*spreadPat.target);
    break;
  }
  default:
    // Literals and other non-binding patterns - nothing to resolve
    break;
  }
}

void LexicalResolver::resolveStatement(const ast::Statement &statement) {
  switch (statement.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &expr_stmt =
        static_cast<const ast::ExpressionStatement &>(statement);
    if (expr_stmt.expression) {
      resolveExpression(*expr_stmt.expression);
    }
    break;
  }

  case ast::NodeType::LetDeclaration: {
    const auto &let = static_cast<const ast::LetDeclaration &>(statement);
    if (let.value) {
      resolveExpression(*let.value);
    }

    auto *identifier = dynamic_cast<const ast::Identifier *>(let.pattern.get());
    if (identifier) {
      // Always allocate a new slot for `let` declarations.
      // This preserves lexical shadowing across nested blocks.
      uint32_t slot = declareLocal(identifier->symbol, identifier, let.isConst);
      const bool is_program_root =
          (function_stack_.size() == 1 && function_stack_.back().scopes.size() == 1);
      if (is_program_root) {
        global_variables_.insert(identifier->symbol);
        noteIdentifierBinding(
            *identifier,
            ResolvedBinding{ResolvedBindingKind::Global, 0, 0, identifier->symbol,
                            let.isConst});
      } else {
        noteIdentifierBinding(
            *identifier,
            ResolvedBinding{ResolvedBindingKind::Local, slot, 0, identifier->symbol,
                            let.isConst});
      }
    } else if (let.pattern) {
      if (let.pattern->kind == ast::NodeType::ListPattern ||
          let.pattern->kind == ast::NodeType::ArrayPattern) {
        const auto &tuple_pattern =
            static_cast<const ast::ArrayPattern &>(*let.pattern);
        for (const auto &element : tuple_pattern.elements) {
          auto *element_id =
              element ? dynamic_cast<const ast::Identifier *>(element.get())
                      : nullptr;
          if (element_id) {
            uint32_t slot =
                declareLocal(element_id->symbol, element_id, let.isConst);
            const bool is_program_root =
                (function_stack_.size() == 1 &&
                 function_stack_.back().scopes.size() == 1);
            if (is_program_root) {
              global_variables_.insert(element_id->symbol);
              noteIdentifierBinding(
                  *element_id,
                  ResolvedBinding{ResolvedBindingKind::Global, 0, 0,
                                  element_id->symbol, let.isConst});
            } else {
              noteIdentifierBinding(
                  *element_id,
                  ResolvedBinding{ResolvedBindingKind::Local, slot, 0,
                                  element_id->symbol, let.isConst});
            }
          }
        }
      } else if (let.pattern->kind == ast::NodeType::ObjectPattern) {
        const auto &objPat =
            static_cast<const ast::ObjectPattern &>(*let.pattern);
        for (const auto &prop : objPat.properties) {
          collectPatternIdentifiers(*prop.second);
        }
      }
    }
    break;
  }

  case ast::NodeType::ReturnStatement: {
    const auto &ret = static_cast<const ast::ReturnStatement &>(statement);
    if (ret.argument) {
      resolveExpression(*ret.argument);
    }
    break;
  }

  case ast::NodeType::IfStatement: {
    const auto &if_stmt = static_cast<const ast::IfStatement &>(statement);
    if (if_stmt.condition) {
      resolveExpression(*if_stmt.condition);
    }
    if (if_stmt.consequence) {
      resolveStatement(*if_stmt.consequence);
    }
    if (if_stmt.alternative) {
      resolveStatement(*if_stmt.alternative);
    }
    break;
  }

  case ast::NodeType::WhileStatement: {
    const auto &while_stmt =
        static_cast<const ast::WhileStatement &>(statement);
    if (while_stmt.condition) {
      resolveExpression(*while_stmt.condition);
    }
    if (while_stmt.body) {
      resolveStatement(*while_stmt.body);
    }
    break;
  }

  case ast::NodeType::LoopStatement: {
    const auto &loop_stmt = static_cast<const ast::LoopStatement &>(statement);
    // Resolve count expression if present (loop 5 { ... })
    if (loop_stmt.countExpr) {
      resolveExpression(*loop_stmt.countExpr);
    }
    // Resolve condition if present (loop while condition { ... })
    if (loop_stmt.condition) {
      resolveExpression(*loop_stmt.condition);
    }
    // Resolve body - loop body creates a new scope
    if (loop_stmt.body) {
      resolveStatement(*loop_stmt.body);
    }
    break;
  }

  case ast::NodeType::DoWhileStatement: {
    const auto &doWhile_stmt =
        static_cast<const ast::DoWhileStatement &>(statement);
    if (doWhile_stmt.body) {
      resolveStatement(*doWhile_stmt.body);
    }
    if (doWhile_stmt.condition) {
      resolveExpression(*doWhile_stmt.condition);
    }
    break;
  }

  case ast::NodeType::ForStatement: {
    const auto &for_stmt = static_cast<const ast::ForStatement &>(statement);
    // Resolve the iterable expression in the current scope
    if (for_stmt.iterable) {
      resolveExpression(*for_stmt.iterable);
    }
    // Create a new scope for the loop body
    beginScope();
    // Declare iterator variables in the loop scope
    for (const auto &iter : for_stmt.iterators) {
      if (iter) {
        uint32_t slot = declareLocal(iter->symbol, iter.get(), false);
      }
    }
    // Resolve the body
    if (for_stmt.body) {
      resolveStatement(*for_stmt.body);
    }
    endScope();
    break;
  }

  case ast::NodeType::BlockStatement: {
    beginScope();
    const auto &block = static_cast<const ast::BlockStatement &>(statement);
    for (const auto &nested : block.body) {
      if (nested) {
        resolveStatement(*nested);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::FunctionDeclaration: {
    const auto &fn = static_cast<const ast::FunctionDeclaration &>(statement);
    if (fn.name) {
      // Check if this is a top-level function (in program body, not nested)
      if (top_level_functions_.count(fn.name->symbol) > 0) {
        // Top-level function - create Function binding
        ResolvedBinding binding;
        binding.kind = ResolvedBindingKind::Function;
        binding.name = fn.name->symbol;
        result_.identifier_bindings[fn.name.get()] = binding;
      } else {
        // Nested function - declare as local in current (enclosing) scope
        uint32_t slot = declareLocal(fn.name->symbol, fn.name.get(), false);
        // Also record the binding for the function name itself so ByteCompiler can find it
        ResolvedBinding binding;
        binding.kind = ResolvedBindingKind::Local;
        binding.slot = slot;
        binding.name = fn.name->symbol;
        binding.is_const = false;
        result_.identifier_bindings[fn.name.get()] = binding;
      }
    }
    resolveFunctionDeclaration(fn);
    break;
  }

  case ast::NodeType::TryExpression: {
    const auto &try_stmt = static_cast<const ast::TryExpression &>(statement);
    if (try_stmt.tryBody) {
      resolveStatement(*try_stmt.tryBody);
    }
    if (try_stmt.catchBody) {
      beginScope();
      if (try_stmt.catchVariable) {
        declareLocal(try_stmt.catchVariable->symbol,
                     try_stmt.catchVariable.get(), false);
      }
      resolveStatement(*try_stmt.catchBody);
      endScope();
    }
    if (try_stmt.finallyBlock) {
      resolveStatement(*try_stmt.finallyBlock);
    }
    break;
  }

  case ast::NodeType::WhenBlockStatement: {
    const auto &when_stmt = static_cast<const ast::WhenBlock &>(statement);
    // Resolve condition
    if (when_stmt.condition) {
      resolveExpression(*when_stmt.condition);
    }
    // Resolve statements in a new scope
    beginScope();
    for (const auto &stmt : when_stmt.statements) {
      if (stmt) {
        resolveStatement(*stmt);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::ModeBlock: {
    const auto &mode_stmt = static_cast<const ast::ModeBlock &>(statement);
    // Resolve statements in a new scope
    beginScope();
    for (const auto &stmt : mode_stmt.statements) {
      if (stmt) {
        resolveStatement(*stmt);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::ThrowStatement: {
    const auto &throw_stmt =
        static_cast<const ast::ThrowStatement &>(statement);
    if (throw_stmt.value) {
      resolveExpression(*throw_stmt.value);
    }
    break;
  }

  case ast::NodeType::HotkeyBinding: {
    const auto &hotkey = static_cast<const ast::HotkeyBinding &>(statement);
    // Create isolated scope for hotkey action
    beginScope();
    if (hotkey.action) {
      resolveStatement(*hotkey.action);
    }
    endScope();
    break;
  }

  case ast::NodeType::ConditionalHotkey: {
    const auto &condHotkey =
        static_cast<const ast::ConditionalHotkey &>(statement);
    // Visit condition in current scope
    if (condHotkey.condition) {
      resolveExpression(*condHotkey.condition);
    }
    // Visit the wrapped hotkey binding (creates its own scope)
    if (condHotkey.binding) {
      resolveStatement(*condHotkey.binding);
    }
    break;
  }

  // Type system declarations - register type names in scope
  case ast::NodeType::ClassDeclaration: {
    const auto &classDecl =
        static_cast<const ast::ClassDeclaration &>(statement);
    // Create a scope for class fields so they're isolated per-class
    beginScope();
    // Declare class fields in this scope
    for (const auto &field : classDecl.definition.fields) {
      declareLocal(field.name, nullptr, false);
    }
    // Resolve class methods in the class scope so they can access fields
    for (const auto &method : classDecl.definition.methods) {
      if (method) {
        beginFunction(method.get());
        // Declare method parameters
        for (const auto &param : method->parameters) {
          if (param && param->pattern) {
            collectPatternIdentifiers(*param->pattern);
          }
        }
        // Resolve method body
        if (method->body) {
          for (const auto &stmt : method->body->body) {
            if (stmt) {
              resolveStatement(*stmt);
            }
          }
        }
        endFunction();
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::StructDeclaration: {
    const auto &structDecl =
        static_cast<const ast::StructDeclaration &>(statement);
    // Register struct type name in current scope
    // For now, just skip - full type system requires semantic analysis pass
    break;
  }

  case ast::NodeType::EnumDeclaration: {
    const auto &enumDecl = static_cast<const ast::EnumDeclaration &>(statement);
    // Register enum type name in current scope
    break;
  }

  case ast::NodeType::TraitDeclaration: {
    const auto &traitDecl =
        static_cast<const ast::TraitDeclaration &>(statement);
    // Register trait name in current scope
    break;
  }

  case ast::NodeType::ImplDeclaration: {
    const auto &implDecl = static_cast<const ast::ImplDeclaration &>(statement);
    // Register impl block - methods are added to type
    break;
  }

  // Shell command: $ cmd or $! cmd
  case ast::NodeType::ShellCommandStatement: {
    const auto &shellStmt = static_cast<const ast::ShellCommandStatement &>(statement);
    if (shellStmt.commandExpr) {
      resolveExpression(*shellStmt.commandExpr);
    }
    // Resolve pipe chain
    const ast::ShellCommandStatement *next = shellStmt.next.get();
    while (next) {
      if (next->commandExpr) {
        resolveExpression(*next->commandExpr);
      }
      next = next->next.get();
    }
    break;
  }

  // Input statement: > "text" or > {Key}
  case ast::NodeType::InputStatement: {
    const auto &inputStmt = static_cast<const ast::InputStatement &>(statement);
    for (const auto &cmd : inputStmt.commands) {
      if (!cmd.xExprStr.empty()) {
        // TODO: resolve xExprStr if it's a parsed expression
      }
      if (!cmd.yExprStr.empty()) {
        // TODO: resolve yExprStr if it's a parsed expression
      }
    }
    break;
  }

  // Wait statement: w condition
  case ast::NodeType::WaitStatement: {
    const auto &waitStmt = static_cast<const ast::WaitStatement &>(statement);
    if (waitStmt.condition) {
      resolveExpression(*waitStmt.condition);
    }
    break;
  }

  default:
    break;
  }
}

void LexicalResolver::resolveExpression(const ast::Expression &expression) {
  switch (expression.kind) {
  case ast::NodeType::Identifier: {
    const auto &id = static_cast<const ast::Identifier &>(expression);

    // Explicit global-scope reference (::x) bypasses lexical lookup.
    if (id.isGlobalScope) {
      noteIdentifierBinding(
          id, ResolvedBinding{ResolvedBindingKind::Global, 0, 0, id.symbol, false});
      break;
    }

    auto binding = resolveIdentifier(id.symbol);
    if (!binding) {
      if (top_level_structs_.count(id.symbol)) {
        noteIdentifierBinding(
            id, ResolvedBinding{ResolvedBindingKind::Global, 0, 0, id.symbol,
                                false});
        break;
      }
    }
    if (!binding) {
      errors_.push_back("Unresolved identifier '" + id.symbol + "' at " +
                        std::to_string(id.line) + ":" +
                        std::to_string(id.column));
      return;
    }
    noteIdentifierBinding(id, *binding);
    break;
  }

  case ast::NodeType::BinaryExpression: {
    const auto &binary = static_cast<const ast::BinaryExpression &>(expression);
    if (binary.left) {
      resolveExpression(*binary.left);
    }
    if (binary.right) {
      resolveExpression(*binary.right);
    }
    break;
  }

  case ast::NodeType::RangeExpression: {
    const auto &range = static_cast<const ast::RangeExpression &>(expression);
    if (range.start) {
      resolveExpression(*range.start);
    }
    if (range.end) {
      resolveExpression(*range.end);
    }
    if (range.step) {
      resolveExpression(*range.step);
    }
    break;
  }

  case ast::NodeType::CallExpression: {
    const auto &call = static_cast<const ast::CallExpression &>(expression);
    if (call.callee) {
      resolveExpression(*call.callee);
    }
    for (const auto &arg : call.args) {
      if (arg) {
        resolveExpression(*arg);
      }
    }
    for (const auto &kwarg : call.kwargs) {
      if (kwarg.value) {
        resolveExpression(*kwarg.value);
      }
    }
    break;
  }

  case ast::NodeType::AssignmentExpression: {
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(expression);

    // STEP 1: Pre-declare assignment targets before resolving RHS.
    // This ensures that lambdas in the RHS can capture variables from
    // outer scopes that are being assigned in the same function.
    if (assignment.target &&
        assignment.target->kind == ast::NodeType::Identifier) {
      const auto &ident =
          static_cast<const ast::Identifier &>(*assignment.target);
      auto binding = resolveIdentifier(ident.symbol);

      // Explicit global override (::x = ...) must never create a local.
      if (assignment.isGlobalScope || ident.isGlobalScope) {
        global_variables_.insert(ident.symbol);
        noteIdentifierBinding(
            ident, ResolvedBinding{ResolvedBindingKind::Global, 0, 0, ident.symbol, false});
        if (assignment.value) {
          resolveExpression(*assignment.value);
        }
        break;
      }

      // Declare as local if:
      // 1. No binding exists
      bool insideFunction = function_stack_.size() > 1;
      bool shouldDeclareLocal = !binding;
      // Note: If binding is Local or Upvalue, use existing (don't shadow)
      if (binding && (binding->kind == ResolvedBindingKind::Local ||
                     binding->kind == ResolvedBindingKind::Upvalue)) {
        shouldDeclareLocal = false;
      }
      // If binding is a "fake" Global (not in global_variables_), still declare local
      if (binding && binding->kind == ResolvedBindingKind::Global &&
          global_variables_.count(binding->name) == 0) {
        shouldDeclareLocal = true;
      }

      if (shouldDeclareLocal) {
        // Implicit declaration on first assignment
        bool isGlobalScope = !insideFunction;

        if (isGlobalScope) {
          // At top-level program scope - declare as global variable
          uint32_t slot = declareLocal(ident.symbol, &ident, false);
          global_variables_.insert(ident.symbol);
          ResolvedBinding newBinding;
          newBinding.kind = ResolvedBindingKind::Global;
          newBinding.slot = 0;
          newBinding.name = ident.symbol;
          newBinding.is_const = false;
          noteIdentifierBinding(ident, newBinding);
        } else {
          // Inside a function - declare as local variable.
          // If we're inside a closure, declare in the outermost
          // enclosing non-closure function so the binding is visible
          // both in the closure and in the surrounding scope.
          size_t target_fn = function_stack_.size() - 1;
          while (target_fn > 0) {
            const auto &ctx = function_stack_[target_fn];
            if (ctx.owner && ctx.owner->kind == ast::NodeType::LambdaExpression) {
              // This is a closure — skip to enclosing function
              if (target_fn == 0) break;
              target_fn--;
            } else {
              break;
            }
          }
          auto &target_ctx = function_stack_[target_fn];
          if (target_ctx.scopes.empty()) {
            beginScope();
            // beginScope was called on the wrong context, we need to
            // manually ensure the target has a scope
          }
          if (target_ctx.scopes.empty()) {
            target_ctx.scopes.emplace_back();
          }
          auto &scope = target_ctx.scopes.back();
          auto existing = scope.find(ident.symbol);
          if (existing == scope.end()) {
            uint32_t slot = target_ctx.next_slot++;
            scope[ident.symbol] =
                FunctionContext::LocalSymbol{.slot = slot, .is_const = false};
            if (result_.declaration_slots.find(&ident) == result_.declaration_slots.end()) {
              result_.declaration_slots[&ident] = slot;
            }
            ResolvedBinding newBinding;
            newBinding.kind = ResolvedBindingKind::Local;
            newBinding.slot = slot;
            newBinding.name = ident.symbol;
            newBinding.is_const = false;
            noteIdentifierBinding(ident, newBinding);
          } else {
            noteIdentifierBinding(ident, ResolvedBinding{
                ResolvedBindingKind::Local, existing->second.slot, 0,
                ident.symbol, existing->second.is_const});
          }
        }
      } else {
        noteIdentifierBinding(ident, *binding);
      }
    } else if (assignment.target &&
               assignment.target->kind == ast::NodeType::ArrayLiteral) {
      // Pre-declare destructuring targets
      const auto &arrayLit =
          static_cast<const ast::ArrayLiteral &>(*assignment.target);
      for (const auto &element : arrayLit.elements) {
        if (element && element->kind == ast::NodeType::Identifier) {
          const auto &ident = static_cast<const ast::Identifier &>(*element);
          auto binding = resolveIdentifier(ident.symbol);
          if (!binding) {
            bool isGlobalScope = (function_stack_.size() == 1);
            if (isGlobalScope) {
              uint32_t slot = declareLocal(ident.symbol, &ident, false);
              global_variables_.insert(ident.symbol);
              ResolvedBinding newBinding;
              newBinding.kind = ResolvedBindingKind::Global;
              newBinding.slot = 0;
              newBinding.name = ident.symbol;
              newBinding.is_const = false;
              noteIdentifierBinding(ident, newBinding);
            } else {
              uint32_t slot = declareLocal(ident.symbol, &ident, false);
              ResolvedBinding newBinding;
              newBinding.kind = ResolvedBindingKind::Local;
              newBinding.slot = slot;
              newBinding.name = ident.symbol;
              noteIdentifierBinding(ident, newBinding);
            }
          } else if (binding->kind == ResolvedBindingKind::Local ||
                     binding->kind == ResolvedBindingKind::Upvalue) {
            // Use existing binding, don't shadow
            noteIdentifierBinding(ident, *binding);
          } else {
            noteIdentifierBinding(ident, *binding);
          }
        }
      }
    } else if (assignment.target &&
               assignment.target->kind == ast::NodeType::ObjectLiteral) {
      // Pre-declare object destructuring targets
      const auto &objLit =
          static_cast<const ast::ObjectLiteral &>(*assignment.target);
      for (const auto &pair : objLit.pairs) {
        if (pair.second && pair.second->kind == ast::NodeType::Identifier) {
          const auto &ident =
              static_cast<const ast::Identifier &>(*pair.second);
          auto binding = resolveIdentifier(ident.symbol);
          if (!binding) {
            bool isGlobalScope = (function_stack_.size() == 1);
            if (isGlobalScope) {
              uint32_t slot = declareLocal(ident.symbol, &ident, false);
              global_variables_.insert(ident.symbol);
              ResolvedBinding newBinding;
              newBinding.kind = ResolvedBindingKind::Global;
              newBinding.slot = 0;
              newBinding.name = ident.symbol;
              newBinding.is_const = false;
              noteIdentifierBinding(ident, newBinding);
            } else {
              uint32_t slot = declareLocal(ident.symbol, &ident, false);
              ResolvedBinding newBinding;
              newBinding.kind = ResolvedBindingKind::Local;
              newBinding.slot = slot;
              newBinding.name = ident.symbol;
              noteIdentifierBinding(ident, newBinding);
            }
          } else if (binding->kind == ResolvedBindingKind::Local ||
                     binding->kind == ResolvedBindingKind::Upvalue) {
            // Use existing binding, don't shadow
            noteIdentifierBinding(ident, *binding);
          } else {
            noteIdentifierBinding(ident, *binding);
          }
        }
      }
    }

    // STEP 2: Now resolve the RHS (lambdas can find pre-declared targets)
    if (assignment.value) {
      resolveExpression(*assignment.value);
    }

    // STEP 3: Resolve any complex targets (member/index expressions)
    if (assignment.target &&
        assignment.target->kind != ast::NodeType::Identifier &&
        assignment.target->kind != ast::NodeType::ArrayLiteral &&
        assignment.target->kind != ast::NodeType::ObjectLiteral) {
      resolveExpression(*assignment.target);
    }

    break;
  }

  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expression);
    if (member.object) {
      resolveExpression(*member.object);
    }
    break;
  }

  case ast::NodeType::IndexExpression: {
    const auto &index = static_cast<const ast::IndexExpression &>(expression);
    if (index.object) {
      resolveExpression(*index.object);
    }
    if (index.index) {
      resolveExpression(*index.index);
    }
    break;
  }

  case ast::NodeType::PipelineExpression: {
    const auto &pipeline =
        static_cast<const ast::PipelineExpression &>(expression);
    // Resolve all stages in the pipeline
    for (const auto &stage : pipeline.stages) {
      if (stage) {
        resolveExpression(*stage);
      }
    }
    break;
  }

  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    for (const auto &element : array.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::TupleExpression: {
    const auto &tuple = static_cast<const ast::TupleExpression &>(expression);
    for (const auto &element : tuple.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::SetExpression: {
    const auto &set = static_cast<const ast::SetExpression &>(expression);
    for (const auto &element : set.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::ObjectLiteral: {
    const auto &object = static_cast<const ast::ObjectLiteral &>(expression);
    for (const auto &pair : object.pairs) {
      if (pair.second) {
        resolveExpression(*pair.second);
      }
    }
    break;
  }

  case ast::NodeType::SpreadExpression: {
    const auto &spread = static_cast<const ast::SpreadExpression &>(expression);
    if (spread.target) {
      resolveExpression(*spread.target);
    }
    break;
  }

  case ast::NodeType::InterpolatedStringExpression: {
    const auto &interp =
        static_cast<const ast::InterpolatedStringExpression &>(expression);
    // Resolve each expression segment in the current scope
    for (const auto &segment : interp.segments) {
      if (!segment.isString && segment.expression) {
        resolveExpression(*segment.expression);
      }
    }
    break;
  }

  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);
    resolveLambdaExpression(lambda);
    break;
  }

  // Hotkey expression: ~space => { ... } — same scoping as statement form
  case ast::NodeType::HotkeyExpression: {
    const auto &hkExpr = static_cast<const ast::HotkeyExpression &>(expression);
    if (hkExpr.binding) {
      // Resolve condition if present (e.g., ~space if mode == "x" => { ... })
      if (hkExpr.binding->conditionExpr) {
        resolveExpression(*hkExpr.binding->conditionExpr);
      }
      // Resolve the action body
      beginScope();
      if (hkExpr.binding->action) {
        resolveStatement(*hkExpr.binding->action);
      }
      endScope();
    }
    break;
  }

  // Closure expressions — resolve duration and body against current scope
  case ast::NodeType::ThreadExpression: {
    const auto &threadExpr = static_cast<const ast::ThreadExpression &>(expression);
    beginFunction(&threadExpr);
    if (threadExpr.body) {
      resolveStatement(*threadExpr.body);
    }
    endFunction();
    break;
  }
  case ast::NodeType::IntervalExpression: {
    const auto &intervalExpr = static_cast<const ast::IntervalExpression &>(expression);
    if (intervalExpr.intervalMs) resolveExpression(*intervalExpr.intervalMs);
    beginFunction(&intervalExpr);
    if (intervalExpr.body) {
      resolveStatement(*intervalExpr.body);
    }
    endFunction();
    break;
  }
  case ast::NodeType::TimeoutExpression: {
    const auto &timeoutExpr = static_cast<const ast::TimeoutExpression &>(expression);
    if (timeoutExpr.delayMs) resolveExpression(*timeoutExpr.delayMs);
    beginFunction(&timeoutExpr);
    if (timeoutExpr.body) {
      resolveStatement(*timeoutExpr.body);
    }
    endFunction();
    break;
  }

  case ast::NodeType::MatchExpression: {
    const auto &match = static_cast<const ast::MatchExpression &>(expression);
    // Resolve discriminants
    for (const auto &disc : match.discriminants) {
      if (disc) {
        resolveExpression(*disc);
      }
    }
    // Resolve each case's patterns, guard, and result expression
    for (const auto &arm : match.cases) {
      // Resolve patterns (pattern identifiers are handled specially)
      for (const auto &pattern : arm.patterns) {
        if (pattern) {
          resolvePatternWithBindings(*pattern);
        }
      }
      if (arm.guard) {
        resolveExpression(*arm.guard);
      }
      if (arm.result) {
        resolveExpression(*arm.result);
      }
    }
    // Resolve default case
    if (match.defaultCase) {
      resolveExpression(*match.defaultCase);
    }
    break;
  }

  case ast::NodeType::AwaitExpression: {
    const auto &await_expr =
        static_cast<const ast::AwaitExpression &>(expression);
    if (await_expr.argument) {
      resolveExpression(*await_expr.argument);
    }
    break;
  }

  case ast::NodeType::YieldExpression: {
    const auto &yield_expr =
        static_cast<const ast::YieldExpression &>(expression);
    if (yield_expr.value) {
      resolveExpression(*yield_expr.value);
    }
    break;
  }

  case ast::NodeType::AsyncExpression: {
    const auto &async_expr =
        static_cast<const ast::AsyncExpression &>(expression);
    if (async_expr.body) {
      resolveStatement(*async_expr.body);
    }
    break;
  }

  case ast::NodeType::UpdateExpression: {
    const auto &update_expr =
        static_cast<const ast::UpdateExpression &>(expression);
    if (update_expr.argument) {
      resolveExpression(*update_expr.argument);
    }
    break;
  }

  case ast::NodeType::UnaryExpression: {
    const auto &unary_expr =
        static_cast<const ast::UnaryExpression &>(expression);
    if (unary_expr.operand) {
      resolveExpression(*unary_expr.operand);
    }
    break;
  }

  // Pattern types - resolve nested patterns and collect bound identifiers
  case ast::NodeType::OrPattern: {
    const auto &orPat = static_cast<const ast::OrPattern &>(expression);
    for (const auto &alt : orPat.alternatives) {
      if (alt) resolveExpression(*alt);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(expression);
    for (const auto &elem : arrPat.elements) {
      if (elem) resolveExpression(*elem);
    }
    if (arrPat.rest) resolveExpression(*arrPat.rest);
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(expression);
    for (const auto &prop : objPat.properties) {
      if (prop.second) resolveExpression(*prop.second);
    }
    break;
  }
  case ast::NodeType::SpreadPattern: {
    const auto &spreadPat = static_cast<const ast::SpreadPattern &>(expression);
    if (spreadPat.target) resolveExpression(*spreadPat.target);
    break;
  }
  case ast::NodeType::RangePattern: {
    const auto &rangePat = static_cast<const ast::RangePattern &>(expression);
    if (rangePat.start) resolveExpression(*rangePat.start);
    if (rangePat.end) resolveExpression(*rangePat.end);
    break;
  }

  default:
    break;
  }
}

std::optional<ResolvedBinding>
LexicalResolver::resolveIdentifier(const std::string &name) {
  if (function_stack_.empty()) {
    return std::nullopt;
  }
  return resolveIdentifierInFunction(name, function_stack_.size() - 1);
}

std::optional<ResolvedBinding>
LexicalResolver::resolveIdentifierInFunction(const std::string &name,
                                             size_t function_index) {
  if (function_index >= function_stack_.size()) {
    return std::nullopt;
  }

  auto &ctx = function_stack_[function_index];

  // Program-root bindings that are tracked as globals should always resolve
  // as globals in __main__, even though they also have declaration slots.
  if (function_index == 0 && global_variables_.count(name) > 0) {
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  // FIRST: Search local scopes (for loop vars, nested let declarations, etc.)
  for (size_t sc = ctx.scopes.size(); sc > 0; --sc) {
    const auto &scope = ctx.scopes[sc - 1];
    auto it = scope.find(name);
    if (it != scope.end()) {
      return ResolvedBinding{
          ResolvedBindingKind::Local, it->second.slot,
          static_cast<uint32_t>(function_stack_.size() - 1 - function_index),
          name, it->second.is_const};
    }
  }

  // SECOND: If at global scope (function_index == 0), check if it's a top-level
  // function BEFORE falling back to dynamic globals.
  if (function_index == 0) {
    if (top_level_functions_.count(name) > 0) {
      return ResolvedBinding{ResolvedBindingKind::Function, 0, 0, name, false};
    }
    if (global_variables_.count(name) > 0) {
      return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
    }
    // Dynamic language: treat all other identifiers as globals
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  // THIRD: In nested function - check enclosing scope recursively
  auto enclosing = resolveIdentifierInFunction(name, function_index - 1);
  if (!enclosing) {
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  if (enclosing->kind == ResolvedBindingKind::Global) {
    return enclosing;
  }

  // If enclosing scope found a top-level function or host function, return it
  // directly without trying to capture it as an upvalue
  if (enclosing->kind == ResolvedBindingKind::Function ||
      enclosing->kind == ResolvedBindingKind::HostFunction) {
    return enclosing;
  }

  if (enclosing->kind == ResolvedBindingKind::Local) {
    uint32_t upvalue_slot =
        addUpvalue(function_index, name, enclosing->slot, true);
    return ResolvedBinding{
        ResolvedBindingKind::Upvalue, upvalue_slot,
        static_cast<uint32_t>(function_stack_.size() - 1 - function_index),
        name, enclosing->is_const};
  }

  uint32_t upvalue_slot =
      addUpvalue(function_index, name, enclosing->slot, false);
  return ResolvedBinding{
      ResolvedBindingKind::Upvalue, upvalue_slot,
      static_cast<uint32_t>(function_stack_.size() - 1 - function_index), name,
      enclosing->is_const};
}

uint32_t LexicalResolver::addUpvalue(size_t function_index,
                                     const std::string &name,
                                     uint32_t source_index,
                                     bool captures_local) {
  auto &ctx = function_stack_[function_index];
  auto it = ctx.upvalue_slots.find(name);
  if (it != ctx.upvalue_slots.end()) {
    return it->second;
  }

  uint32_t slot = static_cast<uint32_t>(ctx.upvalues.size());
  ctx.upvalues.push_back(UpvalueDescriptor{.index = source_index,
                                           .captures_local = captures_local});
  ctx.upvalue_slots[name] = slot;
  return slot;
}

void LexicalResolver::noteIdentifierBinding(const ast::Identifier &identifier,
                                            const ResolvedBinding &binding) {
  result_.identifier_bindings[&identifier] = binding;
}

} // namespace havel::compiler
