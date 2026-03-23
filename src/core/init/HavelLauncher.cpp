#include "HavelLauncher.hpp"
#include "gui/HavelApp.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/compiler/bytecode/HostBridge.hpp"
#include "havel-lang/compiler/bytecode/Pipeline.hpp"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/tools/REPL.hpp"
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
    } else if (arg == "--debug-bytecode" || arg == "-dbc") {
      cfg.debugBytecode = true;
    } else if (arg == "--diff" || arg == "-diff") {
      cfg.diffBytecode = true;
      cfg.debugBytecode = true; // --diff implies --debug-bytecode
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

    // Load and execute startup script if specified (using bytecode VM by
    // default)
    if (!cfg.scriptFile.empty()) {
      std::ifstream file(cfg.scriptFile);
      if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string code = buffer.str();

#ifdef ENABLE_HAVEL_LANG
        // Try bytecode VM first, fall back to AST interpreter
        auto *bytecodeVM =
            reinterpret_cast<havel::compiler::VM *>(havelApp.getBytecodeVM());
        auto *hostBridge = reinterpret_cast<havel::compiler::HostBridge *>(
            havelApp.getHostBridge());

        if (bytecodeVM && hostBridge) {
          info("Executing startup script with bytecode VM: {}", cfg.scriptFile);

          havel::compiler::PipelineOptions options = hostBridge->options();
          options.vm_override = bytecodeVM;

          // Enable bytecode debug output if requested
          if (cfg.debugBytecode) {
            options.write_snapshot_artifact = true;
            options.snapshot_dir = "/tmp/havel-bytecode";

            // Compare with previous snapshot if --diff requested
            if (cfg.diffBytecode) {
              std::ifstream prevSnapshot("/tmp/havel-bytecode/previous.txt");
              if (prevSnapshot) {
                std::string prevContent(
                    (std::istreambuf_iterator<char>(prevSnapshot)),
                    std::istreambuf_iterator<char>());
                // Will compare after execution
              }
            }
          }

          try {
            auto vmResult =
                havel::compiler::runBytecodePipeline(code, "__main__", options);

            // Print bytecode debug info if requested
            if (cfg.debugBytecode && !vmResult.snapshot.bytecode.empty()) {
              info("=== Bytecode Debug Output ===");
              info("{}", vmResult.snapshot.bytecode);

              // Save snapshot for diff comparison
              std::ofstream snapshot("/tmp/havel-bytecode/current.txt");
              if (snapshot) {
                snapshot << vmResult.snapshot.bytecode;
              }

              // Compare with previous if --diff requested
              if (cfg.diffBytecode) {
                std::ifstream prevSnapshot("/tmp/havel-bytecode/previous.txt");
                if (prevSnapshot) {
                  std::string prevContent(
                      (std::istreambuf_iterator<char>(prevSnapshot)),
                      std::istreambuf_iterator<char>());
                  if (prevContent != vmResult.snapshot.bytecode) {
                    info("=== BYTECODE DIFF ===");
                    info("Bytecode has changed from previous run");
                  } else {
                    info("Bytecode unchanged from previous run");
                  }
                }
                // Save current as previous for next run
                std::ofstream prevFile("/tmp/havel-bytecode/previous.txt");
                if (prevFile) {
                  prevFile << vmResult.snapshot.bytecode;
                }
              }
            }

            info("Bytecode execution completed successfully");
          } catch (const std::exception &e) {
            error("Bytecode execution error: {}", e.what());
          }
        }
        // Note: AST interpreter removed - bytecode VM is the only execution
        // engine
#endif
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

    // Execute with bytecode VM through HavelApp
    auto *bytecodeVM = havelApp.getBytecodeVM();
    auto *hostBridge = havelApp.getHostBridge();

    if (!bytecodeVM || !hostBridge) {
      error("Bytecode VM not available");
      return 1;
    }

    try {
      havel::compiler::PipelineOptions options = hostBridge->options();
      options.vm_override = bytecodeVM;

      auto vmResult =
          havel::compiler::runBytecodePipeline(code, "__main__", options);
      info("Startup script executed successfully with bytecode VM");
    } catch (const std::exception &e) {
      error("Startup script error: {}", e.what());
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
      // Create minimal context (no services needed for pure script execution)
      havel::HostContext ctx;

      // Create VM
      havel::compiler::VM tempVm;

      // Assign VM to context so HostBridge can access it
      ctx.vm = &tempVm;

      // Create HostBridge with minimal context
      auto bridge = havel::compiler::createHostBridge(ctx);
      bridge->install();

      // Register stdlib modules with VM (VM-native)
      havel::registerStdLibWithVM(*bridge);

      havel::compiler::PipelineOptions options = bridge->options();
      options.vm_override = &tempVm;

      // Copy VM's host functions to options for compiler and execution
      // This makes built-in functions (toInt, toFloat, etc.) available
      for (const auto &[name, fn] : tempVm.getHostFunctions()) {
        options.host_functions[name] = fn;
      }

      // Add VM's function names to host_global_names so compiler knows about
      // them

      options.host_global_names.insert("int");
      options.host_global_names.insert("num");
      options.host_global_names.insert("str");
      options.host_global_names.insert("print");
      options.host_global_names.insert("clock_ms");
      options.host_global_names.insert("sleep_ms");
      options.host_global_names.insert("system.gc");
      options.host_global_names.insert("system_gc");

      // Use the same VM for execution (so it has all the registered functions)
      options.vm_override = &tempVm;

      // Enable bytecode debug output if requested
      options.write_snapshot_artifact = cfg.debugBytecode;
      if (cfg.debugBytecode) {
        options.snapshot_dir = "/tmp/havel-bytecode";
      }

      auto vmResult =
          havel::compiler::runBytecodePipeline(code, "__main__", options);

      // Print bytecode debug info if requested
      if (cfg.debugBytecode && vmResult.snapshot.bytecode.empty() == false) {
        info("=== Bytecode Debug Output ===");
        info("{}", vmResult.snapshot.bytecode);
      }

      info("Bytecode execution completed successfully");

      // Explicit shutdown to clear containers before ASan leak check
      bridge->shutdown();

      return 0;
    } catch (const std::exception &e) {
      error("Bytecode error: {}", e.what());
      return 1;
    }
  }

  // Bytecode VM not available - error
  error("Bytecode VM not available");
  return 1;
}

int havel::init::HavelLauncher::runScriptAndRepl(const LaunchConfig &, int,
                                                 char *[]) {
  // Interpreter removed - REPL not available
  error("REPL mode not available - interpreter removed");
  error("Use --run mode for bytecode VM execution instead");
  return 1;
}

void havel::init::HavelLauncher::showHelp() {
  std::cout << "  --debug-bytecode, -dbc  Enable bytecode debugging\n";
  std::cout << "  --diff              Compare bytecode with previous run "
               "(implies -dbc)\n";
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
  std::cout << "\nBytecode debugging:\n";
  std::cout << "  --debug-bytecode        Print bytecode to console\n";
  std::cout << "  --diff                  Compare bytecode with previous run\n";
  std::cout << "  Snapshots saved to: /tmp/havel-bytecode/\n";
} // namespace havel::init
// STUBBED - interpreter removed
int havel::init::HavelLauncher::runRepl(const LaunchConfig &) {
  error("REPL not available - interpreter removed");
  return 1;
}

int havel::init::HavelLauncher::runCli(int, char *[]) {
  error("CLI not available - interpreter removed");
  return 1;
}

// DEBUG
#include <iostream>
