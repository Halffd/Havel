#include "CompilationPipeline.hpp"
#include "Lexer.hpp"
#include "Parser.h"
#include "AST.h"
#include "ModuleLoader.hpp"
#include <chrono>

namespace havel::compiler {

// ============================================================================
// CompilationPipeline Implementation
// ============================================================================

CompilationPipeline::CompilationPipeline(const Options& options) : options_(options) {
  currentChunk_ = std::make_unique<BytecodeChunk>();
  codeEmitter_ = std::make_unique<CodeEmitter>(*currentChunk_);
  bindingResolver_ = std::make_unique<BindingResolver>(lexicalResult_);
  patternCompiler_ = std::make_unique<PatternCompiler>(*codeEmitter_, *bindingResolver_);
  exprCompiler_ = std::make_unique<ExpressionCompiler>(*codeEmitter_, *bindingResolver_, lexicalResult_);
  stmtCompiler_ = std::make_unique<StatementCompiler>(*codeEmitter_, *exprCompiler_, *bindingResolver_);
  funcCompiler_ = std::make_unique<FunctionCompiler>(*codeEmitter_, *stmtCompiler_, *exprCompiler_,
                                                      *patternCompiler_, *bindingResolver_);
}

CompilationPipeline::~CompilationPipeline() = default;

CompilationPipeline::Result CompilationPipeline::compile(const std::string& source,
                                                         const std::string& filename) {
  auto startTime = std::chrono::steady_clock::now();

  Result result;
  lastStages_.clear();

  // Stage 1: Lexing
  if (!lexingStage(source, filename)) {
    result.errors.push_back("Lexing failed");
    return result;
  }

  // Stage 2: Parsing
  if (!parsingStage()) {
    result.errors.push_back("Parsing failed");
    return result;
  }

  // Stage 3: Resolution
  if (!resolutionStage()) {
    result.errors.push_back("Resolution failed");
    return result;
  }

  // Stage 4: Compilation
  if (!compilationStage()) {
    result.errors.push_back("Compilation failed");
    return result;
  }

  // Stage 5: Optimization
  if (options_.enableOptimizations) {
    if (!optimizationStage()) {
      result.warnings.push_back("Optimization failed, continuing with unoptimized code");
    }
  }

  auto endTime = std::chrono::steady_clock::now();
  result.compilationTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
  result.success = true;
  result.chunk = std::move(currentChunk_);

  return result;
}

CompilationPipeline::Result CompilationPipeline::compileFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    Result result;
    result.errors.push_back("Cannot open file: " + path.string());
    return result;
  }

  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

  return compile(source, path.string());
}

CompilationPipeline::Result CompilationPipeline::compileWithStages(const std::string& source,
                                                                    const std::string& filename) {
  Result result;
  lastStages_.clear();

  auto runStage = [&](const std::string& name, auto&& func) -> bool {
    Stage stage;
    stage.name = name;

    auto start = std::chrono::steady_clock::now();
    stage.success = func();
    auto end = std::chrono::steady_clock::now();

    stage.timeMs = std::chrono::duration<double, std::milli>(end - start).count();
    lastStages_.push_back(stage);

    return stage.success;
  };

  if (!runStage("lexing", [&]() { return lexingStage(source, filename); })) {
    result.errors.push_back("Lexing failed");
    return result;
  }

  if (!runStage("parsing", [&]() { return parsingStage(); })) {
    result.errors.push_back("Parsing failed");
    return result;
  }

  if (!runStage("resolution", [&]() { return resolutionStage(); })) {
    result.errors.push_back("Resolution failed");
    return result;
  }

  if (!runStage("compilation", [&]() { return compilationStage(); })) {
    result.errors.push_back("Compilation failed");
    return result;
  }

  if (options_.enableOptimizations) {
    if (!runStage("optimization", [&]() { return optimizationStage(); })) {
      result.warnings.push_back("Optimization failed");
    }
  }

  result.success = true;
  result.chunk = std::move(currentChunk_);
  return result;
}

bool CompilationPipeline::lexingStage(const std::string& source, const std::string& filename) {
  (void)filename;
  // TODO: Implement lexing using Lexer
  // For now, assume success - the actual lexer would tokenize 'source'
  return !source.empty();
}

