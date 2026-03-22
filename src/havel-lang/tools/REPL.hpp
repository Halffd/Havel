/*
 * REPL.hpp
 *
 * Bytecode VM-based REPL for Havel language.
 * Provides interactive read-eval-print loop with full host API access.
 */
#pragma once

#include "havel-lang/compiler/bytecode/VM.hpp"
#include "havel-lang/compiler/bytecode/HostBridge.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include <string>
#include <memory>
#include <functional>

namespace havel { class IHostAPI; }

namespace havel::repl {

/**
 * REPL configuration
 */
struct REPLConfig {
  bool debugMode = false;
  bool showAST = false;
  bool stopOnError = false;
  std::string prompt = "havel> ";
  std::string continuePrompt = "... ";
};

/**
 * REPL - Interactive Read-Eval-Print Loop using bytecode VM
 * 
 * Provides an interactive REPL environment with:
 * - Bytecode VM execution (not AST interpreter)
 * - Full host API access through HostBridge
 * - Multi-line input support
 * - Error handling with source context
 * - Result printing
 * 
 * Usage:
 *   REPL repl(config);
 *   repl.initialize(hostAPI);
 *   repl.run();  // Interactive loop
 *   // or
 *   auto result = repl.execute("print('hello')");
 */
class REPL {
public:
  explicit REPL(const REPLConfig& config = REPLConfig{});
  ~REPL();
  
  // Non-copyable
  REPL(const REPL&) = delete;
  REPL& operator=(const REPL&) = delete;
  
  /**
   * Initialize REPL with host API
   * Must be called before run() or execute()
   */
  void initialize(std::shared_ptr<IHostAPI> hostAPI);
  
  /**
   * Run interactive REPL loop
   * Blocks until exit command or EOF
   */
  int run();
  
  /**
   * Execute a single line/statement
   * @param code Code to execute
   * @return true on success, false on error
   */
  bool execute(const std::string& code);
  
  /**
   * Execute a file
   * @param filename Path to .hv file
   * @return true on success, false on error
   */
  bool executeFile(const std::string& filename);
  
  /**
   * Check if REPL is initialized
   */
  bool isInitialized() const { return initialized; }
  
  /**
   * Set a custom print handler
   */
  void setPrintHandler(std::function<void(const std::string&)> handler);
  
  /**
   * Set a custom input handler (for embedded use)
   */
  void setInputHandler(std::function<std::string(const std::string&)> handler);

private:
  /**
   * Read a line of input
   */
  std::string readLine(const std::string& prompt);
  
  /**
   * Check if input is complete (balanced braces, etc.)
   */
  bool isInputComplete(const std::string& input) const;
  
  /**
   * Print a value
   */
  void printValue(const std::string& value);
  
  /**
   * Print an error
   */
  void printError(const std::string& error, int line = -1);
  
  /**
   * Handle special commands (exit, help, etc.)
   * @return true if command was handled, false if it's regular code
   */
  bool handleCommand(const std::string& input);
  
  /**
   * Show help message
   */
  void showHelp() const;

  REPLConfig config_;
  bool initialized = false;
  
  // Bytecode VM
  std::unique_ptr<compiler::VM> vm_;
  std::shared_ptr<compiler::HostBridge> hostBridge_;
  
  // Handlers
  std::function<void(const std::string&)> printHandler_;
  std::function<std::string(const std::string&)> inputHandler_;
  
  // Execution context
  std::string accumulatedInput;
  int currentLine = 0;
};

} // namespace havel::repl
