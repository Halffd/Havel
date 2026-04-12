#include "../Havel.hpp"
#include "HavelLauncher.hpp"
#include "Havel.hpp"
#include "core/ConfigManager.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/tools/REPL.hpp"
#include "havel-lang/utils/ErrorPrinter.hpp"
#include "modules/HostModules.hpp"
#include "utils/Logger.hpp"
#ifdef HAVE_QT_EXTENSION
#include <QApplication>
#include <QProcess>
#endif
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>

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

    if (cfg.buildOnly) {
      return runBuild(cfg);
    }

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
    case Mode::TEST:
      return runTest(cfg);
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
    } else if (arg == "--minimal" || arg == "-m") {
      // Minimal mode - no IO/hotkeys/GUI
      cfg.minimalMode = true;
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
      // Pure script execution - auto enables minimal mode
      cfg.mode = Mode::SCRIPT_ONLY;
      cfg.minimalMode = true;
      // Next argument should be the script file
      if (i + 1 < argc) {
        cfg.scriptFiles.push_back(argv[++i]);
      }
    } else if (arg == "--test" || arg == "-t") {
      // Test mode - run all .hv files in a directory
      cfg.mode = Mode::TEST;
      cfg.minimalMode = true;
      // Next argument should be the test directory
      if (i + 1 < argc) {
        cfg.testDir = argv[++i];
      }
    } else if (arg == "--lint") {
      cfg.lintOnly = true;
      if (i + 1 < argc) {
        cfg.scriptFiles.push_back(argv[++i]);
      }
    } else if (arg == "--build") {
      cfg.buildOnly = true;
      if (i + 1 < argc) {
        cfg.scriptFiles.push_back(argv[++i]);
      }
    } else if (arg == "--output" || arg == "-o") {
      if (i + 1 < argc) {
        cfg.outputPath = argv[++i];
      }
    } else if (arg == "--help" || arg == "-h") {
      showHelp();
      exit(0);
    } else if (arg == "lexer") {
      cfg.mode = Mode::CLI;
      return cfg;
    } else {
      if (arg.size() < 3 || arg.substr(arg.size() - 3) != ".hv") {
        warning("Script file {} does not end with .hv extension", arg);
      }
      cfg.scriptFiles.push_back(arg);
      if (cfg.mode == Mode::DAEMON) {
        cfg.mode = Mode::SCRIPT;
      }
    }
  }

  // Determine mode based on flags and script file
  if (repl && !cfg.scriptFiles.empty()) {
    // Script + REPL mode - full features by default
    cfg.mode = Mode::SCRIPT_AND_REPL;
  } else if (repl && cfg.fullRepl) {
    // Full REPL mode - full features even without script
    cfg.mode = Mode::SCRIPT_AND_REPL;
  } else if (repl && cfg.minimalMode) {
    // Minimal REPL mode
    cfg.mode = Mode::REPL;
  } else if (repl) {
    // REPL only mode - full features by default
    cfg.mode = Mode::SCRIPT_AND_REPL;
  } else if (cfg.scriptFiles.empty() && cfg.mode == Mode::DAEMON) {
    // No script, no REPL, no GUI flag - default to REPL mode
    cfg.mode = Mode::REPL;
  }
  // Otherwise use the mode already set (GUI_ONLY, SCRIPT_ONLY, SCRIPT, CLI)

  return cfg;
}

