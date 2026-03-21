#include "HavelLauncher.hpp"
#include "gui/HavelApp.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include "utils/Logger.hpp"
#include <QApplication>
#include <QProcess>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

using namespace havel;

namespace havel::init {

// Global flag to enable/disable bytecode VM
static constexpr bool USE_BYTECODE_VM = true;

int HavelLauncher::run(int argc, char *argv[]) {
  try {
    LaunchConfig cfg = parseArgs(argc, argv);

    switch (cfg.mode) {
    case Mode::DAEMON:
      return runDaemon(cfg, argc, argv);
    case Mode::SCRIPT:
      return runScript(cfg, argc, argv);
    case Mode::SCRIPT_ONLY:
      return runScriptOnly(cfg, argc, argv);
    case Mode::REPL:
      return runRepl(cfg);
    case Mode::SCRIPT_AND_REPL:
      return runScriptAndRepl(cfg, argc, argv);
    case Mode::CLI:
      return runCli(argc, argv);
    default:
      error("Unknown mode");
      return 1;
    }
  } catch (const std::exception &e) {
    error("Fatal error: {}", e.what());
    return 1;
  }
}

HavelLauncher::LaunchConfig HavelLauncher::parseArgs(int argc, char *argv[]) {
  LaunchConfig cfg;
  bool repl = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--startup" || arg == "-s") {
      cfg.isStartup = true;
    } else if (arg == "--debug" || arg == "-d") {
      cfg.debugMode = true;
      Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
    } else if (arg == "--debug-parser" || arg == "-dp") {
      debugging::debug_parser = true;
      cfg.debugParser = true;
    } else if (arg == "--debug-ast" || arg == "-da") {
      debugging::debug_ast = true;
      cfg.debugAst = true;
    } else if (arg == "--debug-lexer" || arg == "-dl") {
      debugging::debug_lexer = true;
      cfg.debugLexer = true;
    } else if (arg == "--error" || arg == "-e") {
      // Stop on first error/warning
      cfg.stopOnError = true;
    } else if (arg == "--repl" || arg == "-r") {
      repl = true;
    } else if (arg == "--gui") {
      cfg.mode = Mode::DAEMON; // GUI mode is now DAEMON
    } else if (arg == "--full-repl" || arg == "-fr") {
      // Full REPL with all features (hotkeys, GUI, etc.)
      cfg.fullRepl = true;
      repl = true;
    } else if (arg == "--config" || arg == "-c") {
      // Config file path
      if (i + 1 < argc) {
        Configs::SetPath(argv[++i]);
      }
    } else if (arg == "--run" || arg == "run") {
      // Pure script execution without IO/hotkeys
      cfg.mode = Mode::SCRIPT_ONLY;
      // Next argument should be the script file
      if (i + 1 < argc) {
        cfg.scriptFile = argv[++i];
      }
    } else if (arg == "--help" || arg == "-h") {
      showHelp();
      exit(0);
    } else if (arg == "lexer") {
      cfg.mode = Mode::CLI;
      return cfg;
    } else {
      // Assume it's a script file
      if (!cfg.scriptFile.empty()) {
        error("Error: Only one script file can be provided. Got {} and {}",
              cfg.scriptFile, arg);
        exit(1);
      }
      if (!arg.ends_with(".hv")) {
        warning("Script file {} does not end with .hv extension", arg);
      }
      cfg.scriptFile = arg;
      cfg.mode = Mode::SCRIPT;
    }
  }

  // Determine mode based on flags and script file
  if (repl && !cfg.scriptFile.empty()) {
    // Script + REPL mode - full features
    cfg.mode = Mode::SCRIPT_AND_REPL;
  } else if (repl && cfg.fullRepl) {
    // Full REPL mode - full features even without script
    cfg.mode = Mode::SCRIPT_AND_REPL;
  } else if (repl) {
    // REPL only mode
    cfg.mode = Mode::REPL;
  } else if (cfg.scriptFile.empty() && cfg.mode == Mode::DAEMON) {
    // No script, no REPL - daemon mode
    cfg.mode = Mode::DAEMON;
  }
  // Otherwise use the mode already set (GUI_ONLY, SCRIPT_ONLY, SCRIPT, CLI)

  return cfg;
}

