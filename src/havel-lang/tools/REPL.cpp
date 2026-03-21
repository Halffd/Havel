/*
 * REPL.cpp
 *
 * Bytecode VM-based REPL for Havel language.
 */
#include "REPL.hpp"
#include "../../modules/HostModules.hpp"
#include "../../utils/Logger.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace havel::repl {

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
  
  // Initialize service registry with all services
  havel::initializeServiceRegistry(hostAPI);
  
  // Create VM
  vm_ = std::make_unique<compiler::VM>();
  
  // Create HostBridge dependencies
  auto deps = havel::createHostBridgeDependencies(hostAPI);
  
  // Create HostBridge registry
  hostBridge_ = compiler::createHostBridgeRegistry(*vm_, deps);
  
  initialized = true;
  info("REPL initialized successfully");
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

void REPL::printError(const std::string& error, int line) {
  std::string prefix = "Error";
  if (line > 0) {
    prefix += " at line " + std::to_string(line);
  }
  
  if (printHandler_) {
    printHandler_(prefix + ": " + error);
  } else {
    std::cerr << prefix << ": " << error << std::endl;
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
    // Execute with bytecode VM
    auto result = compiler::runBytecodePipeline(code, "__repl__");
    (void)result;  // For now, just check it doesn't throw
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
    std::cerr << "Error: REPL not initialized. Call initialize() first." << std::endl;
    return 1;
  }
  
  std::cout << "Havel REPL (Bytecode VM)\n";
  std::cout << "Type 'help' for commands, 'exit' to quit.\n\n";
  
  accumulatedInput.clear();
  currentLine = 0;
  
  while (true) {
    currentLine++;
    
    // Determine prompt
    std::string prompt = accumulatedInput.empty() 
      ? config_.prompt 
      : config_.continuePrompt;
    
    // Read input
    std::string line = readLine(prompt);
    
    // Check for EOF
    if (line.empty() && std::cin.eof()) {
      std::cout << "\nExiting..." << std::endl;
      break;
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

} // namespace havel::repl