int HavelLauncher::runDaemon(const LaunchConfig &cfg, int argc, char *argv[]) {
#ifdef HAVE_QT_EXTENSION
  // Load scripts first
  std::string combinedCode;
  std::string combinedNames;
  for (const auto& f : cfg.scriptFiles) {
    std::ifstream file(f);
    if (file) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      combinedCode += buffer.str() + "\n";
      if (!combinedNames.empty()) combinedNames += " + ";
      combinedNames += f;
    } else {
      error("Cannot open startup script: {}", f);
    }
  }

  // LINT-ONLY MODE: Parse and type-check ONLY, skip ALL Qt/VM initialization
  if (cfg.lintOnly && !combinedCode.empty()) {
    info("Linting scripts: {}", combinedNames);
    havel::parser::Parser parser{{
      .lexer = cfg.debugLexer,
      .parser = cfg.debugParser,
      .ast = cfg.debugAst
    }};
    std::string primaryFile = combinedNames.empty() ? "input" : combinedNames;
    std::unique_ptr<havel::ast::Program> program;
    try {
      program = parser.produceAST(combinedCode);
    } catch (const std::exception& e) {
      // Parser aborted due to too many errors — still print what we collected
    }
    if (parser.hasErrors()) {
      for (const auto& err : parser.getErrors()) {
        // Get source line content for pretty formatting
        std::string sourceLine;
        if (err.line > 0) {
          std::istringstream ss(combinedCode);
          std::string line;
          for (size_t i = 1; i <= err.line; ++i) {
            if (!std::getline(ss, line)) break;
            if (i == err.line) { sourceLine = line; break; }
          }
        }
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.message, primaryFile,
            err.line, err.column, 1, sourceLine);
        std::cerr << formatted;
      }
      error("Linting failed with {} error(s)", parser.getErrors().size());
      return 1;
    }
    if (!program) {
      error("Parser returned null AST");
      return 1;
    }

    // Also run compiler in error-collection mode to catch compile errors
    havel::compiler::ByteCompiler compiler;
    compiler.setCollectErrors(true);
    try {
      auto chunk = compiler.compile(*program);
      (void)chunk;
    } catch (const std::exception& e) {
      // Compiler aborted — still print what we collected
    }
    if (compiler.hasErrors()) {
      for (const auto& err : compiler.errors()) {
        std::string sourceLine;
        if (err.line > 0) {
          std::istringstream ss(combinedCode);
          std::string line;
          for (size_t i = 1; i <= err.line; ++i) {
            if (!std::getline(ss, line)) break;
            if (i == err.line) { sourceLine = line; break; }
          }
        }
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.message, primaryFile,
            err.line, err.column, 1, sourceLine);
        std::cerr << formatted;
      }
      error("Compilation failed with {} error(s)", compiler.errors().size());
      return 1;
    }

    info("Linting successful");
    return 0;
  }

  // Restart loop - keeps restarting until exit code is not 42
  while (true) {
    QApplication app(argc, argv);
    app.setApplicationName("havel");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("havel");
    app.setQuitOnLastWindowClosed(false);

    // Convert argc/argv to vector<string> for havel::Havel
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    havel::Havel havel_inst(cfg.isStartup, "", false, true, args);

    if (!havel_inst.isInitialized()) {
      error("Failed to initialize havel::Havel");
      return 1;
    }

    // Execute with bytecode VM
    if (!combinedCode.empty()) {
      auto *bytecodeVM =
          reinterpret_cast<havel::compiler::VM *>(havel_inst.getBytecodeVM());
      auto *hostBridge = reinterpret_cast<havel::compiler::HostBridge *>(
          havel_inst.getHostBridge());

      if (bytecodeVM && hostBridge) {
        info("Executing combined scripts with bytecode VM: {}", combinedNames);

        havel::compiler::PipelineOptions options = hostBridge->options();
        options.compile_unit_name = combinedNames;
        options.vm_override = bytecodeVM;
        options.debugBytecode = cfg.debugBytecode;

        try {
          havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
          info("Execution completed successfully");
        } catch (const std::exception &e) {
          error("Execution error: {}", e.what());
        }
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
#else
  (void)cfg; (void)argc; (void)argv;
  error("Qt extension not available - daemon mode requires Qt");
  return 1;
#endif
}

int HavelLauncher::runScript(const LaunchConfig &cfg, int argc, char *argv[]) {
#ifdef HAVE_QT_EXTENSION
  while (true) {
    std::string combinedCode;
    std::string combinedNames;
    for (const auto& f : cfg.scriptFiles) {
      std::ifstream file(f);
      if (file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        combinedCode += buffer.str() + "\n";
        if (!combinedNames.empty()) combinedNames += " + ";
        combinedNames += f;
      } else {
        error("Cannot open script file: {}", f);
        return 2;
      }
    }

    if (combinedCode.empty()) {
      error("No script code provided");
      return 1;
    }

    // LINT-ONLY MODE: Parse and type-check ONLY, no execution
    if (cfg.lintOnly) {
       havel::parser::Parser parser{{
         .lexer = cfg.debugLexer,
         .parser = cfg.debugParser,
         .ast = cfg.debugAst
       }};
       std::string primaryFile = combinedNames.empty() ? "input" : combinedNames;
       std::unique_ptr<havel::ast::Program> program;
       try {
         program = parser.produceAST(combinedCode);
       } catch (const std::exception& e) {
         // Parser aborted — still print what we collected
       }
       if (parser.hasErrors()) {
         for (const auto& err : parser.getErrors()) {
           std::string sourceLine;
           if (err.line > 0) {
             std::istringstream ss(combinedCode);
             std::string line;
             for (size_t i = 1; i <= err.line; ++i) {
               if (!std::getline(ss, line)) break;
               if (i == err.line) { sourceLine = line; break; }
             }
           }
           std::string formatted = havel::ErrorPrinter::formatError(
               "error", err.message, primaryFile,
               err.line, err.column, 1, sourceLine);
           std::cerr << formatted;
         }
         error("Linting failed with {} error(s)", parser.getErrors().size());
         return 1;
       }
       if (program) {
         // Also check compiler errors
         havel::compiler::ByteCompiler compiler;
         compiler.setCollectErrors(true);
         try {
           auto chunk = compiler.compile(*program);
           (void)chunk;
         } catch (const std::exception& e) {}
         if (compiler.hasErrors()) {
           for (const auto& err : compiler.errors()) {
             std::string sourceLine;
             if (err.line > 0) {
               std::istringstream ss(combinedCode);
               std::string line;
               for (size_t i = 1; i <= err.line; ++i) {
                 if (!std::getline(ss, line)) break;
                 if (i == err.line) { sourceLine = line; break; }
               }
             }
             std::string formatted = havel::ErrorPrinter::formatError(
                 "error", err.message, primaryFile,
                 err.line, err.column, 1, sourceLine);
             std::cerr << formatted;
           }
           error("Compilation failed with {} error(s)", compiler.errors().size());
           return 1;
         }
       }
       info("Linting successful");
       return 0;
    }

    info("Running combined scripts: {}", combinedNames);

    int dummy_argc = 1;
    char dummy_name[] = "havel-script";
    char *dummy_argv[] = {dummy_name, nullptr};
    QApplication app(dummy_argc, dummy_argv);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);

    havel::Havel havel_inst(false, combinedNames, false, true, args);

    if (!havel_inst.isInitialized()) {
      error("Failed to initialize havel::Havel");
      return 1;
    }

    auto *bytecodeVM = havel_inst.getBytecodeVM();
    auto *hostBridge = havel_inst.getHostBridge();

    if (!bytecodeVM || !hostBridge) {
      error("Bytecode VM not available");
      return 1;
    }

    try {
      havel::compiler::PipelineOptions options = hostBridge->options();
      options.compile_unit_name = combinedNames;
      options.vm_override = bytecodeVM;
      options.debugBytecode = cfg.debugBytecode;
      auto vmResult = havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
      info("Execution successful");
    } catch (const std::exception &e) {
      error("Execution error: {}", e.what());
      return 1;
    }

    auto *hkManager = havel_inst.getHotkeyManagerPtr();
    if (hkManager) {
      hkManager->printHotkeys();
      hkManager->updateAllConditionalHotkeys();
    }
    info("Scripts loaded. Hotkeys registered. Press Ctrl+C to exit.");
    int exitCode = app.exec();

    if (exitCode == 42) continue;
    return exitCode;
  }

  return 0; // No restart in headless mode
#else
  (void)cfg; (void)argc; (void)argv;
  error("Qt extension not available - script mode requires Qt");
  return 1;
#endif
} // End of restart loop

int havel::init::HavelLauncher::runScriptOnly(const LaunchConfig &cfg, int argc,
                                              char *argv[]) {
  // Pure script execution without IO, hotkeys, display, or GUI
  std::string combinedCode;
  std::string combinedNames;
  for (const auto& f : cfg.scriptFiles) {
    std::ifstream file(f);
    if (file) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      combinedCode += buffer.str() + "\n";
      if (!combinedNames.empty()) combinedNames += " + ";
      combinedNames += f;
    } else {
      error("Cannot open script file: {}", f);
      return 2;
    }
  }

  if (combinedCode.empty()) return 0;

  // LINT-ONLY MODE: Parse and type-check ONLY, no execution
  if (cfg.lintOnly) {
    havel::parser::Parser parser{{
      .lexer = cfg.debugLexer,
      .parser = cfg.debugParser,
      .ast = cfg.debugAst
    }};
    std::string primaryFile = combinedNames.empty() ? "input" : combinedNames;
    std::unique_ptr<havel::ast::Program> program;
    try {
      program = parser.produceAST(combinedCode);
    } catch (const std::exception& e) {
      // Parser aborted — still print what we collected
    }
    if (parser.hasErrors()) {
      for (const auto& err : parser.getErrors()) {
        std::string sourceLine;
        if (err.line > 0) {
          std::istringstream ss(combinedCode);
          std::string line;
          for (size_t i = 1; i <= err.line; ++i) {
            if (!std::getline(ss, line)) break;
            if (i == err.line) { sourceLine = line; break; }
          }
        }
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.message, primaryFile,
            err.line, err.column, 1, sourceLine);
        std::cerr << formatted;
      }
      error("Linting failed with {} error(s)", parser.getErrors().size());
      return 1;
    }
    if (program) {
      havel::compiler::ByteCompiler compiler;
      compiler.setCollectErrors(true);
      try {
        auto chunk = compiler.compile(*program);
        (void)chunk;
      } catch (const std::exception& e) {}
      if (compiler.hasErrors()) {
        for (const auto& err : compiler.errors()) {
          std::string sourceLine;
          if (err.line > 0) {
            std::istringstream ss(combinedCode);
            std::string line;
            for (size_t i = 1; i <= err.line; ++i) {
              if (!std::getline(ss, line)) break;
              if (i == err.line) { sourceLine = line; break; }
            }
          }
          std::string formatted = havel::ErrorPrinter::formatError(
              "error", err.message, primaryFile,
              err.line, err.column, 1, sourceLine);
          std::cerr << formatted;
        }
        error("Compilation failed with {} error(s)", compiler.errors().size());
        return 1;
      }
    }
    info("Linting successful");
    return 0;
  }

  info("Running Havel scripts (pure mode): {}", combinedNames);

  // Set up signal handling ...
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = [](int sig) { std::exit(0); };
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  try {
    havel::HostContext ctx;
    havel::compiler::VM tempVm;
    ctx.vm = &tempVm;
    auto bridge = havel::compiler::createHostBridge(ctx);
    bridge->install();
    havel::registerStdLibWithVM(*bridge);
    
    havel::compiler::PipelineOptions options = bridge->options();
    options.compile_unit_name = combinedNames;
    options.vm_override = &tempVm;
    options.debugBytecode = cfg.debugBytecode;
    
    // Add built-ins ...
    options.host_global_names.insert("print");
    options.host_global_names.insert("assert");

    auto vmResult = havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
    info("Bytecode execution completed successfully");
    bridge->shutdown();
    return 0;
  } catch (const std::exception &e) {
    error("Bytecode error: {}", e.what());
    return 1;
  }

  // Bytecode VM not available - error
  error("Bytecode VM not available");
  return 1;
}