bool CompilationPipeline::parsingStage() {
  // TODO: Implement parsing using Parser
  // For now, assume success
  return true;
}

bool CompilationPipeline::resolutionStage() {
  if (!ast_) {
    return false;
  }

  bindingResolver_->beginFunction(nullptr); // Global scope

  // Collect top-level functions
  for (const auto& stmt : ast_->body) {
    if (stmt && stmt->kind == ast::NodeType::FunctionDeclaration) {
      const auto& func = static_cast<const ast::FunctionDeclaration&>(*stmt);
      if (func.name) {
        bindingResolver_->declareTopLevelFunction(func.name->symbol);
      }
    }
  }

  // Resolve all statements
  for (const auto& stmt : ast_->body) {
    if (stmt) {
      // This would resolve identifiers in the statement
      // Implementation depends on how we want to traverse the AST
    }
  }

  bindingResolver_->endFunction();
  return !bindingResolver_->hasErrors();
}

bool CompilationPipeline::compilationStage() {
  if (!ast_) {
    return false;
  }

  // Compile top-level statements
  for (const auto& stmt : ast_->body) {
    if (stmt) {
      stmtCompiler_->compile(*stmt);
    }
  }

  return true;
}

bool CompilationPipeline::optimizationStage() {
  BytecodeOptimizer optimizer(*currentChunk_);
  optimizer.optimize(options_.maxOptimizationPasses);
  return true;
}

// ============================================================================
// BytecodeOptimizer Implementation
// ============================================================================

BytecodeOptimizer::BytecodeOptimizer(BytecodeChunk& chunk) : chunk_(chunk) {}

BytecodeOptimizer::Stats BytecodeOptimizer::optimize(size_t maxPasses) {
  resetStats();

  for (size_t pass = 0; pass < maxPasses; ++pass) {
    size_t previousRemoved = stats_.instructionsRemoved;

    constantFolding();
    deadCodeElimination();
    jumpOptimization();
    peepholeOptimization();

    // If no changes in this pass, we're done
    if (stats_.instructionsRemoved == previousRemoved) {
      break;
    }
  }

  return stats_;
}

void BytecodeOptimizer::constantFolding() {
  for (auto& function : chunk_.functions) {
    for (size_t i = 0; i < function.instructions.size(); ++i) {
      auto& instr = function.instructions[i];

      // Look for binary operations with constant operands
      if (instr.opcode == OpCode::LOAD_CONST) {
        // Check if next two instructions are also LOAD_CONST followed by a binary op
        if (i + 2 < function.instructions.size()) {
          auto& next = function.instructions[i + 1];
          auto& after = function.instructions[i + 2];

          if (next.opcode == OpCode::LOAD_CONST &&
              (after.opcode >= OpCode::ADD && after.opcode <= OpCode::POW)) {

            auto result = evaluateBinaryOp(after.opcode,
                                          instr.operands[0],
                                          next.operands[0]);

            if (result) {
              // Replace three instructions with one constant load
              instr.operands[0] = *result;
              removeInstruction(function.id, i + 1);
              removeInstruction(function.id, i + 1); // i+2 is now i+1
              stats_.instructionsRemoved += 2;
              stats_.constantsFolded++;
            }
          }
        }
      }
    }
  }
}

void BytecodeOptimizer::deadCodeElimination() {
  for (auto& function : chunk_.functions) {
    for (size_t i = 0; i < function.instructions.size(); ++i) {
      const auto& instr = function.instructions[i];

      // Remove POP immediately after LOAD_CONST (unused constant)
      if (instr.opcode == OpCode::LOAD_CONST &&
          i + 1 < function.instructions.size() &&
          function.instructions[i + 1].opcode == OpCode::POP) {
        removeInstruction(function.id, i);
        removeInstruction(function.id, i); // POP is now at i
        stats_.instructionsRemoved += 2;
        stats_.deadCodeEliminated++;
        --i; // Adjust index
      }

      // Remove unreachable code after unconditional jump/return
      if (instr.opcode == OpCode::RETURN || instr.opcode == OpCode::JUMP) {
        // Find next label or function end
        size_t unreachableStart = i + 1;
        size_t unreachableEnd = unreachableStart;

        for (size_t j = unreachableStart; j < function.instructions.size(); ++j) {
          // Simple heuristic: stop at label-like patterns
          // In real implementation, we'd track actual jump targets
          if (function.instructions[j].opcode == OpCode::LABEL) {
            break;
          }
          unreachableEnd = j;
        }

        if (unreachableEnd > unreachableStart) {
          // Remove unreachable instructions
          for (size_t j = unreachableEnd; j >= unreachableStart; --j) {
            removeInstruction(function.id, j);
          }
          stats_.instructionsRemoved += (unreachableEnd - unreachableStart + 1);
        }
      }
    }
  }
}

