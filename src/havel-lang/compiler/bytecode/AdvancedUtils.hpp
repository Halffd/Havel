#pragma once

#include "havel-lang/ast/AST.h"
#include "havel-lang/compiler/bytecode/BytecodeIR.hpp"
#include "havel-lang/compiler/bytecode/CompilationPipeline.hpp"
#include <filesystem>
#include <memory>
#include <vector>
#include <functional>

namespace havel::compiler {

// ============================================================================
// ASTVisitor - Visitor pattern for traversing AST nodes
// ============================================================================
template<typename ReturnType = void>
class ASTVisitor {
public:
  virtual ~ASTVisitor() = default;

  // Entry point
  virtual ReturnType visit(ast::ASTNode& node) {
    return dispatch(node);
  }

  virtual ReturnType visit(const ast::ASTNode& node) {
    return dispatch(node);
  }

protected:
  // Dispatch to specific visit methods
  ReturnType dispatch(ast::ASTNode& node) {
    switch (node.kind) {
      #define VISIT_CASE(NodeType) \
        case ast::NodeType::NodeType: \
          return visit##NodeType(static_cast<ast::NodeType&>(node))

      VISIT_CASE(Program);
      VISIT_CASE(Identifier);
      VISIT_CASE(BinaryExpression);
      VISIT_CASE(UnaryExpression);
      VISIT_CASE(CallExpression);
      VISIT_CASE(AssignmentExpression);
      VISIT_CASE(MemberExpression);
      VISIT_CASE(IndexExpression);
      VISIT_CASE(LambdaExpression);
      VISIT_CASE(ArrayLiteral);
      VISIT_CASE(ObjectLiteral);
      VISIT_CASE(TernaryExpression);
      VISIT_CASE(UpdateExpression);
      VISIT_CASE(AwaitExpression);
      VISIT_CASE(SpreadExpression);
      VISIT_CASE(RangeExpression);
      VISIT_CASE(PipelineExpression);
      VISIT_CASE(InterpolatedStringExpression);
      VISIT_CASE(FunctionDeclaration);
      VISIT_CASE(ExpressionStatement);
      VISIT_CASE(LetDeclaration);
      VISIT_CASE(IfStatement);
      VISIT_CASE(WhileStatement);
      VISIT_CASE(DoWhileStatement);
      VISIT_CASE(ForStatement);
      VISIT_CASE(LoopStatement);
      VISIT_CASE(ReturnStatement);
      VISIT_CASE(BlockStatement);
      VISIT_CASE(TryExpression);
      VISIT_CASE(ThrowStatement);
      VISIT_CASE(WhenBlock);
      VISIT_CASE(ModeBlock);
      VISIT_CASE(HotkeyBinding);
      VISIT_CASE(ConditionalHotkey);
      VISIT_CASE(InputStatement);
      VISIT_CASE(UseStatement);
      VISIT_CASE(ExportStatement);
      VISIT_CASE(ArrayPattern);
      VISIT_CASE(ObjectPattern);

      #undef VISIT_CASE

      default:
        return visitDefault(node);
    }
  }

  ReturnType dispatch(const ast::ASTNode& node) {
    switch (node.kind) {
      #define VISIT_CASE(NodeType) \
        case ast::NodeType::NodeType: \
          return visit##NodeType(static_cast<const ast::NodeType&>(node))

      VISIT_CASE(Program);
      VISIT_CASE(Identifier);
      VISIT_CASE(BinaryExpression);
      VISIT_CASE(UnaryExpression);
      VISIT_CASE(CallExpression);
      VISIT_CASE(AssignmentExpression);
      VISIT_CASE(MemberExpression);
      VISIT_CASE(IndexExpression);
      VISIT_CASE(LambdaExpression);
      VISIT_CASE(ArrayLiteral);
      VISIT_CASE(ObjectLiteral);
      VISIT_CASE(TernaryExpression);
      VISIT_CASE(UpdateExpression);
      VISIT_CASE(AwaitExpression);
      VISIT_CASE(SpreadExpression);
      VISIT_CASE(RangeExpression);
      VISIT_CASE(PipelineExpression);
      VISIT_CASE(InterpolatedStringExpression);
      VISIT_CASE(FunctionDeclaration);
      VISIT_CASE(ExpressionStatement);
      VISIT_CASE(LetDeclaration);
      VISIT_CASE(IfStatement);
      VISIT_CASE(WhileStatement);
      VISIT_CASE(DoWhileStatement);
      VISIT_CASE(ForStatement);
      VISIT_CASE(LoopStatement);
      VISIT_CASE(ReturnStatement);
      VISIT_CASE(BlockStatement);
      VISIT_CASE(TryExpression);
      VISIT_CASE(ThrowStatement);
      VISIT_CASE(WhenBlock);
      VISIT_CASE(ModeBlock);
      VISIT_CASE(HotkeyBinding);
      VISIT_CASE(ConditionalHotkey);
      VISIT_CASE(InputStatement);
      VISIT_CASE(UseStatement);
      VISIT_CASE(ExportStatement);
      VISIT_CASE(ArrayPattern);
      VISIT_CASE(ObjectPattern);

      #undef VISIT_CASE

      default:
        return visitDefault(node);
    }
  }