int havel::init::HavelLauncher::runScriptAndRepl(const LaunchConfig &cfg, int,
                                                 char *[]) {
  try {
    if (cfg.minimalMode) {
      if (cfg.scriptFiles.empty()) {
        error("No script file provided");
        return 1;
      }
      info("Running scripts and starting REPL in minimal mode...");

      std::string combinedCode;
      for (const auto& f : cfg.scriptFiles) {
        std::ifstream file(f);
        if (file) {
          std::stringstream buffer;
          buffer << file.rdbuf();
          combinedCode += buffer.str() + "\n";
        }
      }

      auto hostAPI = std::make_shared<HostAPI>(nullptr, nullptr, Configs::Get());
      havel::initializeServiceRegistry(hostAPI);
      havel::repl::REPLConfig replConfig;
      replConfig.debugMode = cfg.debugMode;
      replConfig.stopOnError = cfg.stopOnError;
      havel::repl::REPL repl(replConfig);
      repl.initialize(hostAPI);
      
      info("Executing script code...");
      if (!repl.execute(combinedCode)) {
        error("Script execution failed");
        return 1;
      }
      return repl.run();
    } else {
#ifdef HAVE_QT_EXTENSION
      info("Running scripts and starting REPL with full features...");
      std::string combinedCode;
      std::string combinedNames;
      for (const auto& f : cfg.scriptFiles) {
        std::ifstream file(f);
        if (file) {
          std::stringstream buffer;
          buffer << file.rdbuf();
          combinedCode += buffer.str() + "\n";
          if (!combinedNames.empty()) combinedNames += " + ";
          combinedNames += f;
        }
      }

      int dummy_argc = 1;
      char dummy_name[] = "havel-script-repl";
      char *dummy_argv[] = {dummy_name, nullptr};
      QApplication app(dummy_argc, dummy_argv);
      app.setQuitOnLastWindowClosed(false);
      
      std::vector<std::string> args;
      havel::Havel havel_inst(false, combinedNames, false, true, args);
      
      if (!havel_inst.isInitialized()) return 1;
      
      auto *bytecodeVM = havel_inst.getBytecodeVM();
      auto *hostBridge = havel_inst.getHostBridge();
      
      if (!bytecodeVM || !hostBridge) return 1;

      try {
        havel::compiler::PipelineOptions options = hostBridge->options();
        options.compile_unit_name = combinedNames;
        options.vm_override = bytecodeVM;
        options.debugBytecode = cfg.debugBytecode;
        havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
      } catch (const std::exception &e) {
        error("Script execution error: {}", e.what());
        return 1;
      }

      // Print registered hotkeys
      auto *hkManager = havel_inst.getHotkeyManagerPtr();
      if (hkManager) {
        hkManager->printHotkeys();
        hkManager->updateAllConditionalHotkeys();
      }
      info("Script loaded. Hotkeys registered. Enter REPL...");
      
      // Create REPL with full host API
      havel::repl::REPLConfig replConfig;
      replConfig.debugMode = cfg.debugMode;
      replConfig.stopOnError = cfg.stopOnError;
      
      havel::repl::REPL repl(replConfig);
      
      // Create host API with full features
      auto hostAPI = std::shared_ptr<HostAPI>(new HostAPI(
          havel_inst.getIOPtr(),
          havel_inst.getHotkeyManagerPtr(),
          Configs::Get(),
          havel_inst.getWindowManagerPtr(),
          nullptr, nullptr, nullptr, nullptr, nullptr,
          nullptr, nullptr, nullptr, nullptr, nullptr,
          hkManager ? hkManager->getModeManager().get() : nullptr,
          std::vector<std::string>{}));
      
      havel::initializeServiceRegistry(hostAPI);
      repl.initialize(hostAPI);

      // Enter REPL
      return repl.run();
#else
      error("Qt extension not available for full mode. Use --minimal flag.");
      return 1;
#endif
    }
  } catch (const std::exception &e) {
    error("Script+REPL error: {}", e.what());
    return 1;
  }
}

