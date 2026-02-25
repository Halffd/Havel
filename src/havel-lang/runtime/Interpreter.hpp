#pragma once
#include "ast/AST.h"
#include "core/ConfigManager.hpp"
#include "core/IO.hpp"
#include "core/io/KeyTap.hpp"
#include "lexer/Lexer.hpp"
#include "parser/Parser.h"
#include "types/HavelType.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "window/Window.hpp"
#include "window/WindowManager.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel {

// Forward declarations for managers
class HotkeyManager;
class BrightnessManager;
class AudioManager;
class GUIManager;
class ScreenshotManager;
class ClipboardManager;

// Forward declarations
class Environment;
namespace ast {
struct FunctionDeclaration;
struct Program;
} // namespace ast

// Error Handling
class HavelRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
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
                 HavelObject, HavelSet, std::shared_ptr<HavelFunction>,
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
    while (lock.exchange(true)) {
    } // Spin wait
    T result = value;
    lock.store(false);
    return result;
  }

  void set(T newValue) {
    while (lock.exchange(true)) {
    } // Spin wait
    value = newValue;
    lock.store(false);
  }

  Atomic &operator=(const T &newValue) {
    set(newValue);
    return *this;
  }

  operator T() const { return get(); }
};

// Environment class
class Environment {
public:
  Environment(std::shared_ptr<Environment> parentEnv = nullptr)
      : parent(parentEnv) {}

  void Define(const std::string &name, const HavelValue &value) {
    values[name] = value;
  }

  std::optional<HavelValue> Get(const std::string &name) const {
    auto it = values.find(name);
    if (it != values.end()) {
      return it->second;
    }
    if (parent) {
      return parent->Get(name);
    }
    return std::nullopt;
  }

  bool Assign(const std::string &name, const HavelValue &value) {
    auto it = values.find(name);
    if (it != values.end()) {
      values[name] = value;
      return true;
    }
    if (parent) {
      return parent->Assign(name, value);
    }
    return false; // Variable not found
  }

private:
  std::shared_ptr<Environment> parent;
  std::unordered_map<std::string, HavelValue> values;
};

// Main Interpreter class implementing the visitor pattern
class Interpreter : public ast::ASTVisitor, public std::enable_shared_from_this<Interpreter> {
public:
  // Full interpreter with IO and all managers
  Interpreter(IO &io_system, WindowManager &window_mgr,
              HotkeyManager *hotkey_mgr = nullptr,
              BrightnessManager *brightness_mgr = nullptr,
              AudioManager *audio_mgr = nullptr, GUIManager *gui_mgr = nullptr,
              ScreenshotManager *screenshot_mgr = nullptr,
              ClipboardManager *clipboard_mgr = nullptr,
              const std::vector<std::string> &cli_args = {});
  
  // Minimal interpreter for pure script execution (no IO/hotkeys)
  explicit Interpreter(const std::vector<std::string> &cli_args = {});

  ~Interpreter() { 
    if (m_destroyed) m_destroyed->store(true);
    // Explicitly clear environment and lastResult to ensure proper cleanup
    environment.reset();
    lastResult = HavelValue(nullptr);  // Clear any held references
  }

  HavelResult Execute(const std::string &sourceCode);
  void RegisterHotkeys(const std::string &sourceCode);

  // ASTVisitor interface
  void visitProgram(const ast::Program &node) override;
  void visitLetDeclaration(const ast::LetDeclaration &node) override;
  void visitIfStatement(const ast::IfStatement &node) override;
  void visitFunctionDeclaration(const ast::FunctionDeclaration &node) override;
  void visitReturnStatement(const ast::ReturnStatement &node) override;
  void visitHotkeyBinding(const ast::HotkeyBinding &node) override;
  void visitBlockStatement(const ast::BlockStatement &node) override;
  void visitExpressionStatement(const ast::ExpressionStatement &node) override;
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
  void visitIdentifier(const ast::Identifier &node) override;
  void visitHotkeyLiteral(const ast::HotkeyLiteral &node) override;
  void visitImportStatement(const ast::ImportStatement &node) override;
  void visitUseStatement(const ast::UseStatement &node) override;
  void visitWithStatement(const ast::WithStatement &node) override;
  void visitArrayLiteral(const ast::ArrayLiteral &node) override;
  void visitObjectLiteral(const ast::ObjectLiteral &node) override;
  void visitConfigBlock(const ast::ConfigBlock &node) override;
  