  // Default visitor - override in subclasses
  virtual ReturnType visitDefault(ast::ASTNode& node) {
    (void)node;
    if constexpr (!std::is_void_v<ReturnType>) {
      return ReturnType{};
    }
  }

  virtual ReturnType visitDefault(const ast::ASTNode& node) {
    (void)node;
    if constexpr (!std::is_void_v<ReturnType>) {
      return ReturnType{};
    }
  }

  // Specific visit methods - override as needed
  #define DECLARE_VISIT(NodeType) \
    virtual ReturnType visit##NodeType(ast::NodeType& node) { \
      return visitDefault(node); \
    } \
    virtual ReturnType visit##NodeType(const ast::NodeType& node) { \
      return visitDefault(node); \
    }

  DECLARE_VISIT(Program)
  DECLARE_VISIT(Identifier)
  DECLARE_VISIT(BinaryExpression)
  DECLARE_VISIT(UnaryExpression)
  DECLARE_VISIT(CallExpression)
  DECLARE_VISIT(AssignmentExpression)
  DECLARE_VISIT(MemberExpression)
  DECLARE_VISIT(IndexExpression)
  DECLARE_VISIT(LambdaExpression)
  DECLARE_VISIT(ArrayLiteral)
  DECLARE_VISIT(ObjectLiteral)
  DECLARE_VISIT(TernaryExpression)
  DECLARE_VISIT(UpdateExpression)
  DECLARE_VISIT(AwaitExpression)
  DECLARE_VISIT(SpreadExpression)
  DECLARE_VISIT(RangeExpression)
  DECLARE_VISIT(PipelineExpression)
  DECLARE_VISIT(InterpolatedStringExpression)
  DECLARE_VISIT(FunctionDeclaration)
  DECLARE_VISIT(ExpressionStatement)
  DECLARE_VISIT(LetDeclaration)
  DECLARE_VISIT(IfStatement)
  DECLARE_VISIT(WhileStatement)
  DECLARE_VISIT(DoWhileStatement)
  DECLARE_VISIT(ForStatement)
  DECLARE_VISIT(LoopStatement)
  DECLARE_VISIT(ReturnStatement)
  DECLARE_VISIT(BlockStatement)
  DECLARE_VISIT(TryExpression)
  DECLARE_VISIT(ThrowStatement)
  DECLARE_VISIT(WhenBlock)
  DECLARE_VISIT(ModeBlock)
  DECLARE_VISIT(HotkeyBinding)
  DECLARE_VISIT(ConditionalHotkey)
  DECLARE_VISIT(InputStatement)
  DECLARE_VISIT(UseStatement)
  DECLARE_VISIT(ExportStatement)
  DECLARE_VISIT(ArrayPattern)
  DECLARE_VISIT(ObjectPattern)

  #undef DECLARE_VISIT
};

// ============================================================================
// ASTTransformer - Visitor that transforms AST nodes
// ============================================================================
class ASTTransformer : public ASTVisitor<std::unique_ptr<ast::ASTNode>> {
public:
  std::unique_ptr<ast::ASTNode> transform(std::unique_ptr<ast::ASTNode> node);

protected:
  // Helper to transform a list of nodes
  template<typename T>
  std::vector<std::unique_ptr<T>> transformList(
      std::vector<std::unique_ptr<T>>& nodes);

