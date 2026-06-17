/*
 * REPL.cpp
 *
 * Bytecode VM-based REPL for Havel language.
 */
#include "REPL.hpp"
#include "../../utils/Logger.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../modules/HostModules.hpp"
#include "../runtime/Modules.hpp"
#include "../runtime/HostAPI.hpp"
#include "../../utils/Logger.hpp"
#include "../../utils/CrashHandler.hpp"
#include "../parser/Parser.h"
#include "utils/ErrorPrinter.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/DebugUtils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <csignal>
#include <atomic>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace havel::repl {

// Static member initialization
std::atomic<bool> REPL::interrupted_{false};
std::function<void()> REPL::pumpCallbackForHook_;

// Signal handler for REPL (not static - declared as friend in header)
void replSignalHandler(int sig) {
    if (sig == SIGINT) {
        REPL::interrupted_.store(true);
        requestStopAll();
    } else if (sig == SIGQUIT) {
        panic("SIGQUIT received (Ctrl-\\)", true);
    }
}

REPL::REPL(const REPLConfig& config)
  : config_(config) {
}

REPL::~REPL() {
    if (outputLog_.is_open()) {
        outputLog_.close();
    }
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

    // Create Modules with injected context
    modules_ = havel::createModules(*hostContext_);
    modules_->install();

    // Register Modules host functions with the VM
    for (const auto& [name, fn] : modules_->options().host_functions) {
        vm_->registerHostFunction(name, fn);
    }

    // Register prototype methods (array.push, string.len, etc.)
    // These are normally registered inside registerDefaultHostGlobals() via execute(),
    // but REPL uses executePersistent() which skips that, so we must do it explicitly.
    vm_->registerDefaultPrototypes();

    // Open output log if configured
    if (!config_.outputLogFile.empty()) {
        outputLog_.open(config_.outputLogFile, std::ios::app);
        if (outputLog_.is_open()) {
            info("REPL output logging to: {}", config_.outputLogFile);
        } else {
            warn("Failed to open REPL output log: {}", config_.outputLogFile);
        }
    }

    // Resolve history file path
    historyFilePath_ = resolveHistoryPath();

  initialized = true;

  // Set up debug break callback (always, but gated by replDebugMode_)
  vm_->setDebugBreakCallback([this]() {
    if (replDebugMode_) {
      replDebugCallback();
    }
  });
}

void REPL::attach(compiler::VM* existingVM,
                Modules* modules,
                std::unordered_set<std::string> globals) {
    if (!existingVM) {
        throw std::runtime_error("REPL::attach requires a non-null VM");
    }
    if (!modules) {
        throw std::runtime_error("REPL::attach requires a non-null Modules");
    }

    info("Attaching REPL to existing VM...");

    // Reuse the existing VM — non-owning reference via no-op deleter
    vm_ = std::shared_ptr<compiler::VM>(existingVM, [](compiler::VM*){});
    modules_ = std::shared_ptr<Modules>(modules, [](Modules*){});
  known_globals_ = std::move(globals);

    // Open output log if configured
    if (!config_.outputLogFile.empty()) {
        outputLog_.open(config_.outputLogFile, std::ios::app);
        if (outputLog_.is_open()) {
            info("REPL output logging to: {}", config_.outputLogFile);
        } else {
            warn("Failed to open REPL output log: {}", config_.outputLogFile);
        }
    }

    // Resolve history file path
    historyFilePath_ = resolveHistoryPath();

  initialized = true;

  // Set up debug break callback
  vm_->setDebugBreakCallback([this]() {
    if (replDebugMode_) {
      replDebugCallback();
    }
  });

  info("REPL attached successfully");
}

std::string REPL::resolveHistoryPath() const {
    if (!config_.historyFile.empty()) {
        return config_.historyFile;
    }
    std::string home = getenv("HOME") ? getenv("HOME") : "/tmp";
    std::string path = home + "/.havel_history";
    return path;
}

void REPL::logOutput(const std::string& text) {
    if (outputLog_.is_open()) {
        outputLog_ << text;
        outputLog_.flush();
    }
}

void REPL::setPrintHandler(std::function<void(const std::string&)> handler) {
  printHandler_ = std::move(handler);
}

void REPL::setInputHandler(std::function<std::string(const std::string&)> handler) {
  inputHandler_ = std::move(handler);
}

void REPL::setPumpCallback(std::function<void()> callback) {
  pumpCallback_ = std::move(callback);
}