int HavelLauncher::runDaemon(const LaunchConfig &cfg, int argc, char *argv[]) {
  // Restart loop - keeps restarting until exit code is not 42
  while (true) {
    QApplication app(argc, argv);
    app.setApplicationName("havel");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("havel");
    app.setQuitOnLastWindowClosed(false);

    // Convert argc/argv to vector<string> for HavelApp
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    HavelApp havelApp(cfg.isStartup, "", false, true, args);

    if (!havelApp.isInitialized()) {
      error("Failed to initialize HavelApp");
      return 1;
    }

    // Load and execute startup script if specified
    if (!cfg.scriptFile.empty()) {
      std::ifstream file(cfg.scriptFile);
      if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string code = buffer.str();

        auto *interpreter = havelApp.getInterpreter();
        if (interpreter) {
          info("Loading startup script: {}", cfg.scriptFile);
          auto result = interpreter->Execute(code);

          if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
            const auto &err = std::get<havel::HavelRuntimeError>(result);
            if (err.hasLocation && err.line > 0) {
              error("Startup script error at line {}: {}", err.line,
                    err.what());
            } else {
              error("Startup script error: {}", err.what());
            }
          }
        }
      } else {
        error("Cannot open startup script: {}", cfg.scriptFile);
      }
    }

    info("Havel started successfully - running in system tray");
    int exitCode = app.exec();

    // Handle restart exit code - loop back to restart
    if (exitCode == 42) {
      info("Restart requested - relaunching application");
      continue; // Loop back and restart
    }

    return exitCode; // Normal exit
  }
}

int HavelLauncher::runScript(const LaunchConfig &cfg, int argc, char *argv[]) {
  // Restart loop
  while (true) {
    info("Running Havel script: {}", cfg.scriptFile);

    // Read script file
    std::ifstream file(cfg.scriptFile);
    if (!file) {
      error("Cannot open script file: {}", cfg.scriptFile);
      return 2;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();

    // Initialize Qt for script execution (GUI available by default)
    info("Hotkeys or GUI operations detected - initializing Qt");

    int dummy_argc = 1;
    char dummy_name[] = "havel-script";
    char *dummy_argv[] = {dummy_name, nullptr};
    QApplication app(dummy_argc, dummy_argv);

    // Convert argc/argv to vector<string> for HavelApp
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    HavelApp havelApp(false, cfg.scriptFile, false, false,
                      args); // Don't show GUI

    if (!havelApp.isInitialized()) {
      error("Failed to initialize HavelApp");
      return 1;
    }

    // Use HavelApp's interpreter
    auto *interpreter = havelApp.getInterpreter();
    if (!interpreter) {
      error("Interpreter is not available");
      return 1;
    }

    auto result = interpreter->Execute(code);

    if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
      const auto &err = std::get<havel::HavelRuntimeError>(result);

      // Print detailed error information
      havel::error(
          "╔═══════════════════════════════════════════════════════════╗");
      havel::error(
          "║         RUNTIME ERROR IN STARTUP SCRIPT                   ║");
      havel::error(
          "╚═══════════════════════════════════════════════════════════╝");
      havel::error("");
      havel::error("  Error: {}", err.what());

      // Use structured location data if available
      if (err.hasLocation && err.line > 0) {
        havel::error("");
        havel::error("  At line {}, column {}", err.line, err.column);

        // Print source code context
        interpreter->printSourceWithContext(code, err.line);
      } else {
        // No location info - print full source context
        havel::error("");
        havel::error("  (No location information available)");
        interpreter->printSourceWithContext(code, 0);
      }

      havel::error("");
      havel::error("  Script file: {}", cfg.scriptFile);
      havel::error("");

      return 1;
    }

    // Assume hotkeys might be present - let hotkeyManager handle it
    havelApp.hotkeyManager->printHotkeys();
    havelApp.hotkeyManager->updateAllConditionalHotkeys();
    info("Script loaded. Hotkeys registered. Press Ctrl+C to exit.");
    int exitCode = app.exec();

    // Handle restart
    if (exitCode == 42) {
      info("Restart requested - relaunching");
      continue; // Loop back and restart
    }

    return exitCode; // Normal exit
  }

  return 0; // No restart in headless mode
} // End of restart loop
} // namespace havel::init