void BytecodeOptimizer::jumpOptimization() {
  for (auto& function : chunk_.functions) {
    for (size_t i = 0; i < function.instructions.size(); ++i) {
      auto& instr = function.instructions[i];

      // Jump to next instruction (can be eliminated)
      if (instr.opcode == OpCode::JUMP && !instr.operands.empty()) {
        uint32_t target = std::get<uint32_t>(instr.operands[0]);
        if (target == i + 1) {
          removeInstruction(function.id, i);
          stats_.instructionsRemoved++;
          stats_.jumpsOptimized++;
          --i;
        }
      }

      // Jump over unconditional jump (can be folded)
      if ((instr.opcode == OpCode::JUMP_IF_FALSE || instr.opcode == OpCode::JUMP_IF_TRUE) &&
          i + 1 < function.instructions.size()) {
        auto& next = function.instructions[i + 1];
        if (next.opcode == OpCode::JUMP) {
          // Invert condition and jump further
          // This is a more complex optimization that would need careful implementation
        }
      }
    }
  }
}

void BytecodeOptimizer::peepholeOptimization() {
  for (auto& function : chunk_.functions) {
    for (size_t i = 0; i + 1 < function.instructions.size(); ++i) {
      const auto& instr = function.instructions[i];
      const auto& next = function.instructions[i + 1];

      // LOAD_VAR followed by STORE_VAR on same slot (useless)
      if (instr.opcode == OpCode::LOAD_VAR && next.opcode == OpCode::STORE_VAR) {
        uint32_t loadSlot = std::get<uint32_t>(instr.operands[0]);
        uint32_t storeSlot = std::get<uint32_t>(next.operands[0]);
        if (loadSlot == storeSlot) {
          // This pattern shouldn't normally happen, but if it does:
          removeInstruction(function.id, i);
          removeInstruction(function.id, i);
          stats_.instructionsRemoved += 2;
          --i;
        }
      }

      // DUP followed by POP (cancel out)
      if (instr.opcode == OpCode::DUP && next.opcode == OpCode::POP) {
        removeInstruction(function.id, i);
        removeInstruction(function.id, i);
        stats_.instructionsRemoved += 2;
        --i;
      }

      // LOAD_CONST 0 followed by ADD/SUB (no effect)
      if (instr.opcode == OpCode::LOAD_CONST &&
          (next.opcode == OpCode::ADD || next.opcode == OpCode::SUB)) {
        if (std::holds_alternative<int64_t>(instr.operands[0]) &&
            std::get<int64_t>(instr.operands[0]) == 0) {
          removeInstruction(function.id, i);
          removeInstruction(function.id, i);
          stats_.instructionsRemoved += 2;
          --i;
        }
      }
    }
  }
}

void BytecodeOptimizer::registerAllocation() {
  // This is a placeholder for more advanced register allocation
  // In a real implementation, this would:
  // 1. Perform liveness analysis
  // 2. Build interference graph
  // 3. Allocate registers using graph coloring
  // 4. Spill to locals when necessary
}

bool BytecodeOptimizer::isConstant(const BytecodeValue& value) const {
  // Check if value is a compile-time constant
  return std::holds_alternative<int64_t>(value) ||
         std::holds_alternative<double>(value) ||
         std::holds_alternative<bool>(value) ||
         std::holds_alternative<std::string>(value) ||
         std::holds_alternative<nullptr_t>(value);
}