  // Override to implement transformations
  std::unique_ptr<ast::ASTNode> visitProgram(ast::Program& node) override;
  std::unique_ptr<ast::ASTNode> visitBinaryExpression(
      ast::BinaryExpression& node) override;
  std::unique_ptr<ast::ASTNode> visitUnaryExpression(
      ast::UnaryExpression& node) override;
  std::unique_ptr<ast::ASTNode> visitCallExpression(
      ast::CallExpression& node) override;
  // ... other overrides
};

// ============================================================================
// ASTCollector - Collects nodes matching a predicate
// ============================================================================
class ASTCollector : public ASTVisitor<void> {
public:
  using Predicate = std::function<bool(const ast::ASTNode&)>;

  explicit ASTCollector(Predicate pred) : predicate_(pred) {}

  const std::vector<const ast::ASTNode*>& getMatches() const { return matches_; }

protected:
  void visitDefault(const ast::ASTNode& node) override;

private:
  Predicate predicate_;
  std::vector<const ast::ASTNode*> matches_;
};

// ============================================================================
// ASTPrinter - Pretty prints AST structure
// ============================================================================
class ASTPrinter : public ASTVisitor<void> {
public:
  explicit ASTPrinter(std::ostream& output = std::cout) : output_(output) {}

  void print(const ast::ASTNode& node);

protected:
  void visitDefault(const ast::ASTNode& node) override;
  void visitProgram(const ast::Program& node) override;
  void visitIdentifier(const ast::Identifier& node) override;
  void visitBinaryExpression(const ast::BinaryExpression& node) override;
  // ... other overrides

private:
  std::ostream& output_;
  int indent_ = 0;

  void indent();
  void printIndent();
};

// ============================================================================
// ConfigManager - Compiler configuration management
// ============================================================================
class ConfigManager {
public:
  using Value = std::variant<bool, int64_t, double, std::string>;

  static ConfigManager& instance();

  // Load from file
  bool loadFromFile(const std::string& filename);
  bool saveToFile(const std::string& filename) const;

  // Get/set values
  template<typename T>
  T get(const std::string& key, const T& defaultValue = T{}) const;

  template<typename T>
  void set(const std::string& key, const T& value);

  // Check existence
  bool has(const std::string& key) const;
  void remove(const std::string& key);

  // Get all keys
  std::vector<std::string> getKeys() const;

  // Sections
  std::unique_ptr<ConfigManager> getSection(const std::string& name) const;
  void setSection(const std::string& name, const ConfigManager& section);

  // Compiler options
  CompilationPipeline::Options getPipelineOptions() const;
  void setPipelineOptions(const CompilationPipeline::Options& options);

  // Clear
  void clear();

private:
  ConfigManager() = default;
  std::unordered_map<std::string, Value> values_;
  std::unordered_map<std::string, std::unique_ptr<ConfigManager>> sections_;

  // Parsing
  Value parseValue(const std::string& str) const;
  std::string valueToString(const Value& value) const;
};

// ============================================================================
// ModuleCache - Compiled module caching system
// ============================================================================
class ModuleCache {
public:
  struct CacheEntry {
    std::filesystem::path sourcePath;
    std::filesystem::file_time_type sourceMtime;
    std::unique_ptr<BytecodeChunk> compiledChunk;
    std::string compilerVersion;
    std::chrono::system_clock::time_point cachedAt;
  };

  explicit ModuleCache(const std::filesystem::path& cacheDir);

  // Cache operations
  std::optional<const BytecodeChunk*> get(const std::filesystem::path& sourcePath);
  void put(const std::filesystem::path& sourcePath,
           std::unique_ptr<BytecodeChunk> chunk);

  // Invalidate
  void invalidate(const std::filesystem::path& sourcePath);
  void invalidateAll();

  // Cache info
  size_t size() const;
  void cleanExpired(std::chrono::hours maxAge);

  // Persistence
  bool loadFromDisk();
  bool saveToDisk();

private:
  std::filesystem::path cacheDir_;
  std::unordered_map<std::string, CacheEntry> entries_;

  std::string computeHash(const std::filesystem::path& path) const;
  bool isEntryValid(const CacheEntry& entry) const;
};

} // namespace havel::compiler