int havel::init::HavelLauncher::runScriptOnly(const LaunchConfig &cfg, int argc,
                                              char *argv[]) {
  // Pure script execution without IO, hotkeys, display, or GUI
  // Useful for testing scripts that auto-exit or don't need input

  info("Running Havel script (pure mode): {}", cfg.scriptFile);

  // Read script file
  std::ifstream file(cfg.scriptFile);
  if (!file) {
    error("Cannot open script file: {}", cfg.scriptFile);
    return 2;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string code = buffer.str();

  // Set up signal handling for headless mode
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = [](int sig) {
    switch (sig) {
    case SIGINT:
      info("Received SIGINT (Ctrl+C) - shutting down headless mode");
      break;
    case SIGTERM:
      info("Received SIGTERM - shutting down headless mode");
      break;
    default:
      info("Received signal {} - shutting down headless mode", sig);
      break;
    }
    std::exit(0); // Graceful exit
  };

  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  info("Signal handling initialized for headless mode");

  // Execute with bytecode VM if enabled
  if constexpr (USE_BYTECODE_VM) {
    info("Executing with bytecode VM");
    try {
      auto vmResult = havel::compiler::runBytecodePipeline(code, "__main__");
      info("Bytecode execution completed successfully");
      return 0;
    } catch (const std::exception& e) {
      error("Bytecode error: {}", e.what());
      return 1;
    }
  }

  // Create minimal interpreter without IO/hotkeys
  havel::Interpreter interpreter;

  // Set script path for auto-reload support
  interpreter.setScriptPath(cfg.scriptFile);

  // Configure debug flags
  interpreter.setDebugParser(cfg.debugParser);
  interpreter.setStopOnError(cfg.stopOnError);
  interpreter.setShowAST(cfg.debugAst);

  // Execute script
  auto result = interpreter.Execute(code);

  if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
    const auto &err = std::get<havel::HavelRuntimeError>(result);
    if (err.hasLocation && err.line > 0) {
      error("Runtime Error at line {}: {}", err.line, err.what());
    } else {
      error("Runtime Error: {}", err.what());
    }
    return 1;
  }

  // Print result if any
  if (auto *value = std::get_if<havel::HavelValue>(&result)) {
    // Print value based on type using semantic helpers
    if (value->isString()) {
      std::cout << value->asString() << std::endl;
    } else if (value->isNumber()) {
      std::cout << value->asNumber() << std::endl;
    } else if (value->isBool()) {
      std::cout << (value->asBool() ? "true" : "false") << std::endl;
    }
  }

  info("Script executed successfully");
  return 0;
}