void havel::init::HavelLauncher::showHelp() {
  std::cout << "  --debug-bytecode, -dbc  Enable bytecode debugging\n";
  std::cout << "  --diff              Compare bytecode with previous run "
               "(implies -dbc)\n";
  std::cout << "  --error, -e         Stop on first error/warning\n";
  std::cout << "  --minimal, -m       Minimal mode (no IO/hotkeys/GUI)\n";
  std::cout << "  --repl, -r          Start interactive REPL (full features)\n";
  std::cout << "  --full-repl, -fr    Start REPL with ALL features (hotkeys, "
               "GUI, etc.)\n";
  std::cout << "  --gui               GUI-only mode (no hotkeys)\n";
  std::cout
      << "  --run               Run script in minimal mode (auto-enables -m)\n";
  std::cout
      << "  --test, -t          Run all .hv scripts in a directory\n";
  std::cout << "  --lint              Check syntax and compilation errors\n";
  std::cout << "  --build             Compile to .hvc bytecode file\n";
  std::cout << "  --output, -o PATH   Set output path for --build\n";
  std::cout << "  --help, -h          Show this help\n";
  std::cout << "\nIf a .hv script file is provided, it will be executed.\n";
  std::cout << "If no arguments are provided, starts interactive REPL with full features.\n";
  std::cout << "\nModes:\n";
  std::cout << "  havel                   - Start interactive REPL (full features)\n";
  std::cout << "  havel script.hv         - Run script with FULL features\n";
  std::cout << "  havel --repl script.hv    - Run script then REPL (FULL features)\n";
  std::cout << "  havel --repl              - Start REPL with FULL features\n";
  std::cout << "  havel --full-repl         - Start REPL with ALL features\n";
  std::cout << "  havel --run script.hv     - Run script in MINIMAL mode\n";
  std::cout << "  havel --test dir/         - Run all .hv files in directory\n";
  std::cout << "  havel --lint script.hv    - Check for errors without running\n";
  std::cout << "  havel --build script.hv   - Compile to bytecode (.hvc)\n";
  std::cout << "  havel --build script.hv -o out.hvc  - Compile to specific file\n";
  std::cout << "  havel --minimal script.hv - Run script in MINIMAL mode\n";
  std::cout << "  havel --repl --minimal    - Start REPL in MINIMAL mode\n";
  std::cout << "\nFull mode (default):\n";
  std::cout << "  All features enabled: hotkeys, GUI, display, IO, etc.\n";
  std::cout << "  Use for normal automation and hotkey scripts.\n";
  std::cout << "\nMinimal mode (--minimal or --run):\n";
  std::cout << "  Executes scripts without IO, hotkeys, display, or GUI.\n";
  std::cout
      << "  Useful for testing scripts that auto-exit or don't need input.\n";
  std::cout << "  Example: havel --run scripts/test_types.hv\n";
  std::cout << "\nBytecode debugging:\n";
  std::cout << "  --debug-bytecode        Print bytecode to console\n";
  std::cout << "  --diff                  Compare bytecode with previous run\n";
  std::cout << "  Snapshots saved to: /tmp/havel-bytecode/\n";
}