std::string REPL::readLine(const std::string& prompt) {
    if (inputHandler_) {
        return inputHandler_(prompt);
    }

#ifdef HAVE_READLINE
    // Install event hook so readline pumps events between keystrokes
    pumpCallbackForHook_ = pumpCallback_;
    rl_event_hook = rlEventHook;
    char* line = readline(prompt.c_str());
    rl_event_hook = nullptr;
    pumpCallbackForHook_ = nullptr;
    if (line) {
        std::string result(line);
        free(line);
        return result;
    }
    std::cin.setstate(std::ios::eofbit);
    return "";
#else
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    while (true) {
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 50);
        if (ret > 0) {
            if (pfd.revents & POLLIN) {
                char c = 0;
                if (read(STDIN_FILENO, &c, 1) > 0) {
                    if (c == '\n' || c == '\r') {
                        break;
                    }
                    line += c;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else if (ret < 0 && errno != EINTR) {
            break;
        } else if (pumpCallback_) {
            pumpCallback_();
        }
    }
    return line;
#endif
}

int REPL::rlEventHook() {
    if (pumpCallbackForHook_) pumpCallbackForHook_();
    return 0;
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
    
    // Skip // comments
    if (c == '/' && i + 1 < input.size() && input[i+1] == '/') {
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
  logOutput(value + "\n");
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
  logOutput(result + "\n");
}

bool REPL::handleCommand(const std::string& input) {
    if (input == "exit" || input == "quit" || input == ":q") {
        return true; // Exit signal
    }

    if (input == "help" || input == "?") {
        showHelp();
        return true;
    }

    if (input == "clear" || input == ":clear") {
#ifdef HAVE_READLINE
        clear_history();
#endif
        std::cout << "\033[2J\033[H" << std::flush;
        return true;
    }

    if (input == ":bytecode" || input == ":bc") {
        config_.debugBytecode = !config_.debugBytecode;
        std::cout << "Bytecode debug: " << (config_.debugBytecode ? "ON" : "OFF") << "\n";
        return true;
    }

  if (input == ":debug") {
    replDebugMode_ = !replDebugMode_;
    if (replDebugMode_) {
      vm_->attachDebugger();
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::StepInto);
      std::cout << "Debug mode: ON (breakpoints and stepping enabled)\n";
    } else {
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::Continue);
      std::cout << "Debug mode: OFF\n";
    }
    return true;
  }

  if (input == ":globals") {
    std::cout << "Known globals: ";
    bool first = true;
    for (const auto& g : known_globals_) {
      if (!first) std::cout << ", ";
      std::cout << g;
      first = false;
    }
    std::cout << "\n";
    return true;
  }

  if (input == ":classes") {
    bool any = false;
    if (!known_class_names_.empty()) {
      std::cout << "Known classes: ";
      bool first = true;
      for (const auto& c : known_class_names_) {
        if (!first) std::cout << ", ";
        std::cout << c;
        first = false;
      }
      std::cout << "\n";
      any = true;
  }
  if (!known_struct_names_.empty()) {
    std::cout << "Known structs: ";
    bool first = true;
    for (const auto& s : known_struct_names_) {
      if (!first) std::cout << ", ";
      std::cout << s;
      first = false;
    }
    std::cout << "\n";
    any = true;
  }
  if (!known_protocol_names_.empty()) {
    std::cout << "Known protocols: ";
    bool first = true;
    for (const auto& p : known_protocol_names_) {
      if (!first) std::cout << ", ";
      std::cout << p;
      first = false;
    }
    std::cout << "\n";
    any = true;
  }
  if (!known_impl_names_.empty()) {
    std::cout << "Known impls: ";
    bool first = true;
    for (const auto& i : known_impl_names_) {
      if (!first) std::cout << ", ";
      std::cout << i;
      first = false;
    }
    std::cout << "\n";
    any = true;
  }
  if (!any) {
    std::cout << "No classes, structs, protocols, or impls defined yet.\n";
  }
  return true;
  }

  if (input == ":log") {
    if (outputLog_.is_open()) {
      std::cout << "Output log: " << config_.outputLogFile << "\n";
    } else {
      std::cout << "Output logging is disabled\n";
    }
    std::cout << "History file: " << historyFilePath_ << "\n";
    return true;
  }

  if (input.rfind(":load ", 0) == 0 || input.rfind(":l ", 0) == 0) {
    std::string filename = input.substr(input.find(' ') + 1);
    while (!filename.empty() && filename.front() == ' ') filename.erase(0, 1);
    while (!filename.empty() && filename.back() == ' ') filename.pop_back();
    if (filename.empty()) {
      printError(":load requires a filename");
      return true;
    }
    executeFile(filename);
    return true;
  }

  return false; // Not a command
}

void REPL::showHelp() const {
    std::cout << "Havel REPL Help\n";
    std::cout << "===============\n\n";
    std::cout << "Commands:\n";
    std::cout << "  exit, quit, :q   - Exit REPL\n";
    std::cout << "  help, ?          - Show this help\n";
    std::cout << "  clear, :clear    - Clear screen\n";
    std::cout << "  :bytecode, :bc   - Toggle bytecode debug output\n";
  std::cout << "  :debug - Toggle interactive hvdb debug mode\n";
  std::cout << " :globals - Show known global variables\n";
  std::cout << " :classes - Show known classes, structs, protocols, and impls\n";
  std::cout << " :load <file>, :l <file> - Load and execute a script file\n";
  std::cout << " :log - Show output log and history file paths\n";
    std::cout << "\n";
    std::cout << "Keybindings:\n";
    std::cout << "  Up/Down          - Navigate history\n";
    std::cout << "  Ctrl-C           - Cancel input (twice to exit)\n";
    std::cout << "  Ctrl-D           - Exit REPL\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  havel> 1 + 2\n";
    std::cout << "  => 3\n";
    std::cout << "  havel> fn add(a, b) { a + b }\n";
    std::cout << "  havel> add(3, 4)\n";
    std::cout << "  => 7\n";
    std::cout << "\n";
}

bool REPL::execute(const std::string& code) {
  if (!initialized) {
    printError("REPL not initialized. Call initialize() first.");
    return false;
  }

  currentCode_ = code;

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
  byteCompiler.setKnownClassNames(known_class_names_);
  byteCompiler.setKnownStructNames(known_struct_names_);
  byteCompiler.setKnownProtocolNames(known_protocol_names_);
  byteCompiler.setKnownImplNames(known_impl_names_);

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
  for (const auto& name : byteCompiler.topLevelClassNames()) {
    known_class_names_.insert(name);
  }
  for (const auto& name : byteCompiler.topLevelStructNames()) {
    known_struct_names_.insert(name);
  }
  for (const auto& name : byteCompiler.topLevelProtocolNames()) {
    known_protocol_names_.insert(name);
  }
  for (const auto& name : byteCompiler.topLevelImplNames()) {
    known_impl_names_.insert(name);
  }

// Execute persistently (preserves globals between REPL lines)
// executePersistent saves/restores current_chunk; storeReplChunk already
// set it to this chunk, so after execution current_chunk points to sharedChunk.
  auto result = vm_->executePersistent(*sharedChunk, "__main__");

  // Wrap FunctionObjId globals into closures so cross-chunk calls work.
  // A FunctionObjId is just a raw index — meaningless in a different chunk.
  // Closures carry their chunk pointer, making them safe to call from any REPL line.
  auto& g = vm_->globals;
  for (auto& [name, val] : g) {
    if (val.isFunctionObjId()) {
      uint32_t fnIdx = val.asFunctionObjId();
      if (sharedChunk->getFunction(fnIdx)) {
        auto ref = vm_->getHeap().allocateClosure(
          compiler::GCHeap::RuntimeClosure{
            .function_index = fnIdx,
            .chunk_index = 0,
            .chunk = sharedChunk.get(),
            .module_globals = nullptr,
            .upvalues = {}
          });
        val = compiler::Value::makeClosureId(ref.id);
      }
    }
  }

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

    // Session timestamp
    std::time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    sessionStart_ = timeBuf;

#ifdef HAVE_READLINE
    rl_readline_name = "havel";
    rl_catch_signals = 0;
    rl_catch_sigwinch = 1;
    stifle_history(500);
    read_history(historyFilePath_.c_str());
#endif

    std::cout << "Havel REPL (Bytecode VM)" << std::endl;
    std::cout << "Type 'help' for commands, 'exit' to quit." << std::endl;
    std::cout << std::endl;

    logOutput("=== Havel REPL session started at " + sessionStart_ + " ===\n");

  accumulatedInput.clear();
    currentLine = 0;
    int consecutiveInterrupts = 0;

    while (true) {
        currentLine++;

        // Check for interrupt
        if (interrupted_.load()) {
            interrupted_.store(false);
            consecutiveInterrupts++;
            if (!accumulatedInput.empty()) {
                std::cout << "^C\nInput cleared. Press Ctrl-D to exit.\n";
                accumulatedInput.clear();
                continue;
            }
            if (consecutiveInterrupts >= 2) {
                std::cout << "^C\nExiting...\n";
                break;
            }
            std::cout << "^C\n(Press Ctrl-C again to exit, or Ctrl-D)\n";
            continue;
        }
        consecutiveInterrupts = 0;

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
      bool wasCommand = handleCommand(line);
      if (wasCommand) {
        continue;  // Command was handled, don't accumulate
      }
    }
    
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
    
    // Add complete input to readline history (single or multi-line)
#ifdef HAVE_READLINE
    if (!accumulatedInput.empty()) {
      add_history(accumulatedInput.c_str());
    }
#endif
    
    // Execute accumulated input
    bool success = execute(accumulatedInput);

    // Pump event loop callback (e.g., EventListener) on same thread after each execution
    if (pumpCallback_) {
      pumpCallback_();
    }

    // Reset for next input
    accumulatedInput.clear();
    
        if (!success && config_.stopOnError) {
            break; // Stop on error
        }
    }

    logOutput("=== Havel REPL session ended at " + sessionStart_ + " ===\n");

#ifdef HAVE_READLINE
    write_history(historyFilePath_.c_str());
#endif

    if (outputLog_.is_open()) {
        outputLog_.close();
    }

    return 0;
}