int havel::init::HavelLauncher::runRepl(const LaunchConfig &cfg) {
  info("Starting Havel REPL...");

  // Don't create QApplication for REPL - it's not needed for script execution
  // Qt will be lazily initialized only if GUI/clipboard functions are called

  // Use empty args for REPL (no command line arguments needed)
  std::vector<std::string> args;

  HavelApp havelApp(false, cfg.scriptFile, true, false, args);

  if (!havelApp.isInitialized()) {
    error("Failed to initialize HavelApp");
    return 1;
  }

  // Use HavelApp's interpreter
  auto *interpreter = havelApp.getInterpreter();
  if (!interpreter) {
    error("Interpreter is not available");
    return 1;
  }
  info("Script file: {}", cfg.scriptFile);
  if (!cfg.scriptFile.empty() && std::filesystem::exists(cfg.scriptFile)) {
    // Read script file
    std::ifstream file(cfg.scriptFile);
    if (!file) {
      error("Cannot open script file: {}", cfg.scriptFile);
      return 2;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();

    auto result = interpreter->Execute(code);
    if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
      const auto &err = std::get<havel::HavelRuntimeError>(result);
      if (err.hasLocation && err.line > 0) {
        error("Runtime Error at line {} in startup script: {}", err.line,
              err.what());
      } else {
        error("Runtime Error in startup script: {}", err.what());
      }
    }
  }
  std::cout << "Havel Language REPL v1.0\n";
  std::cout << "Type 'exit' or 'quit' to exit, 'help' for help\n\n";

  std::string line;
  std::string multiline;
  int braceCount = 0;

  // REPL log file
  std::string home =
      std::getenv("HOME") ? std::getenv("HOME") : std::string(".");
  std::string logPath = home + "/.havel_repl.log";
  std::ofstream replLog(logPath, std::ios::app);

  while (true) {
    std::string prompt = (braceCount > 0) ? "... " : ">>> ";

#ifdef HAVE_READLINE
    // Disable readline signal handling
    rl_catch_signals = 0;
    rl_catch_sigwinch = 0;

    char *input = readline(prompt.c_str());
    if (!input) {
      std::cout << "\nGoodbye!\n";
      break;
    }

    line = std::string(input);
    free(input);
    if (!line.empty())
      add_history(line.c_str());
#else
    std::cout << prompt;
    if (!std::getline(std::cin, line))

#endif

    // Trim whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
      if (braceCount == 0)
        continue;
      line = "";
    } else {
      line = line.substr(start);
    }

    // Log input
    if (replLog.is_open()) {
      replLog << line << '\n';
      replLog.flush();
    }

    // Commands
    if (braceCount == 0) {
      if (line == "exit" || line == "quit") {
        std::cout << "Goodbye!\n";
        return 0;
      }
      if (line == "help") {
        std::cout << "Available commands:\n";
        std::cout << "  exit, quit  - Exit REPL\n";
        std::cout << "  help        - Show this help\n";
        std::cout << "  clear       - Clear screen\n";
        std::cout << "\nType any Havel expression or statement to evaluate.\n";
        continue;
      }
      if (line == "clear") {
        std::cout << "\033[2J\033[1;1H";
        continue;
      }
      if (line.empty()) {
        continue;
      }
    }

    // Track braces (ignoring braces inside strings)
    bool inString = false;
    bool escaped = false;
    for (char c : line) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        inString = !inString;
        continue;
      }
      if (!inString) {
        if (c == '{')
          braceCount++;
        else if (c == '}')
          braceCount--;
      }
    }

    multiline += line + "\n";

    // Execute when balanced
    if (braceCount == 0 && !multiline.empty()) {
      try {
        if (interpreter) {
          auto result = interpreter->Execute(multiline);

          if (std::holds_alternative<havel::HavelValue>(result)) {
            auto val = std::get<havel::HavelValue>(result);
            if (!val.isNull()) {
              std::cout << "=> " << havel::Interpreter::ValueToString(val)
                        << "\n";
            }
          } else if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
            const auto &err = std::get<havel::HavelRuntimeError>(result);
            if (err.hasLocation && err.line > 0) {
              std::cerr << "Error at line " << err.line << ": " << err.what()
                        << "\n";
            } else {
              std::cerr << "Error: " << err.what() << "\n";
            }
          }
        } else {
          std::cerr << "Error: Interpreter is not available\n";
        }
      } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
      }

      multiline.clear();
    }
  }

  return 0;
}