std::optional<BytecodeValue> BytecodeOptimizer::evaluateBinaryOp(
    OpCode op,
    const BytecodeValue& left,
    const BytecodeValue& right) const {

  // Integer operations
  if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
    int64_t l = std::get<int64_t>(left);
    int64_t r = std::get<int64_t>(right);

    switch (op) {
      case OpCode::ADD: return l + r;
      case OpCode::SUB: return l - r;
      case OpCode::MUL: return l * r;
      case OpCode::DIV: return r != 0 ? l / r : 0;
      case OpCode::MOD: return r != 0 ? l % r : 0;
      case OpCode::EQ: return l == r;
      case OpCode::NE: return l != r;
      case OpCode::LT: return l < r;
      case OpCode::LE: return l <= r;
      case OpCode::GT: return l > r;
      case OpCode::GE: return l >= r;
      default: return std::nullopt;
    }
  }

  // Double operations
  if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
    double l = std::get<double>(left);
    double r = std::get<double>(right);

    switch (op) {
      case OpCode::ADD: return l + r;
      case OpCode::SUB: return l - r;
      case OpCode::MUL: return l * r;
      case OpCode::DIV: return r != 0.0 ? l / r : 0.0;
      case OpCode::EQ: return l == r;
      case OpCode::NE: return l != r;
      case OpCode::LT: return l < r;
      case OpCode::LE: return l <= r;
      case OpCode::GT: return l > r;
      case OpCode::GE: return l >= r;
      default: return std::nullopt;
    }
  }

  // String concatenation
  if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
    if (op == OpCode::ADD) {
      return std::get<std::string>(left) + std::get<std::string>(right);
    }
  }

  return std::nullopt;
}

void BytecodeOptimizer::removeInstruction(size_t functionIndex, size_t instructionIndex) {
  if (functionIndex >= chunk_.functions.size()) return;
  auto& function = chunk_.functions[functionIndex];
  if (instructionIndex >= function.instructions.size()) return;

  function.instructions.erase(function.instructions.begin() + instructionIndex);
}

void BytecodeOptimizer::replaceInstruction(size_t functionIndex,
                                           size_t instructionIndex,
                                           const Instruction& newInstruction) {
  if (functionIndex >= chunk_.functions.size()) return;
  auto& function = chunk_.functions[functionIndex];
  if (instructionIndex >= function.instructions.size()) return;

  function.instructions[instructionIndex] = newInstruction;
  stats_.instructionsModified++;
}

// ============================================================================
// ASTValidator Implementation
// ============================================================================

ASTValidator::ASTValidator(bool strictMode) : strictMode_(strictMode) {}

bool ASTValidator::validate(const ast::Program& program) {
  errors_.clear();
  scopeStack_.clear();
  inLoop_ = false;
  inFunction_ = false;

  // Enter global scope
  scopeStack_.emplace_back();

  for (const auto& stmt : program.body) {
    if (stmt) {
      validateStatement(*stmt);
    }
  }

  return !hasErrors();
}

bool ASTValidator::validateExpression(const ast::Expression& expr) {
  (void)expr;
  // TODO: Implement expression validation
  return true;
}

bool ASTValidator::validateStatement(const ast::Statement& stmt) {
  (void)stmt;
  // TODO: Implement statement validation
  return true;
}

void ASTValidator::addError(const std::string& message, const ast::ASTNode& node) {
  ValidationError error;
  error.message = message;
  error.line = node.line;
  error.column = node.column;
  errors_.push_back(error);
}

// ============================================================================
// SymbolTable Implementation
// ============================================================================

SymbolTable::SymbolTable() = default;

SymbolTable::SymbolTable(std::shared_ptr<SymbolTable> parent)
    : parent_(parent), depth_(parent ? parent->depth_ + 1 : 0) {
  if (parent) {
    nextSlot_ = parent->nextSlot_;
  }
}

std::shared_ptr<SymbolTable> SymbolTable::enterScope() {
  return std::make_shared<SymbolTable>(shared_from_this());
}

std::shared_ptr<SymbolTable> SymbolTable::exitScope() {
  return parent_;
}