  // Helper method for nested config processing
  void processConfigPairs(
      const std::vector<std::pair<std::string, std::unique_ptr<ast::Expression>>>& pairs,
      Configs& config,
      const std::string& prefix);
  
  void visitDevicesBlock(const ast::DevicesBlock &node) override;
  void visitModesBlock(const ast::ModesBlock &node) override;
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
  void visitStructDefinition(const ast::StructDefinition &node) override;
  void visitStructDeclaration(const ast::StructDeclaration &node) override;
  void visitEnumVariantDef(const ast::EnumVariantDef &node) override;
  void visitEnumDefinition(const ast::EnumDefinition &node) override;
  void visitEnumDeclaration(const ast::EnumDeclaration &node) override;

  void
  visitAssignmentExpression(const ast::AssignmentExpression &node) override;
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
  IO *io;
  WindowManager *windowManager;
  HotkeyManager *hotkeyManager;
  BrightnessManager *brightnessManager;
  AudioManager *audioManager;
  GUIManager *guiManager;
  ScreenshotManager *screenshotManager;
  ClipboardManager *clipboardManager;
  HavelResult lastResult;
  std::mutex interpreterMutex; // Protect interpreter state

  std::vector<std::string> cliArgs;

  // Debug control options
  struct DebugOptions {
    bool lexer = false;
    bool parser = false;
    bool ast = false;
    bool bytecode = false;
    bool jit = false;
  } debug;

  // KeyTap instances for advanced hotkey functionality
  std::vector<std::unique_ptr<KeyTap>> keyTaps;

  // Keep parsed programs alive for function declarations captured by closures
  std::vector<std::unique_ptr<ast::Program>> loadedPrograms;

  // Shared atomic flag to prevent use-after-free in conditional hotkey lambdas
  // Using shared_ptr ensures the flag outlives the Interpreter and can be safely checked by lambdas
  std::shared_ptr<std::atomic<bool>> m_destroyed;

  // Script auto-reload support
  std::string scriptPath;  // Path to current script file
  std::atomic<bool> reloadEnabled{false};  // Auto-reload enabled state
  std::atomic<bool> isFirstRun{true};  // Track if this is first run vs reload
  std::filesystem::file_time_type lastModifiedTime;  // Last known file modification time
  std::thread reloadWatcherThread;  // File watcher thread
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

  // KeyTap constructor for advanced hotkey functionality
  KeyTap* createKeyTap(
      const std::string &keyName, std::function<void()> onTap,
      std::variant<std::string, std::function<bool()>> tapCondition = {},
      std::variant<std::string, std::function<bool()>> comboCondition = {},
      std::function<void()> onCombo = nullptr, bool grabDown = true,
      bool grabUp = true);

  void InitializeStandardLibrary();
  void InitializeSystemBuiltins();
  void InitializeWindowBuiltins();
  void InitializeClipboardBuiltins();
  void InitializeTextBuiltins();
  void InitializeFileBuiltins();
  void InitializeArrayBuiltins();
  void InitializeIOBuiltins();
  void InitializeBrightnessBuiltins();
  void InitializeMathBuiltins();
  void InitializeRegexBuiltins();
  void InitializeDebugBuiltins();
  void InitializeAudioBuiltins();
  void InitializeMediaBuiltins();
  void InitializeFileManagerBuiltins();
  void InitializeLauncherBuiltins();
  void InitializeGUIBuiltins();
  void InitializeScreenshotBuiltins();
  void InitializeTimerBuiltins();
  void InitializeAutomationBuiltins();
  void InitializeAsyncBuiltins();
  void InitializePhysicsBuiltins();
  void InitializeHelpBuiltin();

  // Get shared pointer to destroyed flag for safe lambda capture
  std::shared_ptr<std::atomic<bool>> getDestroyedFlag() const { return m_destroyed; }
  
  // Reload helper methods (private)
  void executeOnStart();
  void executeOnReload();
  bool hasRunOnce(const std::string& id);
  void markRunOnce(const std::string& id);
  void clearRunOnce(const std::string& id);
};

} // namespace havel