// REPL implementation using bytecode VM
int havel::init::HavelLauncher::runRepl(const LaunchConfig &cfg) {
  try {
    if (cfg.minimalMode) {
      info("Starting Havel REPL in minimal mode (no IO/hotkeys)...");
      
      // Debug: Show mode information
      std::cerr << "[DEBUG] Running in REPL mode (minimal):" << std::endl;
      std::cerr << "  - GUI: disabled" << std::endl;
      std::cerr << "  - IO/Hotkeys: disabled" << std::endl;

      // Create minimal host API for REPL (no IO, no hotkeys)
      auto hostAPI = std::make_shared<HostAPI>(nullptr, nullptr, Configs::Get());

      // Create REPL
      havel::repl::REPLConfig replConfig;
      replConfig.debugMode = cfg.debugMode;
      replConfig.stopOnError = cfg.stopOnError;

      havel::repl::REPL repl(replConfig);
      repl.initialize(hostAPI);

      // Run REPL
      return repl.run();
    } else {
#ifdef HAVE_QT_EXTENSION
      info("Starting Havel REPL with full features (hotkeys, GUI)...");
      
      // Debug: Show mode information
      std::cerr << "[DEBUG] Running in REPL mode (full):" << std::endl;
      std::cerr << "  - GUI: enabled" << std::endl;
      std::cerr << "  - IO/Hotkeys: enabled" << std::endl;

      // Full mode - initialize Qt and havel::Havel
      int dummy_argc = 1;
      char dummy_name[] = "havel-repl";
      char *dummy_argv[] = {dummy_name, nullptr};
      QApplication app(dummy_argc, dummy_argv);
      app.setQuitOnLastWindowClosed(false);

      // Create havel::Havel with full features
      std::vector<std::string> args;
      havel::Havel havel_inst(false, "", false, true, args);
      
      if (!havel_inst.isInitialized()) {
        error("Failed to initialize havel::Havel");
        return 1;
      }
      
      // Get VM and host bridge from havel::Havel
      auto *bytecodeVM = reinterpret_cast<havel::compiler::VM *>(havel_inst.getBytecodeVM());
      auto *hostBridge = reinterpret_cast<havel::compiler::HostBridge *>(havel_inst.getHostBridge());
      
      if (!bytecodeVM || !hostBridge) {
        error("Bytecode VM or HostBridge not available");
        return 1;
      }
      
      // Create REPL with full host API
      havel::repl::REPLConfig replConfig;
      replConfig.debugMode = cfg.debugMode;
      replConfig.stopOnError = cfg.stopOnError;
      
      havel::repl::REPL repl(replConfig);
      
      // Create host API with full features
      auto *hkManager = havel_inst.getHotkeyManagerPtr();
      auto hostAPI = std::shared_ptr<HostAPI>(new HostAPI(
          havel_inst.getIOPtr(),
          havel_inst.getHotkeyManagerPtr(),
          Configs::Get(),
          havel_inst.getWindowManagerPtr(),
          nullptr, nullptr, nullptr, nullptr, nullptr,
          nullptr, nullptr, nullptr, nullptr, nullptr,
          hkManager ? hkManager->getModeManager().get() : nullptr,
          std::vector<std::string>{}));
      
      havel::initializeServiceRegistry(hostAPI);
      repl.initialize(hostAPI);

      // Run REPL
      return repl.run();
#else
      error("Qt extension not available for full REPL mode. Use --minimal flag.");
      return 1;
#endif
    }
  } catch (const std::exception &e) {
    error("REPL error: {}", e.what());
    return 1;
  }
}

