/*
 * REPL.cpp
 *
 * Bytecode VM-based REPL for Havel language.
 */
#include "REPL.hpp"
#include "../../utils/Logger.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../modules/HostModules.hpp"
#include "../runtime/StdLibModules.hpp"
#include "../runtime/HostAPI.hpp"
#include "../../utils/Logger.hpp"
#include "../../utils/CrashHandler.hpp"
#include "../parser/Parser.h"
#include "../utils/ErrorPrinter.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/DebugUtils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <csignal>
#include <atomic>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace havel::repl {

// Static member initialization
std::atomic<bool> REPL::interrupted_{false};

// Signal handler for REPL (not static - declared as friend in header)
void replSignalHandler(int sig) {
    if (sig == SIGINT) {
        REPL::interrupted_.store(true);
        requestStopAll();
        std::cout << "\n^C" << std::endl;
    } else if (sig == SIGQUIT) {
        panic("SIGQUIT received (Ctrl-\\)", true);
    }
}

REPL::REPL(const REPLConfig& config)
  : config_(config) {
}

REPL::~REPL() {
  // Cleanup
}

void REPL::initialize(std::shared_ptr<IHostAPI> hostAPI) {
  if (!hostAPI) {
    throw std::runtime_error("REPL requires valid IHostAPI");
  }

  info("Initializing REPL with bytecode VM...");

  // Clear service registry (for REPL restarts)
  havel::host::ServiceRegistry::instance().clear();

  // Initialize service registry with all services (if IO is available)
  if (hostAPI->GetIO()) {
    havel::initializeServiceRegistry(hostAPI);
  }

  // Create VM
  vm_ = std::make_shared<compiler::VM>();

  // Create HostContext with injected dependencies (stored as member to persist beyond this function)
  hostContext_ = std::make_unique<HostContext>(havel::createHostContext(hostAPI));

  // Wire VM into HostContext (required before HostBridge::install() fires vm_setup_callbacks_)
  hostContext_->vm = vm_.get();

  // Create HostBridge with injected context
  hostBridge_ = compiler::createHostBridge(*hostContext_);
  hostBridge_->install();

  // Register HostBridge host functions with the VM
  for (const auto& [name, fn] : hostBridge_->options().host_functions) {
    vm_->registerHostFunction(name, fn);
  }

  initialized = true;
  info("REPL initialized successfully");
}

void REPL::attach(compiler::VM* existingVM,
                  compiler::HostBridge* bridge,
                  std::unordered_set<std::string> globals) {
  if (!existingVM) {
    throw std::runtime_error("REPL::attach requires a non-null VM");
  }
  if (!bridge) {
    throw std::runtime_error("REPL::attach requires a non-null HostBridge");
  }

  info("Attaching REPL to existing VM...");

  // Reuse the existing VM — non-owning reference via no-op deleter
  vm_ = std::shared_ptr<compiler::VM>(existingVM, [](compiler::VM*){});
  hostBridge_ = std::shared_ptr<compiler::HostBridge>(bridge, [](compiler::HostBridge*){});
  known_globals_ = std::move(globals);
  initialized = true;
  info("REPL attached successfully");
}

void REPL::setPrintHandler(std::function<void(const std::string&)> handler) {
  printHandler_ = std::move(handler);
}

void REPL::setInputHandler(std::function<std::string(const std::string&)> handler) {
  inputHandler_ = std::move(handler);
}

std::string REPL::readLine(const std::string& prompt) {
  if (inputHandler_) {
    return inputHandler_(prompt);
  }
  
#ifdef HAVE_READLINE
  char* line = readline(prompt.c_str());
  if (line) {
    std::string result(line);
    free(line);
    return result;
  }
  return "";  // EOF
#else
  std::cout << prompt;
  std::cout.flush();
  std::string line;
  if (!std::getline(std::cin, line)) {
    return "";  // EOF
  }
  return line;
#endif
}

