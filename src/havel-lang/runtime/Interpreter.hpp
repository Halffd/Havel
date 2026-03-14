#pragma once
#include "ast/AST.h"
#include "lexer/Lexer.hpp"
#include "parser/Parser.h"
#include "types/HavelType.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "../../host/HostContext.hpp"
#include "RuntimeServices.hpp"
#include "ModuleLoader.hpp"
#include "core/io/KeyTap.hpp"
#include "semantic/SemanticAnalyzer.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel {

// Forward declarations - host managers accessed via HostContext
class IO;
class HotkeyManager;
class Environment;
class Configs;
class Interpreter;  // Forward declare for modules namespace

// HotkeyModule function - declared here to avoid circular dependency
namespace modules {
  void SetHotkeyInterpreter(std::weak_ptr<Interpreter> interp);
}

namespace ast {
struct FunctionDeclaration;
struct Program;
} // namespace ast

// Error Handling
class HavelRuntimeError : public std::runtime_error {
public:
  size_t line = 0;
  size_t column = 0;
  bool hasLocation = false;
  
  HavelRuntimeError(const std::string& msg) 
    : std::runtime_error(msg) {}
  
  HavelRuntimeError(const std::string& msg, size_t line, size_t column)
    : std::runtime_error(msg), line(line), column(column), hasLocation(true) {}
};

// Forward declare types
struct HavelValue;
struct HavelFunction;

// Recursive types now use shared_ptr for reference semantics
using HavelArray = std::shared_ptr<std::vector<HavelValue>>;
using HavelObject =
    std::shared_ptr<std::unordered_map<std::string, HavelValue>>;

// User-defined function representation (closure + declaration)
struct HavelFunction {
  std::shared_ptr<Environment> closure;
  const ast::FunctionDeclaration *declaration;

  HavelFunction(std::shared_ptr<Environment> env,
                const ast::FunctionDeclaration *decl)
      : closure(std::move(env)), declaration(decl) {}
};

// Set wrapper to distinguish from Array
struct HavelSet {
  std::shared_ptr<std::vector<HavelValue>> elements;

  HavelSet() : elements(std::make_shared<std::vector<HavelValue>>()) {}
  explicit HavelSet(std::shared_ptr<std::vector<HavelValue>> elems)
      : elements(std::move(elems)) {}
};

// Struct instance with fields and methods
struct HavelStructInstance {
  std::string typeName;
  std::shared_ptr<std::unordered_map<std::string, HavelValue>> fields;
  std::shared_ptr<HavelStructType> structType;

  HavelStructInstance(const std::string& type, std::shared_ptr<HavelStructType> st)
      : typeName(type), fields(std::make_shared<std::unordered_map<std::string, HavelValue>>()), structType(st) {}
};

// Return/break/continue value wrappers (must be defined BEFORE HavelResult)
struct ReturnValue {
  std::shared_ptr<HavelValue> value;
};

struct BreakValue {};

struct ContinueValue {};

// Result type (declare BEFORE BuiltinFunction)
using HavelResult = std::variant<HavelValue, HavelRuntimeError, ReturnValue,
                                 BreakValue, ContinueValue>;

// Function type (now HavelResult is known)
using BuiltinFunction =
    std::function<HavelResult(const std::vector<HavelValue> &)>;

// Forward declaration for Promise
// struct Promise; // Temporarily disabled to fix build

// Forward declarations
class Channel;
template <typename T> class Atomic;

// Value type for interpreter
using HavelValueBase =
    std::variant<std::nullptr_t, bool, int, double, std::string, HavelArray,
                 HavelObject, HavelSet, HavelStructInstance, std::shared_ptr<HavelFunction>,
                 std::shared_ptr<Channel>, BuiltinFunction>;

/**
 * HavelValue with optional type annotation for gradual typing
 *
 * Types are metadata only - runtime representation stays dynamic (variant-based).
 * Type checking occurs based on TypeMode:
 * - None: ignore types entirely
 * - Warn: print warnings on mismatch
 * - Strict: runtime error on mismatch
 */