int havel::init::HavelLauncher::runScriptAndRepl(const LaunchConfig &cfg,
                                                 int argc, char *argv[]) {
  info("Running Havel script and entering REPL: {}", cfg.scriptFile);

  // Read script file
  std::ifstream file(cfg.scriptFile);
  if (!file) {
    error("Cannot open script file: {}", cfg.scriptFile);
    return 2;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string code = buffer.str();

  // Convert argc/argv to vector<string> for HavelApp
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  // Create HavelApp instance
  HavelApp havelApp(false, cfg.scriptFile, false, false, args);

  // Use HavelApp's interpreter
  auto *interpreter = havelApp.getInterpreter();
  if (!interpreter) {
    error("Interpreter is not available");
    return 1;
  }

  // Execute the script first
  auto result = interpreter->Execute(code);
  if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
    const auto &err = std::get<havel::HavelRuntimeError>(result);
    if (err.hasLocation && err.line > 0) {
      error("Script runtime error at line {}: {}", err.line, err.what());
    } else {
      error("Script runtime error: {}", err.what());
    }
    return 1;
  }

  info("Script executed successfully. Entering REPL...");

  // Now enter REPL with the same interpreter and environment
  // Reuse the REPL logic but without creating a new interpreter
  std::cout << "Havel Language REPL v1.0\n";
  std::cout << "Type 'exit' or 'quit' to exit, 'help' for help\n\n";

  std::string line;
  std::string multiline;
  int braceCount = 0;

  // REPL log file
  std::string home =
      std::getenv("HOME") ? std::getenv("HOME") : std::string(".");
  std::string logPath = home + "/.havel_repl.log";
  std::ofstream replLog(logPath, std::ios::app);

  while (true) {
    std::string prompt = (braceCount > 0) ? "... " : ">>> ";

#ifdef HAVE_READLINE
    // Disable readline signal handling
    rl_catch_signals = 0;
    rl_catch_sigwinch = 0;

    char *input = readline(prompt.c_str());
    if (!input) {
      std::cout << "\nGoodbye!\n";
      break;
    }

    line = std::string(input);
    free(input);
    if (!line.empty())
      add_history(line.c_str());
#else
    std::cout << prompt;
    if (!std::getline(std::cin, line))

#endif

    // Trim whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
      if (braceCount == 0)
        continue;
      line = "";
    } else {
      line = line.substr(start);
    }

    // Log input
    if (replLog.is_open()) {
      replLog << line << '\n';
      replLog.flush();
    }

    // Commands
    if (braceCount == 0) {
      if (line == "exit" || line == "quit") {
        std::cout << "Goodbye!\n";
        return 0;
      }
      if (line == "help") {
        std::cout << "Available commands:\n";
        std::cout << "  exit, quit  - Exit REPL\n";
        std::cout << "  help        - Show this help\n";
        std::cout << "  clear       - Clear screen\n";
        std::cout << "\nType any Havel expression or statement to evaluate.\n";
        continue;
      }
      if (line == "clear") {
        std::cout << "\033[2J\033[1;1H";
        continue;
      }
      if (line.empty()) {
        continue;
      }
    }

    // Track braces
    for (char c : line) {
      if (c == '{')
        braceCount++;
      else if (c == '}')
        braceCount--;
    }

    multiline += line + "\n";

    // Execute when balanced
    if (braceCount == 0 && !multiline.empty()) {
      try {
        auto result = interpreter->Execute(multiline);

        if (std::holds_alternative<havel::HavelValue>(result)) {
          auto val = std::get<havel::HavelValue>(result);
          if (!val.isNull()) {
            std::cout << "=> " << havel::Interpreter::ValueToString(val)
                      << "\n";
          }
        } else if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
          const auto &err = std::get<havel::HavelRuntimeError>(result);
          if (err.hasLocation && err.line > 0) {
            std::cerr << "Error at line " << err.line << ": " << err.what()
                      << "\n";
          } else {
            std::cerr << "Error: " << err.what() << "\n";
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
      }

      multiline.clear();
    }
  }

  return 0;
}