int havel::init::HavelLauncher::runTest(const LaunchConfig &cfg) {
  if (cfg.testDir.empty()) {
    error("No test directory specified. Usage: havel --test <directory>");
    return 1;
  }

  // Find all .hv files in the directory
  std::vector<std::string> testFiles;
  try {
    for (const auto &entry : std::filesystem::directory_iterator(cfg.testDir)) {
      if (entry.is_regular_file()) {
        std::string path = entry.path().string();
        if (path.size() >= 3 && path.substr(path.size() - 3) == ".hv") {
          testFiles.push_back(path);
        }
      }
    }
  } catch (const std::exception &e) {
    error("Failed to read test directory '{}': {}", cfg.testDir, e.what());
    return 1;
  }

  if (testFiles.empty()) {
    error("No .hv files found in '{}'", cfg.testDir);
    return 1;
  }

  // Sort for consistent output
  std::sort(testFiles.begin(), testFiles.end());

  // Get path to self for running tests
  std::string selfPath = "/proc/self/exe";
  char selfBuf[PATH_MAX];
  ssize_t len = readlink(selfPath.c_str(), selfBuf, sizeof(selfBuf) - 1);
  if (len == -1) {
    // Fallback: try argv[0]
    selfPath = "havel";
  } else {
    selfBuf[len] = '\0';
    selfPath = std::string(selfBuf);
  }

  // Run each test as a subprocess with timeout
  int passed = 0;
  int failed = 0;
  int total = static_cast<int>(testFiles.size());

  info("Running {} tests from '{}'", total, cfg.testDir);

  for (const auto &testFile : testFiles) {
    std::string testName = testFile.substr(testFile.find_last_of('/') + 1);

    // Run test as subprocess with 10 second timeout
    std::string cmd = "timeout 10 " + selfPath + " --run " + testFile + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      error("  FAIL {} - failed to run", testName);
      failed++;
      continue;
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
      output += buf;
    }
    int status = pclose(pipe);
    int exitCode = WEXITSTATUS(status);

    if (exitCode == 0) {
      info("  PASS {}", testName);
      passed++;
    } else {
      // Extract error message
      std::string errLine;
      std::istringstream iss(output);
      std::string line;
      while (std::getline(iss, line)) {
        if (line.find("[ERROR]") != std::string::npos ||
            line.find("Error:") != std::string::npos) {
          errLine = line;
          // Clean up ANSI/color codes
          errLine.erase(std::remove(errLine.begin(), errLine.end(), '\r'), errLine.end());
          break;
        }
      }
      if (exitCode == 124) {
        error("  FAIL {} - timeout (10s)", testName);
      } else if (!errLine.empty()) {
        // Extract just the error message after [ERROR] or Error:
        size_t pos = errLine.find("[ERROR]");
        if (pos != std::string::npos) {
          errLine = errLine.substr(pos + 7);
        } else {
          pos = errLine.find("Error:");
          if (pos != std::string::npos) {
            errLine = errLine.substr(pos + 6);
          }
        }
        // Trim leading space
        while (!errLine.empty() && errLine[0] == ' ') errLine.erase(0, 1);
        error("  FAIL {} - {}", testName, errLine);
      } else {
        error("  FAIL {} - exit code {}", testName, exitCode);
      }
      failed++;
    }
  }

  // Print summary
  std::cout << "\n";
  info("Test Results:");
  info("  Total:  {}", total);
  info("  Passed: {}", passed);
  info("  Failed: {}", failed);

  if (failed > 0) {
    error("{} test(s) failed", failed);
    return 1;
  }

  info("All tests passed!");
  return 0;
}