bool REPL::isInputComplete(const std::string& input) const {
  // Simple brace/bracket/parenthesis balancing
  int braceCount = 0;
  int bracketCount = 0;
  int parenCount = 0;
  bool inString = false;
  bool inChar = false;
  bool escape = false;
  
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    
    if (escape) {
      escape = false;
      continue;
    }
    
    if (c == '\\') {
      escape = true;
      continue;
    }
    
    if (c == '"' && !inChar) {
      inString = !inString;
      continue;
    }
    
    if (c == '\'' && !inString) {
      inChar = !inChar;
      continue;
    }
    
    if (inString || inChar) {
      continue;
    }
    
    // Skip comments
    if (c == '#' && (i == 0 || input[i-1] != '\\')) {
      break;
    }
    
    switch (c) {
      case '{': braceCount++; break;
      case '}': braceCount--; break;
      case '[': bracketCount++; break;
      case ']': bracketCount--; break;
      case '(': parenCount++; break;
      case ')': parenCount--; break;
    }
  }
  
  return braceCount == 0 && bracketCount == 0 && parenCount == 0;
}

void REPL::printValue(const std::string& value) {
  if (printHandler_) {
    printHandler_(value);
  } else {
    std::cout << value << std::endl;
  }
}

void REPL::printError(const std::string& error, int line, int column, int length, const std::string& sourceLine) {
  std::string result;
  if (line > 0) {
    result = havel::ErrorPrinter::formatError("Error", error, "<repl>", (size_t)line, (size_t)column, (size_t)length, sourceLine);
  } else {
    // Fallsback for errors without line info
    result = "\033[1;31mError\033[0m: " + error + "\n";
  }
  
  if (printHandler_) {
    printHandler_(result);
        } else {
            ::havel::info("{}", result);
  }
}

bool REPL::handleCommand(const std::string& input) {
  if (input == "exit" || input == "quit" || input == ":q") {
    return true;  // Exit signal
  }
  
  if (input == "help" || input == "?") {
    showHelp();
    return false;
  }
  
  if (input == "clear" || input == ":clear") {
#ifdef HAVE_READLINE
    // Clear readline history
    clear_history();
#endif
    std::cout << "\033[2J\033[H" << std::flush;
    return false;
  }
  
  return false;  // Not a command
}

void REPL::showHelp() const {
  std::cout << "Havel REPL Help\n";
  std::cout << "===============\n\n";
  std::cout << "Commands:\n";
  std::cout << "  exit, quit, :q     - Exit REPL\n";
  std::cout << "  help, ?            - Show this help\n";
  std::cout << "  clear, :clear      - Clear screen\n";
  std::cout << "\n";
  std::cout << "Usage:\n";
  std::cout << "  - Enter expressions directly: 1 + 2\n";
  std::cout << "  - Define functions: fn add(a,b) { return a+b }\n";
  std::cout << "  - Multi-line input is supported (brace matching)\n";
  std::cout << "  - Comments start with #\n";
  std::cout << "\n";
  std::cout << "Examples:\n";
  std::cout << "  havel> 1 + 2\n";
  std::cout << "  => 3\n";
  std::cout << "  havel> fn hello() { print(\"Hello, World!\") }\n";
  std::cout << "  havel> hello()\n";
  std::cout << "  Hello, World!\n";
  std::cout << "\n";
}

bool REPL::execute(const std::string& code) {
  if (!initialized) {
    printError("REPL not initialized. Call initialize() first.");
    return false;
  }

  try {
    // Compile and execute only the new code
    // executePersistent preserves globals/state between calls
        compiler::ByteCompiler byteCompiler;
        parser::Parser parser{{
            .lexer = config_.debugLexer,
            .parser = config_.debugParser,
            .ast = config_.debugAst
        }};

    // Tell compiler about globals from previous REPL lines
    // so `let x = 5` on line 1 means `x` resolves as Global on line 2
    byteCompiler.setKnownGlobals(known_globals_);

    auto program = parser.produceAST(code);
    if (!program || parser.hasErrors()) {
      for (const auto& err : parser.getErrors()) {
        // Find the line content in current input
        std::string sourceLine;
        std::istringstream stream(code);
        std::string l;
        size_t current = 1;
        while (std::getline(stream, l)) {
          if (current == err.line) {
            sourceLine = l;
            break;
          }
          current++;
        }
        printError(err.message, (int)err.line, (int)err.column, (int)err.length, sourceLine);
      }
      return false;
    }

auto chunk = byteCompiler.compile(*program);

if (config_.debugBytecode && chunk) {
compiler::BytecodeDisassembler disasm(*chunk);
std::cout << disasm.disassemble() << std::endl;
}

// Keep the chunk alive in the VM so closures/functions from this
// REPL line remain valid (values reference into the BytecodeChunk)
auto sharedChunk = std::shared_ptr<compiler::BytecodeChunk>(std::move(chunk));
vm_->storeReplChunk(sharedChunk);

// Collect new global names from the resolver and add to known_globals_
// so subsequent REPL lines know about variables declared here
for (const auto& name : byteCompiler.lexicalResolution().global_variables) {
known_globals_.insert(name);
}

// Execute persistently (preserves globals between REPL lines)
auto result = vm_->executePersistent(*sharedChunk, "__main__");

// Restore current_chunk to this REPL line's chunk so toString can
// resolve function names/string IDs from it
vm_->setCurrentChunk(sharedChunk.get());

if (!result.isNull()) {
printValue(vm_->toString(result));
}
    return true;
  } catch (const std::exception& e) {
    printError(e.what(), currentLine);
    return false;
  }
}