int havel::init::HavelLauncher::runCli(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "usage: havel lexer script.hv\n";
    return 2;
  }

  const std::string subcommand = argv[1];
  if (subcommand != "lexer") {
    std::cerr << "Unknown command: " << subcommand << "\n";
    std::cerr << "usage: havel lexer script.hv\n";
    return 2;
  }

  const std::string filePath = argv[2];

  auto readFile = [](const std::string &path) -> std::string {
    std::ifstream file(path);
    if (!file) {
      throw std::runtime_error("Cannot open script file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  };

  auto getLine = [](const std::string &source,
                    size_t oneBasedLine) -> std::string {
    if (oneBasedLine == 0)
      return "";
    size_t currentLine = 1;
    size_t start = 0;
    while (start < source.size() && currentLine < oneBasedLine) {
      size_t nl = source.find('\n', start);
      if (nl == std::string::npos)
        return "";
      start = nl + 1;
      currentLine++;
    }
    if (start >= source.size())
      return "";
    size_t end = source.find('\n', start);
    if (end == std::string::npos)
      end = source.size();
    return source.substr(start, end - start);
  };

  auto printDiagnostic = [&](const std::string &kind, size_t line,
                             size_t column, const std::string &message,
                             const std::string &source) {
    auto countLines = [](const std::string &s) -> size_t {
      if (s.empty())
        return 1;
      size_t count = 1;
      for (char c : s) {
        if (c == '\n')
          ++count;
      }
      return count;
    };

    const size_t totalLines = countLines(source);
    const size_t safeLine =
        (line == 0 ? 1 : (line > totalLines ? totalLines : line));
    const size_t safeColumn = (column == 0 ? 1 : column);

    std::cerr << filePath << ":" << safeLine << ":" << safeColumn << ": "
              << kind << ": " << message << "\n";

    std::string srcLine = getLine(source, safeLine);
    if (safeLine >= 1 && safeLine <= totalLines) {
      std::cerr << srcLine << "\n";

      size_t caretCol = safeColumn;
      if (column == 0) {
        caretCol = srcLine.size() + 1;
      }
      if (caretCol < 1)
        caretCol = 1;
      if (caretCol > srcLine.size() + 1)
        caretCol = srcLine.size() + 1;

      for (size_t i = 1; i < caretCol; ++i)
        std::cerr << ' ';
      std::cerr << "^\n";
    }
  };

  auto prettify = [](const std::vector<havel::Token> &tokens) -> std::string {
    auto needsSpaceBefore = [](havel::TokenType t) {
      return t == havel::TokenType::Identifier ||
             t == havel::TokenType::Number || t == havel::TokenType::String ||
             t == havel::TokenType::InterpolatedString ||
             t == havel::TokenType::Hotkey;
    };
    auto needsSpaceAround = [](havel::TokenType t) {
      return t == havel::TokenType::Plus || t == havel::TokenType::Minus ||
             t == havel::TokenType::Multiply || t == havel::TokenType::Divide ||
             t == havel::TokenType::Modulo || t == havel::TokenType::Equals ||
             t == havel::TokenType::NotEquals || t == havel::TokenType::Less ||
             t == havel::TokenType::Greater ||
             t == havel::TokenType::LessEquals ||
             t == havel::TokenType::GreaterEquals ||
             t == havel::TokenType::And || t == havel::TokenType::Or ||
             t == havel::TokenType::Assign ||
             t == havel::TokenType::PlusAssign ||
             t == havel::TokenType::MinusAssign ||
             t == havel::TokenType::MultiplyAssign ||
             t == havel::TokenType::DivideAssign ||
             t == havel::TokenType::Arrow || t == havel::TokenType::Pipe ||
             t == havel::TokenType::DotDot;
    };

    std::string out;
    havel::TokenType prev = havel::TokenType::EOF_TOKEN;

    for (const auto &tok : tokens) {
      if (tok.type == havel::TokenType::EOF_TOKEN)

        if (tok.type == havel::TokenType::NewLine) {
          while (!out.empty() && out.back() == ' ')
            out.pop_back();
          out += "\n";
          prev = tok.type;
          continue;
        }

      if (tok.type == havel::TokenType::Comma) {
        while (!out.empty() && out.back() == ' ')
          out.pop_back();
        out += ", ";
        prev = tok.type;
        continue;
      }

      if (tok.type == havel::TokenType::Semicolon) {
        while (!out.empty() && out.back() == ' ')
          out.pop_back();
        out += ";\n";
        prev = tok.type;
        continue;
      }

      if (tok.type == havel::TokenType::CloseParen ||
          tok.type == havel::TokenType::CloseBracket ||
          tok.type == havel::TokenType::CloseBrace) {
        while (!out.empty() && out.back() == ' ')
          out.pop_back();
      }

      bool insertSpace = false;
      if (!out.empty() && out.back() != '\n') {
        if (needsSpaceBefore(tok.type) &&
            (needsSpaceBefore(prev) || prev == havel::TokenType::CloseParen ||
             prev == havel::TokenType::CloseBracket)) {
          insertSpace = true;
        }
        if (tok.type == havel::TokenType::OpenBrace &&
            prev != havel::TokenType::NewLine &&
            prev != havel::TokenType::OpenBrace) {
          insertSpace = true;
        }
      }

      if (insertSpace)
        out += ' ';

      if (needsSpaceAround(tok.type)) {
        while (!out.empty() && out.back() == ' ')
          out.pop_back();
        if (!out.empty() && out.back() != '\n')
          out += ' ';
        out += tok.raw;
        out += ' ';
      } else {
        out += tok.raw;
      }

      prev = tok.type;
    }

    while (!out.empty() && out.back() == ' ')
      out.pop_back();
    if (!out.empty() && out.back() != '\n')
      out += '\n';
    return out;
  };

  try {
    const std::string source = readFile(filePath);

    havel::Lexer lexer(source);
    std::vector<havel::Token> tokens = lexer.tokenize();

    havel::parser::Parser parser;
    (void)parser.parseStrict(source);

    std::cout << prettify(tokens);
    return 0;
  } catch (const havel::LexError &e) {
    printDiagnostic("lex error", e.line, e.column, e.what(),
                    readFile(filePath));
    return 1;
  } catch (const havel::parser::ParseError &e) {
    printDiagnostic("parse error", e.line, e.column, e.what(),
                    readFile(filePath));
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

void havel::init::HavelLauncher::showHelp() {
  std::cout << "Usage: havel [script.hv] [options]\n";
  std::cout << "       havel lexer script.hv\n";
  std::cout << "       havel --run script.hv\n";
  std::cout << "Options:\n";
  std::cout << "  --startup, -s       Run at system startup\n";
  std::cout << "  --debug, -d         Enable debug logging\n";
  std::cout << "  --debug-parser, -dp Enable parser debugging\n";
  std::cout << "  --debug-ast, -da    Enable AST debugging\n";
  std::cout << "  --debug-lexer, -dl  Enable lexer debugging\n";
  std::cout << "  --error, -e         Stop on first error/warning\n";
  std::cout << "  --repl, -r          Start interactive REPL (minimal mode)\n";
  std::cout << "  --full-repl, -fr    Start REPL with ALL features (hotkeys, "
               "GUI, etc.)\n";
  std::cout << "  --gui               GUI-only mode (no hotkeys)\n";
  std::cout
      << "  --run               Run script without IO/hotkeys (pure mode)\n";
  std::cout << "  --help, -h          Show this help\n";
  std::cout << "\nIf a .hv script file is provided, it will be executed.\n";
  std::cout << "If no script is provided, the GUI tray application starts.\n";
  std::cout << "\nModes:\n";
  std::cout << "  havel script.hv           - Run script with full features\n";
  std::cout << "  havel --repl script.hv    - Run script then enter REPL (full "
               "features)\n";
  std::cout << "  havel --full-repl         - Start REPL with all features\n";
  std::cout << "  havel --run script.hv     - Run script in minimal mode (no "
               "hotkeys)\n";
  std::cout << "\nPure mode (--run):\n";
  std::cout << "  Executes scripts without IO, hotkeys, display, or GUI.\n";
  std::cout
      << "  Useful for testing scripts that auto-exit or don't need input.\n";
  std::cout << "  Example: havel --run scripts/test_types.hv\n";
} // namespace havel::init