int havel::init::HavelLauncher::runCli(int, char *[]) {
  error("CLI not available - interpreter removed");
  return 1;
}

int havel::init::HavelLauncher::runBuild(const LaunchConfig &cfg) {
  // Build mode: compile .hv files to .hvc bytecode
  std::string combinedCode;
  std::string primaryFile;
  for (const auto& f : cfg.scriptFiles) {
    std::ifstream file(f);
    if (file) {
      std::stringstream buffer;
      buffer << file.rdbuf();
      combinedCode += buffer.str() + "\n";
      if (primaryFile.empty()) primaryFile = f;
    } else {
      error("Cannot open script file: {}", f);
      return 1;
    }
  }

  if (combinedCode.empty()) {
    error("No script files to build");
    return 1;
  }

  // Determine output path
  std::string outputPath = cfg.outputPath;
  if (outputPath.empty()) {
    // Default: replace .hv with .hvc
    if (!primaryFile.empty()) {
      outputPath = primaryFile;
      size_t dotPos = outputPath.rfind('.');
      if (dotPos != std::string::npos) {
        outputPath.erase(dotPos);
      }
      outputPath += ".hvc";
    } else {
      outputPath = "output.hvc";
    }
  }

  info("Building: {} -> {}", primaryFile.empty() ? "input" : primaryFile, outputPath);

  // Parse
  havel::parser::Parser parser{{
    .lexer = cfg.debugLexer,
    .parser = cfg.debugParser,
    .ast = cfg.debugAst
  }};
  std::unique_ptr<havel::ast::Program> program;
  try {
    program = parser.produceAST(combinedCode);
  } catch (const std::exception& e) {
    error("Parse error: {}", e.what());
    return 1;
  }
  if (parser.hasErrors()) {
    for (const auto& err : parser.getErrors()) {
      std::string formatted = havel::ErrorPrinter::formatError(
          "error", err.message, primaryFile,
          err.line, err.column, 1, "");
      std::cerr << formatted;
    }
    error("Build failed with {} parse error(s)", parser.getErrors().size());
    return 1;
  }
  if (!program) {
    error("Parser returned null AST");
    return 1;
  }

  // Compile to bytecode
  havel::compiler::ByteCompiler compiler;
  if (cfg.debugBytecode) {
    compiler.setCollectErrors(true);
  }
  std::unique_ptr<havel::compiler::BytecodeChunk> chunk;
  try {
    chunk = compiler.compile(*program);
  } catch (const std::exception& e) {
    error("Compile error: {}", e.what());
    return 1;
  }
  if (compiler.hasErrors()) {
    for (const auto& err : compiler.errors()) {
      std::string formatted = havel::ErrorPrinter::formatError(
          "error", err.message, primaryFile,
          err.line, err.column, 1, "");
      std::cerr << formatted;
    }
    error("Build failed with {} compile error(s)", compiler.errors().size());
    return 1;
  }
  if (!chunk) {
    error("Compiler returned null chunk");
    return 1;
  }

  // Serialize and write
  havel::compiler::ValueSerializer serializer;
  auto data = serializer.serializeChunk(*chunk);
  std::ofstream outFile(outputPath, std::ios::binary);
  if (!outFile.is_open()) {
    error("Cannot open output file: {}", outputPath);
    return 1;
  }
  outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
  if (!outFile.good()) {
    error("Failed to write output file: {}", outputPath);
    return 1;
  }
  outFile.close();

  info("Build successful: {} ({} bytes)", outputPath, data.size());
  return 0;
}

// DEBUG
#include <iostream>

} // namespace havel::init