bool REPL::executeFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) {
    printError("Cannot open file: " + filename);
    return false;
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  return execute(buffer.str());
}

int REPL::run() {
  if (!initialized) {
        ::havel::error("REPL not initialized. Call initialize() first.");
    return 1;
  }

  setupSignalHandlers();

  std::cout << "Havel REPL (Bytecode VM)\n";
  std::cout << "Type 'help' for commands, 'exit' to quit.\n";
  std::cout << "Ctrl-C: stop threads/loops, clear input | Ctrl-D: exit | Ctrl-\\: panic\n\n";

  accumulatedInput.clear();
  currentLine = 0;

  while (true) {
    currentLine++;

    // Check for interrupt
    if (interrupted_.load()) {
      interrupted_.store(false);
      if (!accumulatedInput.empty()) {
        std::cout << "^C\nInput cleared. Press Ctrl-D to exit.\n";
        accumulatedInput.clear();
        continue;
      }
      std::cout << "^C\n";
      continue;
    }

    // Determine prompt
    std::string prompt = accumulatedInput.empty()
        ? config_.prompt
        : config_.continuePrompt;
    
  // Read input
  std::string line = readLine(prompt);

  // Check for interrupt after read
  if (interrupted_.load()) {
    interrupted_.store(false);
    std::cout << "^C\n";
    accumulatedInput.clear();
    continue;
  }

  // Check for EOF (Ctrl-D)
  if (line.empty() && std::cin.eof()) {
    if (accumulatedInput.empty()) {
      std::cout << "^D\nExiting...\n";
      break;
    } else {
      std::cout << "^D\nInput cleared (press Ctrl-D again to exit).\n";
      accumulatedInput.clear();
      std::cin.clear();
      continue;
    }
  }

  // Trim whitespace
  line.erase(0, line.find_first_not_of(" \t\n\r"));
  line.erase(line.find_last_not_of(" \t\n\r") + 1);
    
    // Skip empty lines (unless accumulating)
    if (line.empty() && accumulatedInput.empty()) {
      continue;
    }
    
    // Check for commands (only when not accumulating)
    if (accumulatedInput.empty()) {
      if (handleCommand(line)) {
        break;  // Exit command
      }
    }
    
    // Add to history (if using readline)
#ifdef HAVE_READLINE
    if (!line.empty() && accumulatedInput.empty()) {
      add_history(line.c_str());
    }
#endif
    
    // Accumulate input
    if (accumulatedInput.empty()) {
      accumulatedInput = line;
    } else {
      accumulatedInput += "\n" + line;
    }
    
    // Check if input is complete
    if (!isInputComplete(accumulatedInput)) {
      continue;  // Need more input
    }
    
    // Execute accumulated input
    bool success = execute(accumulatedInput);
    
    // Reset for next input
    accumulatedInput.clear();
    
  if (!success && config_.stopOnError) {
    break;  // Stop on error
  }
}

return 0;
}

void REPL::setupSignalHandlers() {
struct sigaction sa;
sa.sa_handler = replSignalHandler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;

sigaction(SIGINT, &sa, nullptr);
sigaction(SIGQUIT, &sa, nullptr);

initCrashHandler();
}

} // namespace havel::repl