struct HavelValue {
  // The actual value (variant-based) - PRIVATE, use accessors
  HavelValueBase data;
  
  // Optional type annotation for gradual typing
  std::optional<std::shared_ptr<HavelType>> annotatedType;

  // Default constructor
  HavelValue() : data(nullptr), annotatedType(std::nullopt) {}

  // Copy constructor preserves type annotation
  HavelValue(const HavelValue& other)
    : data(other.data), annotatedType(other.annotatedType) {}

  // Move constructor preserves type annotation
  HavelValue(HavelValue&& other) noexcept
    : data(std::move(other.data)), annotatedType(std::move(other.annotatedType)) {}

  // Assignment preserves type annotation
  HavelValue& operator=(const HavelValue& other) {
    data = other.data;
    annotatedType = other.annotatedType;
    return *this;
  }

  // Construct from base variant
  HavelValue(HavelValueBase&& base) : data(std::move(base)), annotatedType(std::nullopt) {}
  HavelValue(const HavelValueBase& base) : data(base), annotatedType(std::nullopt) {}

  // Convenience constructors with optional type
  HavelValue(std::nullptr_t, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(nullptr), annotatedType(type) {}
  HavelValue(bool b, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(b), annotatedType(type) {}
  HavelValue(int i, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(i), annotatedType(type) {}
  HavelValue(double d, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(d), annotatedType(type) {}
  HavelValue(const std::string& s, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(s), annotatedType(type) {}
  HavelValue(HavelArray arr, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(std::move(arr)), annotatedType(type) {}
  HavelValue(HavelObject obj, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(std::move(obj)), annotatedType(type) {}
  HavelValue(HavelSet set, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(std::move(set)), annotatedType(type) {}
  HavelValue(std::shared_ptr<HavelFunction> func, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(std::move(func)), annotatedType(type) {}
  HavelValue(BuiltinFunction func, std::optional<std::shared_ptr<HavelType>> type = std::nullopt)
    : data(std::move(func)), annotatedType(type) {}

  // ============================================================================
  // VARIANT ACCESSORS - Never access .data directly outside this class
  // ============================================================================
  
  template<typename T>
  bool is() const { return std::holds_alternative<T>(data); }
  
  template<typename T>
  const T& get() const { return std::get<T>(data); }
  
  template<typename T>
  T& get() { return std::get<T>(data); }
  
  template<typename T>
  const T* get_if() const { return std::get_if<T>(&data); }
  
  template<typename T>
  T* get_if() { return std::get_if<T>(&data); }

  // ============================================================================
  // SEMANTIC HELPERS - Intent-based access for common operations
  // ============================================================================
  
  bool isNumber() const { return is<int>() || is<double>(); }
  bool isInt() const { return is<int>(); }
  bool isDouble() const { return is<double>(); }
  bool isString() const { return is<std::string>(); }
  bool isBool() const { return is<bool>(); }
  bool isNull() const { return is<std::nullptr_t>(); }
  bool isArray() const { return is<HavelArray>(); }
  bool isObject() const { return is<HavelObject>(); }
  bool isStructInstance() const { return is<HavelStructInstance>(); }
  bool isFunction() const { return is<std::shared_ptr<HavelFunction>>() || is<BuiltinFunction>(); }

  double asNumber() const {
    if (is<double>()) return get<double>();
    if (is<int>()) return static_cast<double>(get<int>());
    return 0.0;
  }

  const std::string& asString() const { return get<std::string>(); }
  bool asBool() const { return get<bool>(); }
  HavelArray asArray() const { return get<HavelArray>(); }
  HavelObject asObject() const { return get<HavelObject>(); }
  HavelStructInstance asStructInstance() const { return get<HavelStructInstance>(); }
};

// Cooperative async scheduler
class AsyncScheduler {
public:
  struct Task {
    std::function<HavelResult()> func;
    std::string id;
    bool completed = false;
    HavelResult result;

    Task(std::function<HavelResult()> f, const std::string &name = "")
        : func(std::move(f)), id(name) {}
  };

private:
  std::queue<std::unique_ptr<Task>> taskQueue;
  std::unique_ptr<Task> currentTask;
  std::unordered_map<std::string, std::unique_ptr<Task>> waitingTasks;

public:
  static AsyncScheduler &getInstance() {
    static AsyncScheduler instance;
    return instance;
  }

  void spawn(std::function<HavelResult()> func, const std::string &name = "") {
    auto task = std::make_unique<Task>(std::move(func), name);
    taskQueue.push(std::move(task));
  }

  HavelResult await(const std::string &taskId) {
    auto it = waitingTasks.find(taskId);
    if (it != waitingTasks.end()) {
      auto task = std::move(it->second);
      waitingTasks.erase(it);
      currentTask = std::move(task);

      // Execute the task cooperatively
      while (!currentTask->completed) {
        auto result = currentTask->func();
        if (std::holds_alternative<ReturnValue>(result)) {
          // Task yielded, continue later
          break;
        } else {
          // Task completed
          currentTask->completed = true;
          currentTask->result = result;
        }
      }

      return currentTask->result;
    }
    return HavelRuntimeError("Task not found: " + taskId);
  }

  void yield() {
    // Cooperative yielding - return control to scheduler
  }

  bool step() {
    if (currentTask && !currentTask->completed) {
      auto result = currentTask->func();
      if (std::holds_alternative<ReturnValue>(result)) {
        return true; // Continue running current task
      } else {
        currentTask->completed = true;
        currentTask->result = result;
        currentTask.reset();
        return true;
      }
    }

    if (!taskQueue.empty()) {
      currentTask = std::move(taskQueue.front());
      taskQueue.pop();
      return true;
    }

    return false;
  }

  std::string getCurrentTaskId() const {
    return currentTask ? currentTask->id : "";
  }
};

// Channel for message passing
class Channel {
private:
  std::queue<HavelValue> messages;
  std::vector<std::function<void()>> waiters;

public:
  void send(const HavelValue &msg) {
    messages.push(msg);
    if (!waiters.empty()) {
      auto waiter = waiters.back();
      waiters.pop_back();
      waiter();
    }
  }

  std::function<HavelResult()> recv() {
    return [this]() -> HavelResult {
      if (messages.empty()) {
        return HavelRuntimeError("No message available");
      }
      auto msg = messages.front();
      messages.pop();
      return msg;
    };
  }

  bool isEmpty() const { return messages.empty(); }
};

// Atomic values (simple numbers and booleans only)
template <typename T> class Atomic {
private:
  T value;
  mutable std::atomic<bool> lock{false};

public:
  Atomic(T initial) : value(initial) {}

  T get() const {
    // Spin with backoff and yield to avoid 100% CPU
    int attempt = 0;
    while (lock.exchange(true)) {
      if (++attempt > 10) {
        std::this_thread::yield();  // Yield to other threads
        attempt = 0;
      }
    }
    T result = value;
    lock.store(false);
    return result;
  }

  void set(T newValue) {
    // Spin with backoff and yield to avoid 100% CPU
    int attempt = 0;
    while (lock.exchange(true)) {
      if (++attempt > 10) {
        std::this_thread::yield();  // Yield to other threads
        attempt = 0;
      }
    }
    value = newValue;
    lock.store(false);
  }

  Atomic &operator=(const T &newValue) {
    set(newValue);
    return *this;
  }

  operator T() const { return get(); }
};

// Note: Environment.hpp includes this file, so don't include it here
// to avoid circular dependency. Files that need both should include
// Environment.hpp which will transitively include this file.

// Main Interpreter class implementing the visitor pattern
class Interpreter : public ast::ASTVisitor, public std::enable_shared_from_this<Interpreter> {
  // Friend evaluators to access private members
  friend class ExprEvaluator;
  friend class StatementEvaluator;
  friend class CallDispatcher;
  friend class MemberResolver;

public:
  // Full interpreter with HostContext
  explicit Interpreter(HostContext ctx, const std::vector<std::string> &cli_args = {});

  // Minimal interpreter for pure script execution (no IO/hotkeys)
  explicit Interpreter(const std::vector<std::string> &cli_args = {});

  ~Interpreter() {
    if (m_destroyed) m_destroyed->store(true);
    environment.reset();
    lastResult = HavelValue(nullptr);
    // Clear global interpreter reference to prevent dangling pointer
    havel::modules::SetHotkeyInterpreter(std::weak_ptr<Interpreter>());
  }
  
  // Register this interpreter for hotkey callbacks (call AFTER construction)
  void RegisterForHotkeys();

  // Get environment
  std::shared_ptr<Environment>& getEnvironment() { return environment; }

  // Get HostContext for module access
  HostContext& getHostContext() { return hostContext; }
  const HostContext& getHostContext() const { return hostContext; }
  
  // Get HostAPI for updating manager pointers
  std::shared_ptr<IHostAPI> getHostAPI() { return hostAPI; }

  // Set last result
  void setLastResult(HavelResult result) { lastResult = result; }

  HavelResult Execute(const std::string &sourceCode);
  void RegisterHotkeys(const std::string &sourceCode);
  
  // Call a function value (for hotkey callbacks)
  HavelResult CallFunction(const HavelValue& func, const std::vector<HavelValue>& args);
  
  // Debug control methods
  void setStopOnError(bool stop);
  bool getStopOnError() const { return stopOnError; }
  void setShowAST(bool show);
  bool getShowAST() const { return showASTOnParse; }
  std::string getInterpreterState() const;

  // ASTVisitor interface
  void visitProgram(const ast::Program &node) override;
  void visitLetDeclaration(const ast::LetDeclaration &node) override;
  void visitIfStatement(const ast::IfStatement &node) override;
  void visitIfExpression(const ast::IfExpression &node) override;
  void visitFunctionDeclaration(const ast::FunctionDeclaration &node) override;
  void visitFunctionParameter(const ast::FunctionParameter &node) override;
  void visitReturnStatement(const ast::ReturnStatement &node) override;
  void visitHotkeyBinding(const ast::HotkeyBinding &node) override;
  void visitBlockStatement(const ast::BlockStatement &node) override;
  void visitBlockExpression(const ast::BlockExpression &node) override;
  void visitExpressionStatement(const ast::ExpressionStatement &node) override;
  void visitSleepStatement(const ast::SleepStatement &node) override;
  void visitBacktickExpression(const ast::BacktickExpression &node) override;
  void visitShellCommandExpression(const ast::ShellCommandExpression &node) override;
  void visitShellCommandStatement(const ast::ShellCommandStatement &node) override;
  void visitRepeatStatement(const ast::RepeatStatement &node) override;
  void visitInputStatement(const ast::InputStatement &node) override;
  void visitPipelineExpression(const ast::PipelineExpression &node) override;
  void visitBinaryExpression(const ast::BinaryExpression &node) override;
  void visitUnaryExpression(const ast::UnaryExpression &node) override;
  void visitUpdateExpression(const ast::UpdateExpression &node) override;
  void visitCallExpression(const ast::CallExpression &node) override;
  void visitMemberExpression(const ast::MemberExpression &node) override;
  void visitLambdaExpression(const ast::LambdaExpression &node) override;
  void visitAsyncExpression(const ast::AsyncExpression &node) override;
  void visitAwaitExpression(const ast::AwaitExpression &node) override;
  void visitStringLiteral(const ast::StringLiteral &node) override;
  void visitInterpolatedStringExpression(
      const ast::InterpolatedStringExpression &node) override;
  void visitNumberLiteral(const ast::NumberLiteral &node) override;
  void visitBooleanLiteral(const ast::BooleanLiteral &node) override;
  void visitIdentifier(const ast::Identifier &node) override;
  void visitHotkeyLiteral(const ast::HotkeyLiteral &node) override;
  void visitImportStatement(const ast::ImportStatement &node) override;
  void visitUseStatement(const ast::UseStatement &node) override;
  void visitWithStatement(const ast::WithStatement &node) override;
  void visitArrayLiteral(const ast::ArrayLiteral &node) override;
  void visitTupleExpression(const ast::TupleExpression &node) override;
  void visitObjectLiteral(const ast::ObjectLiteral &node) override;
  void visitSpreadExpression(const ast::SpreadExpression &node) override;
  void visitConfigBlock(const ast::ConfigBlock &node) override;
  
  // Helper method for nested config processing
  void processConfigPairs(
      const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs,
      Configs& config,
      const std::string& prefix);
  
  void visitDevicesBlock(const ast::DevicesBlock &node) override;
  void visitModesBlock(const ast::ModesBlock &node) override;
  void visitConfigSection(const ast::ConfigSection &node) override;
  void visitIndexExpression(const ast::IndexExpression &node) override;
  void visitTernaryExpression(const ast::TernaryExpression &node) override;
  void visitRangeExpression(const ast::RangeExpression &node) override;
  void visitSetExpression(const ast::SetExpression &node) override;
  void visitArrayPattern(const ast::ArrayPattern &node) override;
  void visitObjectPattern(const ast::ObjectPattern &node) override;
  void visitTryExpression(const ast::TryExpression &node) override;
  void visitThrowStatement(const ast::ThrowStatement &node) override;
  void visitOnModeStatement(const ast::OnModeStatement &node) override;
  void visitOffModeStatement(const ast::OffModeStatement &node) override;
  void visitOnReloadStatement(const ast::OnReloadStatement &node) override;
  void visitOnStartStatement(const ast::OnStartStatement &node) override;

  // Type system - struct/enum support
  void visitStructFieldDef(const ast::StructFieldDef &node) override;
  void visitStructMethodDef(const ast::StructMethodDef &node) override;
  void visitStructDefinition(const ast::StructDefinition &node) override;
  void visitStructDeclaration(const ast::StructDeclaration &node) override;
  void visitEnumVariantDef(const ast::EnumVariantDef &node) override;
  void visitEnumDefinition(const ast::EnumDefinition &node) override;
  void visitEnumDeclaration(const ast::EnumDeclaration &node) override;
  void visitTraitDeclaration(const ast::TraitDeclaration &node) override;
  void visitTraitMethod(const ast::TraitMethod &node) override;
  void visitImplDeclaration(const ast::ImplDeclaration &node) override;

  void
  visitAssignmentExpression(const ast::AssignmentExpression &node) override;
  void visitCastExpression(const ast::CastExpression &node) override;
  void visitMatchExpression(const ast::MatchExpression &node) override;
  void visitForStatement(const ast::ForStatement &node) override;
  void visitLoopStatement(const ast::LoopStatement &node) override;
  void visitBreakStatement(const ast::BreakStatement &node) override;
  void visitContinueStatement(const ast::ContinueStatement &node) override;
  void visitConditionalHotkey(const ast::ConditionalHotkey &node) override;
  void visitWhenBlock(const ast::WhenBlock &node) override;

  // Stubs for unused AST nodes
  void visitWhileStatement(const ast::WhileStatement &node) override;
  void visitDoWhileStatement(const ast::DoWhileStatement &node) override;
  void visitSwitchStatement(const ast::SwitchStatement &node) override;
  void visitSwitchCase(const ast::SwitchCase &node) override;
  void visitTypeDeclaration(const ast::TypeDeclaration &node) override;
  void visitTypeAnnotation(const ast::TypeAnnotation &node) override;
  void visitUnionType(const ast::UnionType &node) override;
  void visitRecordType(const ast::RecordType &node) override;
  void visitFunctionType(const ast::FunctionType &node) override;
  void visitTypeReference(const ast::TypeReference &node) override;

  // Helper methods for value conversion
  static std::string ValueToString(const HavelValue &value);
  static bool ValueToBool(const HavelValue &value);
  static double ValueToNumber(const HavelValue &value);
  static bool ExecResultToBool(const HavelResult &result);

  // Helper method for format() builtin
  std::string FormatValue(const HavelValue &value, const std::string &formatSpec);

  // Script auto-reload control (public API)
  void enableReload();
  void disableReload();
  void toggleReload();
  bool isReloadEnabled() const { return reloadEnabled.load(); }
  void setScriptPath(const std::string& path) { scriptPath = path; }
  std::string getScriptPath() const { return scriptPath; }
  void startReloadWatcher();
  void stopReloadWatcher();
  void triggerReload();

private:
  std::shared_ptr<Environment> environment;
  HostContext hostContext;  // All host managers accessed via context
  std::shared_ptr<IHostAPI> hostAPI;  // Keep HostAPI alive for module lambdas
  HavelResult lastResult;
  std::mutex interpreterMutex;

  // Runtime services (long-lived, reused for all evaluations)
  RuntimeServices services;
  semantic::SemanticAnalyzer semanticAnalyzer;

  std::vector<std::string> cliArgs;

  // Debug control options
  struct DebugOptions {
    bool lexer = false;
    bool parser = false;
    bool ast = false;
    bool bytecode = false;
    bool jit = false;
  } debug;

  // Debug control flags
  bool stopOnError = false;
  bool showASTOnParse = false;

  // KeyTap instances
  std::vector<std::unique_ptr<KeyTap>> keyTaps;

  // Keep parsed programs alive
  std::vector<std::unique_ptr<ast::Program>> loadedPrograms;

  // Shared atomic flag for conditional hotkey lambdas
  std::shared_ptr<std::atomic<bool>> m_destroyed;

  // Script auto-reload support
  std::string scriptPath;
  std::atomic<bool> reloadEnabled{false};
  std::atomic<bool> isFirstRun{true};
  std::filesystem::file_time_type lastModifiedTime;
  std::thread reloadWatcherThread;
  std::atomic<bool> reloadWatcherRunning{false};  // Watcher thread control
  std::mutex reloadMutex;  // Protect reload state
  
  // on reload/on start handlers
  std::function<void()> onReloadHandler;
  std::function<void()> onStartHandler;
  std::unordered_map<std::string, bool> runOnceExecuted;  // Track runOnce execution

  int nextTimerId = 1;
  std::unordered_map<int, std::shared_ptr<std::atomic<bool>>> timers;
  std::mutex timersMutex; // Thread safety for timer operations

  HavelResult Evaluate(const ast::ASTNode &node);
  
  // Type system helpers
  std::shared_ptr<HavelType> resolveType(const ast::TypeDefinition& typeDef);

  // KeyTap constructor for advanced hotkey functionality
  KeyTap* createKeyTap(
      const std::string &keyName, std::function<void()> onTap,
      std::variant<std::string, std::function<bool()>> tapCondition = {},
      std::variant<std::string, std::function<bool()>> comboCondition = {},
      std::function<void()> onCombo = nullptr, bool grabDown = true,
      bool grabUp = true);

  // Get shared pointer to destroyed flag for safe lambda capture
  std::shared_ptr<std::atomic<bool>> getDestroyedFlag() const { return m_destroyed; }

  // Reload helper methods
  void executeOnStart();
  void executeOnReload();
  bool hasRunOnce(const std::string& id);
  void markRunOnce(const std::string& id);
  void clearRunOnce(const std::string& id);

public:
  // Error formatting helpers - public for use by HavelLauncher
  std::string formatErrorWithLocation(const std::string& message, size_t line, size_t column, const std::string& sourceCode);
  void printError(const HavelResult& error, const std::string& sourceCode);
  void printSourceWithContext(const std::string& sourceCode, size_t errorLine);

  // Debug options setter - public for use by HavelLauncher
  void setDebugParser(bool enable) { debug.parser = enable; }
};

} // namespace havel