void REPL::replDebugCallback() {
  auto info = vm_->getCurrentFrameInfo();
  std::vector<std::string> sourceLines;
  if (!currentCode_.empty()) {
    std::istringstream stream(currentCode_);
    std::string l;
    while (std::getline(stream, l)) sourceLines.push_back(l);
  }

  std::cout << "\nBreakpoint";
  if (!info.source_file.empty())
    std::cout << " at " << info.source_file << ":" << info.line;
  if (!info.function_name.empty())
    std::cout << " in " << info.function_name;
  std::cout << std::endl;

  if (info.line > 0 && info.line <= sourceLines.size())
    std::cout << "  " << info.line << ": " << sourceLines[info.line - 1] << "\n";

  while (true) {
    std::cout << "(hvdb) " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::Continue);
      break;
    }
    if (line.empty()) continue;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
      std::cout << "Exiting." << std::endl;
      _Exit(0);
    } else if (cmd == "help" || cmd == "h" || cmd == "?") {
      std::cout << "  continue/c  Continue execution\n";
      std::cout << "  step/s      Step into\n";
      std::cout << "  next/n      Step over\n";
      std::cout << "  fin/f       Step out\n";
      std::cout << "  locals/l    Show locals\n";
      std::cout << "  globals/g   Show globals\n";
      std::cout << "  stack/bt    Show stack\n";
      std::cout << "  print/eval <expr>  Evaluate expression\n";
      std::cout << "  quit/q      Exit debugger\n";
    } else if (cmd == "continue" || cmd == "c") {
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::Continue);
      break;
    } else if (cmd == "step" || cmd == "s" || cmd == "into") {
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::StepInto);
      break;
    } else if (cmd == "next" || cmd == "n" || cmd == "over") {
      auto frames = vm_->getStackFrames();
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::StepOver);
      vm_->setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
      break;
    } else if (cmd == "fin" || cmd == "f" || cmd == "out") {
      auto frames = vm_->getStackFrames();
      vm_->setDebugStepMode(compiler::VM::DebugStepMode::StepOut);
      vm_->setDebugStepFrameDepth(frames.empty() ? 0 : frames.back().frame_depth);
      break;
    } else if (cmd == "locals" || cmd == "l") {
      auto vars = vm_->getLocals();
      if (vars.empty()) std::cout << "  (no locals)\n";
      else for (auto& v : vars)
        std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
    } else if (cmd == "globals" || cmd == "g") {
      auto vars = vm_->getDebugGlobals();
      size_t n = 0;
      for (auto& v : vars) {
        if (n++ >= 30) { std::cout << "  ... (truncated)\n"; break; }
        std::cout << "  " << v.name << " : " << v.type << " = " << v.value << "\n";
      }
      if (vars.empty()) std::cout << "  (no globals)\n";
    } else if (cmd == "stack" || cmd == "bt" || cmd == "trace") {
      auto frames = vm_->getStackFrames();
      if (frames.empty()) std::cout << "  (empty stack)\n";
      else for (size_t i = 0; i < frames.size(); ++i)
        std::cout << "  #" << i << " " << frames[i].function_name
                  << " at " << frames[i].source_file << ":" << frames[i].line << "\n";
    } else if (cmd == "print" || cmd == "p" || cmd == "eval") {
      std::string expr; std::getline(iss >> std::ws, expr);
      if (!expr.empty()) {
        auto val = vm_->evaluateInFrame(expr);
        std::cout << "  " << expr << " = " << vm_->toString(val) << "\n";
      }
    } else {
      std::cout << "Unknown command. Type 'help'.\n";
    }
  }
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