bool SymbolTable::declare(const std::string& name, Symbol::Kind kind,
                          const ast::ASTNode* declaration) {
  if (symbols_.count(name) > 0) {
    return false; // Already declared in this scope
  }

  Symbol sym;
  sym.name = name;
  sym.kind = kind;
  sym.scopeDepth = depth_;
  sym.declaration = declaration;

  if (kind == Symbol::Kind::Variable || kind == Symbol::Kind::Parameter) {
    sym.slot = allocateSlot();
  }

  symbols_[name] = sym;
  return true;
}

std::optional<SymbolTable::Symbol> SymbolTable::lookup(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it != symbols_.end()) {
    return it->second;
  }

  if (parent_) {
    return parent_->lookup(name);
  }

  return std::nullopt;
}

std::optional<SymbolTable::Symbol> SymbolTable::lookupLocal(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it != symbols_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool SymbolTable::hasSymbol(const std::string& name) const {
  return lookup(name).has_value();
}

bool SymbolTable::markAsCaptured(const std::string& name) {
  auto it = symbols_.find(name);
  if (it != symbols_.end()) {
    it->second.isCaptured = true;
    return true;
  }
  return false;
}

bool SymbolTable::markAsConst(const std::string& name) {
  auto it = symbols_.find(name);
  if (it != symbols_.end()) {
    it->second.isConst = true;
    return true;
  }
  return false;
}

std::vector<SymbolTable::Symbol> SymbolTable::getAllSymbols() const {
  std::vector<Symbol> result;

  // Get parent symbols first
  if (parent_) {
    result = parent_->getAllSymbols();
  }

  // Add local symbols
  for (const auto& [name, sym] : symbols_) {
    (void)name;
    result.push_back(sym);
  }

  return result;
}

std::vector<SymbolTable::Symbol> SymbolTable::getLocalSymbols() const {
  std::vector<Symbol> result;
  for (const auto& [name, sym] : symbols_) {
    (void)name;
    result.push_back(sym);
  }
  return result;
}

uint32_t SymbolTable::allocateSlot() {
  return nextSlot_++;
}

// ============================================================================
// ParserUtils Implementation
// ============================================================================

bool ParserUtils::isLiteral(const Token& token) {
  switch (token.type) {
    case TokenType::NUMBER:
    case TokenType::STRING:
    case TokenType::TRUE:
    case TokenType::FALSE:
    case TokenType::NIL:
      return true;
    default:
      return false;
  }
}

bool ParserUtils::isOperator(const Token& token) {
  switch (token.type) {
    case TokenType::PLUS:
    case TokenType::MINUS:
    case TokenType::STAR:
    case TokenType::SLASH:
    case TokenType::PERCENT:
    case TokenType::EQ:
    case TokenType::NE:
    case TokenType::LT:
    case TokenType::LE:
    case TokenType::GT:
    case TokenType::GE:
    case TokenType::AND:
    case TokenType::OR:
    case TokenType::ASSIGN:
    case TokenType::PLUS_ASSIGN:
    case TokenType::MINUS_ASSIGN:
      return true;
    default:
      return false;
  }
}

bool ParserUtils::isKeyword(const Token& token) {
  switch (token.type) {
    case TokenType::FN:
    case TokenType::LET:
    case TokenType::CONST:
    case TokenType::IF:
    case TokenType::ELSE:
    case TokenType::WHILE:
    case TokenType::FOR:
    case TokenType::RETURN:
    case TokenType::TRUE:
    case TokenType::FALSE:
    case TokenType::NIL:
    case TokenType::AND:
    case TokenType::OR:
      return true;
    default:
      return false;
  }
}

int ParserUtils::getPrecedence(TokenType type) {
  switch (type) {
    case TokenType::OR:          return 1;
    case TokenType::AND:         return 2;
    case TokenType::EQ:
    case TokenType::NE:          return 3;
    case TokenType::LT:
    case TokenType::LE:
    case TokenType::GT:
    case TokenType::GE:          return 4;
    case TokenType::PLUS:
    case TokenType::MINUS:       return 5;
    case TokenType::STAR:
    case TokenType::SLASH:
    case TokenType::PERCENT:     return 6;
    case TokenType::POWER:       return 7;
    default:                     return 0;
  }
}

bool ParserUtils::isRightAssociative(TokenType type) {
  return type == TokenType::POWER || type == TokenType::ASSIGN;
}

} // namespace havel::compiler
