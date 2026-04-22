/*
 * REPL.hpp
 *
 * Bytecode VM-based REPL for Havel language.
 * Provides interactive read-eval-print loop with full host API access.
 * 
 * Signal handling:
 * - Ctrl-C (SIGINT): Stops running threads/loops, clears current input
 * - Ctrl-D (EOF): Exits REPL when input is empty
 * - Ctrl-\ (SIGQUIT): Triggers panic with crash report and core dump
 */
#pragma once

#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <atomic>

namespace havel { class IHostAPI; }

namespace havel::repl {

/**
 * REPL configuration
 */
struct REPLConfig {
  bool debugMode = false;
  bool showAST = false;
  bool stopOnError = false;
  bool debugBytecode = false;
  bool debugLexer = false;
  bool debugParser = false;
  bool debugAst = false;
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
 * - Signal handling (Ctrl-C, Ctrl-D, Ctrl-\)
 *
 * Usage:
 * REPL repl(config);
 * repl.initialize(hostAPI);
 * repl.run(); // Interactive loop
 * // or
 * auto result = repl.execute("print('hello')");
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
     * Attach REPL to an existing VM and HostBridge
     * Use this when the REPL should share state with a previously executed script.
     * Skips creating a new VM — reuses the provided one.
     * @param existingVM The VM that already executed the script (non-owning)
     * @param bridge The HostBridge installed on that VM (non-owning)
     * @param globals Global variable names already defined in the VM
     */
    void attach(compiler::VM* existingVM,
                compiler::HostBridge* bridge,
                std::unordered_set<std::string> globals);

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

    /**
     * Check if interrupt was requested (Ctrl-C)
     */
    static bool isInterrupted() { return interrupted_.load(); }
    
    /**
     * Clear interrupt flag
     */
    static void clearInterrupt() { interrupted_.store(false); }

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
    void printError(const std::string& error, int line = -1, int column = 1, int length = 1, const std::string& sourceLine = "");

    /**
     * Handle special commands (exit, help, etc.)
     * @return true if command was handled, false if it's regular code
     */
    bool handleCommand(const std::string& input);

    /**
     * Show help message
     */
    void showHelp() const;

    /**
     * Setup signal handlers for REPL
     */
    void setupSignalHandlers();

    REPLConfig config_;
    bool initialized = false;

    // Bytecode VM
    std::shared_ptr<compiler::VM> vm_;
    std::unique_ptr<HostContext> hostContext_;
    std::shared_ptr<compiler::HostBridge> hostBridge_;

    // Handlers
    std::function<void(const std::string&)> printHandler_;
    std::function<std::string(const std::string&)> inputHandler_;

    // Execution context
    std::string accumulatedInput;
    std::string inputHistory; // All previous input for recompilation
    int currentLine = 0;

    // Known globals from previous executions (so compiler doesn't re-declare them)
    std::unordered_set<std::string> known_globals_;

    // Interrupt flag for Ctrl-C handling
    static std::atomic<bool> interrupted_;

    friend void replSignalHandler(int sig);
};

} // namespace havel::repl
