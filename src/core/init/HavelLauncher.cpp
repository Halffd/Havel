#include "Havel.hpp"
#include "HavelLauncher.hpp"
#include "core/init/Havel.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "core/config/ConfigManager.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/core/ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#include "havel-lang/lexer/Lexer.hpp"
#include "havel-lang/parser/Parser.h"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/tools/REPL.hpp"
#include "havel-lang/runtime/HavelEngine.hpp"
#include "havel-lang/utils/ErrorPrinter.hpp"
#include "modules/HostModules.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include "core/util/Env.hpp"

#ifdef HAVEL_ENABLE_LLVM
// LLVM headers for AOT
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/IR/LegacyPassManager.h>
#endif

#include "host/ui/UIManager.hpp"
#include <csignal>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_set>
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

// Read a script file and strip the shebang line if present
static std::string readScriptFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return ""; // Signal failure
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  // Skip shebang line if present (#!/... or #!havel)
  if (content.size() >= 2 && content[0] == '#' && content[1] == '!') {
    size_t newline = content.find('\n');
    if (newline != std::string::npos) {
      content = content.substr(newline + 1);
    } else {
      // Entire file is just a shebang with no newline
      content.clear();
    }
  }

  return content;
}

static std::unordered_set<std::string>
collectKnownGlobals(const havel::compiler::VM *vm) {
  std::unordered_set<std::string> globals;
  if (!vm) {
    return globals;
  }
  for (const auto &[name, value] : vm->getAllGlobals()) {
    (void)value;
    if (name.empty() || name[0] == '_') {
      continue;
    }
    globals.insert(name);
  }
  return globals;
}

#ifdef HAVEL_ENABLE_LLVM
static std::string normalizeTargetOS(std::string os) {
  std::transform(os.begin(), os.end(), os.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (os == "win") return "windows";
  if (os == "mac" || os == "darwin") return "macos";
  return os;
}

static std::string mapTargetTripleForOS(const std::string &requestedOS,
                                        const std::string &fallbackTriple) {
  std::string os = normalizeTargetOS(requestedOS);
  if (os.empty() || os == "native") return fallbackTriple;
  llvm::Triple hostTriple(fallbackTriple);
  std::string arch = hostTriple.getArchName().str();
  if (arch.empty()) arch = "x86_64";
  if (os == "linux") return arch + "-pc-linux-gnu";
  if (os == "windows") return arch + "-pc-windows-msvc";
  if (os == "macos") return arch + "-apple-darwin";
  if (os == "wasm") return "wasm32-unknown-unknown";
  return fallbackTriple;
}

static std::string sharedLibraryExtensionForOS(const std::string &requestedOS) {
  const std::string os = normalizeTargetOS(requestedOS);
  if (os == "windows") return ".dll";
  if (os == "macos") return ".dylib";
  return ".so";
}

static void appendLinkLibraries(std::string &linkCmd,
                                const std::vector<std::string> &libs) {
  for (const auto &lib : libs) {
    if (lib.empty()) continue;
    if (lib.rfind("-l", 0) == 0 || lib.rfind("-L", 0) == 0 ||
        lib.rfind("/", 0) == 0 || lib.rfind(".", 0) == 0) {
      linkCmd += " " + lib;
    } else {
      linkCmd += " -l" + lib;
    }
  }
}

static void appendDefaultLlvmLinkLibraries(std::string &linkCmd) {
#ifdef HAVEL_DEFAULT_LLVM_LINK_FLAGS
  constexpr const char *kDefaultLlvmLinkFlags = HAVEL_DEFAULT_LLVM_LINK_FLAGS;
  if (kDefaultLlvmLinkFlags[0] != '\0') {
    linkCmd += " ";
    linkCmd += kDefaultLlvmLinkFlags;
  }
#else
  (void)linkCmd;
#endif
}
#endif

int HavelLauncher::run(int argc, char *argv[]) {
  try {
    LaunchConfig cfg = parseArgs(argc, argv);

    // Target mode controls execution backend defaults.
    if (cfg.target == Target::INTERPRET) {
      cfg.useJIT = false;
    } else if (cfg.target == Target::JIT) {
      cfg.useJIT = true;
    }

    // Apply JIT settings to global config
    Configs::Get().Set("Compiler.JIT", cfg.useJIT ? "true" : "false");
    Configs::Get().Set("Compiler.DebugJIT", cfg.debugJIT ? "true" : "false");
    Configs::Get().Set("Compiler.DumpIR", cfg.dumpIR ? "true" : "false");
    Configs::Get().Set("Compiler.OutputAsm", cfg.outputAsmToFile ? "true" : "false");
    Configs::Get().Set("Compiler.JITWarnings", cfg.aotWarnings ? "true" : "false");
#ifdef HAVEL_ENABLE_LLVM
    Configs::Get().Set("Compiler.JITTargetOS", normalizeTargetOS(cfg.targetOS));
#else
    Configs::Get().Set("Compiler.JITTargetOS", cfg.targetOS);
#endif


    if (cfg.buildOnly) {

        return runBuild(cfg);
    }

    // Self-hosted mode: route through launcher.hv instead of C++ pipeline
    if (cfg.selfHosted) {
        cfg.mode = Mode::SCRIPT_ONLY;
        cfg.minimalMode = true;
        cfg.pureStdlib = true;
        return runSelfHosted(cfg);
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
} else if (arg == "--debug-gc" || arg == "-dgc") {
            debugging::debug_gc = true;
            cfg.debugGc = true;
        } else if (arg == "--debug-engine" || arg == "-de") {
            debugging::debug_engine = true;
            cfg.debugEngine = true;
        } else if (arg == "--debug-io" || arg == "-dio") {
            debugging::debug_io = true;
            cfg.debugIo = true;
        } else if (arg == "--debug-hotkeys" || arg == "-dhk") {
            debugging::debug_hotkeys = true;
            cfg.debugHotkeys = true;
 } else if (arg == "--diff" || arg == "-diff") {
 cfg.diffBytecode = true;
 cfg.debugBytecode = true;
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
    } else if (arg == "--no-jit") {
      cfg.useJIT = false;
      if (cfg.target == Target::JIT) {
        cfg.target = Target::INTERPRET;
      }
    } else if (arg == "--target") {
      if (i + 1 >= argc) {
        error("--target requires one of: interpret, jit, aot, asm, ir, wasm");
        continue;
      }
      std::string target = argv[++i];
      if (target == "interpret") {
        cfg.target = Target::INTERPRET;
        cfg.useJIT = false;
      } else if (target == "jit") {
        cfg.target = Target::JIT;
        cfg.useJIT = true;
      } else if (target == "aot") {
        cfg.target = Target::AOT;
        cfg.buildOnly = true;
        cfg.emitObj = true;
        cfg.emitBinary = true;
      } else if (target == "asm") {
        cfg.target = Target::ASM;
        cfg.buildOnly = true;
        cfg.emitAsm = true;
      } else if (target == "ir") {
        cfg.target = Target::IR;
        cfg.buildOnly = true;
        cfg.emitLLVM = true;
      } else if (target == "wasm") {
        cfg.target = Target::WASM;
        cfg.buildOnly = true;
        cfg.emitWasm = true;
      } else if (target == "elf" || target == "bin") {
        cfg.target = Target::ELF;
        cfg.buildOnly = true;
        cfg.emitElf = true;
        cfg.emitObj = true;
      } else {
        error("Unknown --target '{}'. Expected: interpret, jit, aot, asm, ir, wasm, elf, bin", target);
      }
 } else if (arg == "--debug-jit" || arg == "-djt") {
            cfg.debugJIT = true;
            cfg.dumpIR = true;
            Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
    } else if (arg == "-S") {
      cfg.outputAsmToFile = true;
      cfg.dumpIR = true;
    } else if (arg == "--os") {
      if (i + 1 >= argc) {
        error("--os requires one of: native, linux, windows, macos, wasm");
      } else {
        cfg.targetOS = argv[++i];
      }
    } else if (arg == "--aot-warnings") {
      cfg.aotWarnings = true;
    } else if (arg == "--no-aot-warnings") {
      cfg.aotWarnings = false;
    } else if (arg == "--link-lib") {
      if (i + 1 >= argc) {
        error("--link-lib requires a library name or linker flag");
      } else {
        cfg.linkLibs.push_back(argv[++i]);
      }
    } else if (arg == "--profile") {
      if (i + 1 >= argc) {
        error("--profile requires one of: full, core");
      } else {
        cfg.profile = argv[++i];
      }
    } else if (arg == "--full-aot") {
      cfg.fullAot = true;
      cfg.buildOnly = true;
      cfg.target = Target::AOT;
      cfg.emitLLVM = true;
      cfg.emitAsm = true;
      cfg.emitObj = true;
      cfg.emitBinary = true;
      cfg.emitElf = true;

  } else if (arg == "--config" || arg == "-c") {
    // Config file path
    if (i + 1 < argc) {
      Configs::SetPath(argv[++i]);
    }
  } else if (arg == "--output-log") {
    if (i + 1 < argc) {
      cfg.outputLogFile = argv[++i];
    }
  } else if (arg == "--history-file") {
    if (i + 1 < argc) {
      cfg.historyFile = argv[++i];
    }
  } else if (arg == "--eval" || arg == "-E") {
    if (i + 1 < argc) {
        cfg.evalString = argv[++i];
            if (cfg.mode == Mode::DAEMON) cfg.mode = Mode::SCRIPT_ONLY;
            cfg.minimalMode = true;
            cfg.pureStdlib = true;
    }
} else if (arg == "--run" || arg == "run") {
                cfg.mode = Mode::SCRIPT_ONLY;
                cfg.minimalMode = true;
                cfg.pureStdlib = true;
        } else if (arg == "--pure-stdlib") {
            cfg.pureStdlib = true;
        } else if (arg == "--self-hosted") {
            cfg.selfHosted = true;
    } else if (arg == "--test" || arg == "-t") {
      // Test mode - run all .hv files in a directory
        cfg.mode = Mode::TEST;
            cfg.minimalMode = true;
            cfg.pureStdlib = true;
      // Next argument should be the test directory
      if (i + 1 < argc) {
        cfg.testDir = argv[++i];
      }
      // If number on next arg use as timeout for each test
      if (i + 1 < argc) {
        try {
          cfg.testTimeout = std::stoi(argv[i + 1]);
          i++;        
        } catch (const std::exception&) {
          // Not a number, ignore
        }   
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
	} else if (arg == "--emit-llvm") {
		cfg.target = Target::IR;
		cfg.emitLLVM = true;
		cfg.buildOnly = true;
		if (i + 1 < argc && argv[i + 1][0] != '-') {
			cfg.scriptFiles.push_back(argv[++i]);
		}
	} else if (arg == "--emit-asm") {
		cfg.target = Target::ASM;
		cfg.emitAsm = true;
		cfg.buildOnly = true;
		if (i + 1 < argc && argv[i + 1][0] != '-') {
			cfg.scriptFiles.push_back(argv[++i]);
		}
	} else if (arg == "--emit-obj") {
		cfg.target = Target::AOT;
		cfg.emitObj = true;
		cfg.buildOnly = true;
		if (i + 1 < argc && argv[i + 1][0] != '-') {
			cfg.scriptFiles.push_back(argv[++i]);
		}
	} else if (arg == "--arch") {
		if (i + 1 < argc) {
			cfg.arch = argv[++i];
		}
	} else if (arg == "--syntax") {
		if (i + 1 < argc) {
			std::string syntax = argv[++i];
			if (syntax == "intel") {
				cfg.asmSyntax = AsmSyntax::INTEL;
			} else if (syntax == "att") {
				cfg.asmSyntax = AsmSyntax::ATT;
			} else {
				error("Unknown assembly syntax: {}. Supported: intel, att", syntax);
			}
		}
	} else if (arg == "--input" || arg == "-i") {
		if (i + 1 < argc) {
			cfg.inputBackend = argv[++i];
		}
      } else if (arg == "--") {
        // Everything after -- is script arguments (app.args), not flags
        for (int j = i + 1; j < argc; j++) {
          cfg.scriptArgs.push_back(argv[j]);
        }
        break;
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
	// Check for debug flags
	if(Configs::Get().Get<bool>("Debug.ForceMinimal", false)){
		cfg.minimalMode = true;
		debug("Debug.ForceMinimal is set - forcing minimal mode");
	}

	// Resolve input backend: CLI arg > config > auto-detect
	if (cfg.inputBackend.empty()) {
		cfg.inputBackend = Configs::Get().Get<std::string>("Input.Backend", "auto");
	}
	if (cfg.inputBackend == "auto") {
		cfg.inputBackend = ""; // Will be resolved by auto-detection
	}

	// Otherwise use the mode already set (GUI_ONLY, SCRIPT_ONLY, SCRIPT, CLI)

  return cfg;
}

int HavelLauncher::runDaemon(const LaunchConfig &cfg, int argc, char *argv[]) {
  // Load scripts first
  std::string combinedCode;
  std::string combinedNames;
  for (const auto& f : cfg.scriptFiles) {
    std::string content = readScriptFile(f);
    if (!content.empty()) {
      combinedCode += content + "\n";
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
            "error", err.what(), primaryFile,
            err.line, err.column, 1, sourceLine);
        std::cerr << formatted;
      }
      error("Compilation failed with {} error(s)", compiler.errors().size());
      return 1;
    }

    info("Linting successful");
    return 0;
  }

  auto* backend = host::UIManager::instance().backend();

  // Set application metadata BEFORE constructing havel::Havel
  // (D2 precondition: ensureApp() must see the right metadata)
  host::UIBackend::ApplicationMetadata meta;
  meta.argc = &argc;
  meta.argv = argv;
  meta.applicationName = "havel";
  meta.applicationVersion = "1.0";
  meta.organizationName = "havel";
  meta.quitOnLastWindowClosed = false; // Daemon keeps running without windows
  backend->setApplicationMetadata(meta);

  // Restart loop - QApplication is reused across iterations (D4)
  while (true) {
    // Scope block ensures havel_inst destructor runs BEFORE resetPerRunState
    // (D4 mandatory ordering: Havel destructor sees live widget map)
    {
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

      // Wire shutdown callback through UI module (D5)
      havel_inst.setShutdownCallback([] {
        host::UIManager::instance().backend()->quitEventLoop(0);
      });

      int exitCode = backend->runEventLoop();

      // Handle restart exit code
      if (exitCode != 42) {
        return exitCode; // Normal exit
      }
      // havel_inst destructor runs here — sees live UIService widget map
    }

    // D4 step 2: reset per-run state after Havel destruction
    backend->resetPerRunState();
    info("Restart requested - relaunching application");
  }
}

int HavelLauncher::runScript(const LaunchConfig &cfg, int argc, char *argv[]) {
  // Unify .hvc execution path with runScriptOnly.
  std::vector<std::string> hvcFiles;
  std::vector<std::string> hvFiles;
  for (const auto &f : cfg.scriptFiles) {
    if (f.size() >= 4 && f.substr(f.size() - 4) == ".hvc") {
      hvcFiles.push_back(f);
    } else {
      hvFiles.push_back(f);
    }
  }
  if (!hvcFiles.empty() && hvFiles.empty() && cfg.evalString.empty()) {
    return runBytecodeFiles(cfg, hvcFiles);
  }

  std::string combinedCode;
  std::string combinedNames;
  for (const auto& f : cfg.scriptFiles) {
    std::string content = readScriptFile(f);
    if (!content.empty()) {
      combinedCode += content + "\n";
      if (!combinedNames.empty()) combinedNames += " + ";
      combinedNames += f;
    } else {
    error("Cannot open script file: {}", f);
    return 2;
    }
  }
  if (!cfg.evalString.empty()) {
    combinedCode += cfg.evalString + "\n";
    if (!combinedNames.empty()) combinedNames += " + ";
    combinedNames += "<eval>";
  }

  if (combinedCode.empty()) {
    error("No script code provided");
    return 1;
  }

  // Parse once to check for hotkey bindings
  havel::parser::Parser parser{{
    .lexer = cfg.debugLexer,
    .parser = cfg.debugParser,
    .ast = cfg.debugAst
  }};
  std::unique_ptr<havel::ast::Program> program;
  try {
    program = parser.produceAST(combinedCode);
  } catch (const std::exception& e) {}

  // Helper: check for hotkey bindings in AST
  auto checkHotkeys = [](const havel::ast::Program& prog) -> bool {
    for (const auto& stmt : prog.body) {
      if (stmt && stmt->kind == havel::ast::NodeType::HotkeyBinding) return true;
    }
    return false;
  };
  bool hasHotkeys = program && checkHotkeys(*program);

  if (hasHotkeys) {
    // Full mode with UI backend — needs IO, hotkeys, event loop
    if (debugging::debug_io) debug("Hotkeys detected — using full execution mode");

    auto* backend = host::UIManager::instance().backend();
    host::UIBackend::ApplicationMetadata meta;
    meta.applicationName = "havel";
    meta.applicationVersion = "1.0";
    meta.organizationName = "havel";
    meta.quitOnLastWindowClosed = true;
    backend->setApplicationMetadata(meta);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);

    havel::Havel havel_inst(false, combinedNames, false, true, args);
    if (!havel_inst.isInitialized()) {
      error("Failed to initialize havel::Havel");
      return 1;
    }

    auto* bytecodeVM = havel_inst.getBytecodeVM();
    auto* hostBridge = havel_inst.getHostBridge();
    if (!bytecodeVM || !hostBridge) {
      error("Bytecode VM not available");
      return 1;
    }

    auto* hkManager = havel_inst.getHotkeyManagerPtr();
    auto hostAPI = std::shared_ptr<HostAPI>(new HostAPI(
      havel_inst.getIOPtr(), hkManager, Configs::Get(),
      havel_inst.getWindowManagerPtr(),
      nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, nullptr, nullptr, nullptr,
      hkManager ? hkManager->getModeManager().get() : nullptr,
      std::vector<std::string>{}));
    havel::initializeServiceRegistry(hostAPI);

    bytecodeVM->setTimerCheckFunction([hostBridge]() { hostBridge->checkTimers(); });

    try {
      havel::compiler::PipelineOptions options = hostBridge->options();
      options.compile_unit_name = combinedNames;
      options.vm_override = bytecodeVM;
      options.debugBytecode = cfg.debugBytecode;
      havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
    } catch (const std::exception &e) {
      error("Execution error: {}", e.what());
      return 1;
    }

    if (hkManager) {
      hkManager->printHotkeys();
      hkManager->updateAllConditionalHotkeys();
    }

    havel_inst.setShutdownCallback([] {
      host::UIManager::instance().backend()->quitEventLoop(0);
    });

    if (!hkManager || hkManager->getHotkeyList().empty()) {
      info("No hotkeys registered — exiting");
      return 0;
    }

    info("Scripts loaded. Hotkeys registered. Press Ctrl+C to exit.");
    int exitCode = backend->runEventLoop();
    return exitCode;
  }

  // Headless mode — no UI backend, no hotkeys, pure bytecode execution
  if (debugging::debug_io) debug("Running combined scripts (headless): {}", combinedNames);

  try {
        havel::HavelEngine engine({
            .debugBytecode = cfg.debugBytecode,
            .debugLexer = cfg.debugLexer,
            .debugParser = cfg.debugParser,
            .debugAst = cfg.debugAst,
            .stopOnError = cfg.stopOnError,
            .leanMinimalStartup = cfg.minimalMode,
            .pureStdlib = cfg.pureStdlib
        });
    engine.initializeMinimal();
    engine.execute(combinedCode, "__main__", combinedNames);
    engine.shutdown();
    return 0;
  } catch (const std::exception &e) {
    error("Execution error: {}", e.what());
    return 1;
  }
}

int havel::init::HavelLauncher::runBytecodeFiles(const LaunchConfig &cfg,
                                                  const std::vector<std::string> &hvcFiles) {
  // Load and execute .hvc bytecode files directly

  for (const auto& f : hvcFiles) {
    std::ifstream file(f, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      error("Cannot open bytecode file: {}", f);
      return 2;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
      error("Failed to read bytecode file: {}", f);
      return 2;
    }

    havel::compiler::ValueSerializer serializer;
    auto chunk = serializer.deserializeChunk(buffer);
    if (!chunk) {
      error("Failed to deserialize bytecode: {}", f);
      return 2;
    }

    havel::HostContext ctx;
    havel::compiler::VM tempVm;
    ctx.vm = &tempVm;
    auto bridge = havel::compiler::createHostBridge(ctx);
    // Register host functions with VM
    auto *vm = static_cast<havel::compiler::VM *>(ctx.vm);
    const bool coreProfile = (cfg.profile == "core") || cfg.minimalMode;
#ifdef HAVEL_ENABLE_LLVM
    std::unique_ptr<havel::compiler::BytecodeOrcJIT> jit;
    if (cfg.useJIT) {
      jit = std::make_unique<havel::compiler::BytecodeOrcJIT>();
      jit->setDebugMode(cfg.debugJIT);
      jit->setDumpIR(cfg.dumpIR);
      jit->setDumpAsmToFile(cfg.outputAsmToFile);
      jit->setShowWarnings(cfg.aotWarnings);
      vm->setHotFunctionCallback([jit_ptr = jit.get()](const havel::compiler::BytecodeFunction &func) {
        if (!jit_ptr->isCompiled(func.name)) {
          jit_ptr->compileFunction(func);
        }
      });
      vm->setJITCompiler(jit.get());
    }
#endif
    bridge->install(
        coreProfile ? havel::compiler::HostBridge::InstallProfile::Core
                    : havel::compiler::HostBridge::InstallProfile::Full,
        !coreProfile);
    if (coreProfile) {
        if (cfg.pureStdlib) {
            havel::registerPureStdLib(*vm);
        } else {
            havel::registerCoreStdLib(*vm);
        }
    } else {
        havel::registerStdLibWithVM(*bridge);
    }

    for (const auto& [name, fn] : bridge->options().host_functions) {
      vm->registerHostFunction(name, fn);
    }

    info("Loaded bytecode file: {} ({} function(s))", f, chunk->getFunctionCount());
    if (cfg.debugBytecode) {
      info("Bytecode loaded successfully: {}", f);
    }

// Set main_chunk_ so CLOSURE correctly decides whether to snapshot globals
auto chunkPtr = std::make_shared<compiler::BytecodeChunk>(std::move(*chunk));
vm->setMainChunkShared(chunkPtr);

// Set script directory for module resolution (so `use` finds .hvc/.hv modules)
vm->setCurrentScriptDir(std::filesystem::path(f).parent_path().string());

    // Populate app.args with script arguments (after --)
    if (!cfg.scriptArgs.empty()) {
        auto arrRef = vm->createHostArray();
        for (const auto& arg : cfg.scriptArgs) {
            auto strRef = vm->createRuntimeString(arg);
            vm->pushHostArrayValue(arrRef, havel::compiler::Value::makeStringId(strRef.id));
        }
        vm->setAppArgs(arrRef.id);
    }

    // Execute __main__ function from this chunk directly to keep string/constant tables valid
    try {
        auto result = vm->execute(*chunkPtr, "__main__");
      (void)result;
    } catch (const std::exception &e) {
      error("Bytecode error in {}: {}", f, e.what());
      bridge->shutdown();
      return 1;
    } catch (...) {
      error("Unknown bytecode error in {}", f);
      bridge->shutdown();
      return 1;
    }

    bridge->shutdown();
  }

  return 0;
}

// Helper: Check if AST contains any hotkey bindings
static bool hasHotkeyBindings(const havel::ast::Program& program) {
  for (const auto& stmt : program.body) {
    if (stmt && stmt->kind == havel::ast::NodeType::HotkeyBinding) {
      return true;
    }
  }
  return false;
}

int havel::init::HavelLauncher::runScriptOnly(const LaunchConfig &cfg, int argc,
                                              char *argv[]) {
  // Check if we have .hvc bytecode files
  std::vector<std::string> hvcFiles;
  std::vector<std::string> hvFiles;
  for (const auto& f : cfg.scriptFiles) {
    if (f.size() >= 4 && f.substr(f.size() - 4) == ".hvc") {
      hvcFiles.push_back(f);
    } else {
      hvFiles.push_back(f);
    }
  }

  // Pure bytecode execution: load and run .hvc files directly
  if (!hvcFiles.empty() && hvFiles.empty()) {
    return runBytecodeFiles(cfg, hvcFiles);
  }

  // Pure script execution without IO, hotkeys, display, or GUI
  std::string combinedCode;
  std::string combinedNames;
  for (const auto& f : cfg.scriptFiles) {
    std::string content = readScriptFile(f);
    if (!content.empty()) {
      combinedCode += content + "\n";
      if (!combinedNames.empty()) combinedNames += " + ";
      combinedNames += f;
    } else {
      error("Cannot open script file: {}", f);
      return 2;
    }
  }
  if (!cfg.evalString.empty()) {
    combinedCode += cfg.evalString + "\n";
    if (!combinedNames.empty()) combinedNames += " + ";
    combinedNames += "<eval>";
  }

  // Read from stdin if piped and no other source
  if (combinedCode.empty() && !isatty(STDIN_FILENO)) {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    combinedCode = ss.str();
    combinedNames = "<stdin>";
  }

  if (combinedCode.empty()) return 0;

  // Parse script to check for hotkey bindings
  havel::parser::Parser parser{{
    .lexer = cfg.debugLexer,
    .parser = cfg.debugParser,
    .ast = cfg.debugAst
  }};
  std::unique_ptr<havel::ast::Program> program;
  try {
    program = parser.produceAST(combinedCode);
  } catch (const std::exception& e) {
    // Parser aborted
  }

    if (parser.hasErrors() || !program) {
        for (const auto& err : parser.getErrors()) {
            std::cerr << "Parse error: " << err.message << " at line " << err.line << " col " << err.column << std::endl;
        }
        error("Failed to parse script");
        return 1;
    }

  // If hotkeys found, switch to SCRIPT mode (with full IO/event loop)
  if (hasHotkeyBindings(*program)) {
    if (debugging::debug_io) debug("Hotkey bindings detected - starting full IO/event loop");
    LaunchConfig fullCfg = cfg;
    fullCfg.mode = Mode::SCRIPT;
    return runScript(fullCfg, argc, argv);
  }

  // LINT-ONLY MODE: type-check only, no execution
  if (cfg.lintOnly) {
    std::string primaryFile = combinedNames.empty() ? "input" : combinedNames;
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
              "error", err.what(), primaryFile,
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

	if (debugging::debug_io) debug("Running Havel scripts (pure mode): {}", combinedNames);

	// Set up signal handling ...
	struct sigaction sa;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = [](int sig) { std::exit(0); };
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);

	// Debug.AutoExit support for pure script mode
	if (Configs::Get().Get<bool>("Debug.AutoExit", false)) {
		int delay = Configs::Get().Get<int>("Debug.AutoExitDelay", 15);
		std::thread([delay]() {
			std::this_thread::sleep_for(std::chrono::seconds(delay));
			if (!Configs::Get().Get<bool>("Debug.AutoExit", false)) {
				return; // AutoExit was disabled during the wait
			}
			if (debugging::debug_io) debug("AutoExit enabled - exiting after {} seconds", delay);
			std::exit(0);
		}).detach();
	}

    try {
        havel::HavelEngine engine({
            .debugBytecode = cfg.debugBytecode,
            .debugLexer = cfg.debugLexer,
            .debugParser = cfg.debugParser,
            .debugAst = cfg.debugAst,
            .stopOnError = cfg.stopOnError,
            .leanMinimalStartup = cfg.minimalMode,
            .pureStdlib = cfg.pureStdlib
        });
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.initializeMinimal();

        if (!cfg.scriptArgs.empty()) {
            auto& vm = *engine.vm();
            auto arrRef = vm.createHostArray();
            for (const auto& arg : cfg.scriptArgs) {
                auto strRef = vm.createRuntimeString(arg);
                vm.pushHostArrayValue(arrRef, havel::compiler::Value::makeStringId(strRef.id));
            }
            vm.setAppArgs(arrRef.id);
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        engine.execute(combinedCode, "__main__", combinedNames);
        auto t2 = std::chrono::high_resolution_clock::now();
        engine.shutdown();
        auto t3 = std::chrono::high_resolution_clock::now();

        if (cfg.debugMode) {
            double init_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double exec_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
            double shut_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
            double total_ms = std::chrono::duration<double, std::milli>(t3 - t0).count();
            info("benchmark: init={:.1f}ms exec={:.1f}ms shutdown={:.1f}ms total={:.1f}ms",
                 init_ms, exec_ms, shut_ms, total_ms);
        }
        return 0;
	} catch (const std::exception &e) {
		error("Bytecode error: {}", e.what());
		return 1;
	}
}

int havel::init::HavelLauncher::runSelfHosted(const LaunchConfig &cfg) {
    // Find launcher.hv relative to the binary
    // Binary lives in build-debug/ or build-release/, modules are at repo root
    char selfBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
    std::string binDir;
    if (len > 0) {
        selfBuf[len] = '\0';
        std::filesystem::path p(selfBuf);
        binDir = p.parent_path().string();
    }

    std::vector<std::string> searchPaths = {
        binDir + "/../modules/lang/launcher.hv",
        binDir + "/../../modules/lang/launcher.hv",
        "modules/lang/launcher.hv",
        "../modules/lang/launcher.hv",
        "../../modules/lang/launcher.hv",
    };

    std::string launcherPath;
    for (const auto& candidate : searchPaths) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            launcherPath = candidate;
            break;
        }
    }

    if (launcherPath.empty()) {
        error("Cannot find modules/lang/launcher.hv (searched relative to binary and cwd)");
        return 1;
    }

    // Resolve to absolute path
    std::error_code ec;
    auto absPath = std::filesystem::canonical(launcherPath, ec);
    if (!ec) launcherPath = absPath.string();

    std::string launcherCode = readScriptFile(launcherPath);
    if (launcherCode.empty()) {
        error("Cannot read launcher.hv at {}", launcherPath);
        return 1;
    }

    // Build app.args: --self-hosted <script files> [-- script args]
    // launcher.hv's main() reads app.args and dispatches
    std::vector<std::string> appArgList;
    appArgList.push_back("--self-hosted");
    for (const auto& f : cfg.scriptFiles) {
        appArgList.push_back(f);
    }
    if (cfg.lintOnly) appArgList.push_back("--lint");
    for (const auto& a : cfg.scriptArgs) {
        appArgList.push_back(a);
    }

    if (debugging::debug_io) debug("Self-hosted mode: launcher={}, args={}", launcherPath, appArgList.size());

    // Set up signal handling
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = [](int sig) { std::exit(0); };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    try {
        havel::HavelEngine engine({
            .debugBytecode = cfg.debugBytecode,
            .debugLexer = cfg.debugLexer,
            .debugParser = cfg.debugParser,
            .debugAst = cfg.debugAst,
            .stopOnError = cfg.stopOnError,
            .leanMinimalStartup = true,
            .pureStdlib = true
        });
        engine.initializeMinimal();

        // Populate app.args for launcher.hv
        auto& vm = *engine.vm();
        auto arrRef = vm.createHostArray();
        for (const auto& arg : appArgList) {
            auto strRef = vm.createRuntimeString(arg);
            vm.pushHostArrayValue(arrRef, havel::compiler::Value::makeStringId(strRef.id));
        }
        vm.setAppArgs(arrRef.id);

        engine.execute(launcherCode, "__main__", launcherPath);
        engine.shutdown();
        return 0;
    } catch (const std::exception &e) {
        error("Self-hosted error: {}", e.what());
        return 1;
    }
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
        std::string combinedNames;
        for (const auto& f : cfg.scriptFiles) {
            std::string content = readScriptFile(f);
            if (!content.empty()) {
                combinedCode += content + "\n";
                if (!combinedNames.empty()) combinedNames += " + ";
                combinedNames += f;
            }
        }

        havel::HavelEngine engine({
            .debugBytecode = cfg.debugBytecode,
            .debugLexer = cfg.debugLexer,
            .debugParser = cfg.debugParser,
            .debugAst = cfg.debugAst,
            .stopOnError = cfg.stopOnError
        });
        engine.initializeMinimal();

        info("Executing script code...");
        try {
            engine.execute(combinedCode, "__main__", combinedNames.empty() ? "script" : combinedNames);
        } catch (const std::exception &e) {
            error("Script execution failed: {}", e.what());
            return 1;
        }

        havel::repl::REPLConfig replConfig;
        replConfig.debugMode = cfg.debugMode;
        replConfig.stopOnError = cfg.stopOnError;
        replConfig.debugBytecode = cfg.debugBytecode;
        replConfig.debugLexer = cfg.debugLexer;
        replConfig.debugParser = cfg.debugParser;
        replConfig.debugAst = cfg.debugAst;
        replConfig.outputLogFile = cfg.outputLogFile;
        replConfig.historyFile = cfg.historyFile;
        havel::repl::REPL repl(replConfig);
        repl.attach(engine.vm(), engine.hostBridge(), collectKnownGlobals(engine.vm()));

        return repl.run();
    } else {
      info("Running scripts and starting REPL with full features...");
      std::string combinedCode;
      std::string combinedNames;
      for (const auto& f : cfg.scriptFiles) {
        std::string content = readScriptFile(f);
        if (!content.empty()) {
          combinedCode += content + "\n";
          if (!combinedNames.empty()) combinedNames += " + ";
          combinedNames += f;
        }
      }

      auto* replBackend = host::UIManager::instance().backend();
      host::UIBackend::ApplicationMetadata replMeta;
      replMeta.applicationName = "havel";
      replMeta.organizationName = "havel";
      replMeta.quitOnLastWindowClosed = false;
      replBackend->setApplicationMetadata(replMeta);

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
        replConfig.debugBytecode = cfg.debugBytecode;
        replConfig.debugLexer = cfg.debugLexer;
        replConfig.debugParser = cfg.debugParser;
        replConfig.debugAst = cfg.debugAst;
        replConfig.outputLogFile = cfg.outputLogFile;
        replConfig.historyFile = cfg.historyFile;

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

      // Attach REPL to the existing VM from the Havel instance
      // (instead of initialize() which creates a new VM)
      repl.attach(bytecodeVM, havel_inst.getHostBridge(), collectKnownGlobals(bytecodeVM));

      // Enter REPL
      return repl.run();
    }
  } catch (const std::exception &e) {
    error("Script+REPL error: {}", e.what());
    return 1;
  }
}

void havel::init::HavelLauncher::showHelp() {
std::cout << " --debug-bytecode, -dbc Enable bytecode debugging\n";
    std::cout << " --debug-gc, -dgc       Enable GC debugging\n";
std::cout << " --debug-engine, -de Enable engine debugging\n";
std::cout << " --debug-io, -dio Enable IO debugging\n";
std::cout << " --debug-hotkeys, -dhk Enable hotkey debugging\n";
  std::cout << "  --diff              Compare bytecode with previous run "
               "(implies -dbc)\n";
  std::cout << " --error, -e Stop on first error/warning\n";
  std::cout << " --eval, -E CODE Run inline Havel code (minimal mode)\n";
  std::cout << " --minimal, -m Minimal mode (no IO/hotkeys/GUI)\n";
  std::cout << "  --repl, -r          Start interactive REPL (full features)\n";
  std::cout << "  --full-repl, -fr    Start REPL with ALL features (hotkeys, "
               "GUI, etc.)\n";
  std::cout << "  --gui               GUI-only mode (no hotkeys)\n";
  std::cout
        << " --run Run script in minimal mode (auto-enables -m)\n";
    std::cout
        << " --self-hosted Run via pure Havel pipeline (launcher.hv)\n";
  std::cout
      << "  --test, -t          Run all .hv scripts in a directory\n";
  std::cout << "  --lint              Check syntax and compilation errors\n";
	std::cout << " --build Compile to .hvc bytecode file\n";
	std::cout << " --output, -o PATH Set output path for --build\n";
	std::cout << " --emit-llvm FILE Output LLVM IR (.ll) for AOT compilation\n";
	std::cout << " --emit-asm FILE Output native assembly (.s) for AOT\n";
	std::cout << " --emit-obj FILE Output object file (.o) for AOT linking\n";
	std::cout << " --target <mode> Target backend: interpret|jit|aot|asm|ir|wasm|elf|bin\n";
	std::cout << " --os <name> AOT/JIT target OS: native|linux|windows|macos|wasm\n";
	std::cout << " --aot-warnings Enable AOT/JIT warning messages\n";
	std::cout << " --no-aot-warnings Disable AOT/JIT warning messages\n";
std::cout << " --link-lib <lib> Add linker library/flag (repeatable)\n";
	std::cout << " --profile <name> AOT link profile: full|core\n";
	std::cout << " --full-aot Emit llvm+asm+obj+shared+executable in one run\n";
	std::cout << " --arch <triple> Set target architecture (e.g. x86_64-pc-linux-gnu)\n";
	std::cout << " --syntax <type> Assembly syntax: att|intel\n";
	std::cout << " --no-jit Disable JIT compilation\n";
	std::cout << " --debug-jit, -djt Print LLVM IR and Assembly to console\n";
	std::cout << " -S Output compiled IR and Assembly to files\n";
	std::cout << " --input, -i TYPE Set input backend (evdev, x11, wayland, auto)\n";
	std::cout << " --help, -h Show this help\n";

  std::cout << "\nIf a .hv script file is provided, it will be executed.\n";
  std::cout << "If no arguments are provided, starts interactive REPL with full features.\n";
  std::cout << "\nModes:\n";
  std::cout << "  havel                   - Start interactive REPL (full features)\n";
  std::cout << "  havel script.hv         - Run script with FULL features\n";
  std::cout << "  havel --repl script.hv    - Run script then REPL (FULL features)\n";
  std::cout << "  havel --repl              - Start REPL with FULL features\n";
  std::cout << "  havel --full-repl         - Start REPL with ALL features\n";
    std::cout << " havel --run script.hv - Run script in MINIMAL mode\n";
    std::cout << " havel --run --self-hosted script.hv - Run via pure Havel pipeline\n";
  std::cout << "  havel --test dir/         - Run all .hv files in directory\n";
  std::cout << "  havel --lint script.hv    - Check for errors without running\n";
  std::cout << "  havel --build script.hv   - Compile to bytecode (.hvc)\n";
  std::cout << "  havel --build script.hv -o out.hvc  - Compile to specific file\n";
  std::cout << "  havel --target interpret script.hv   - Run on bytecode interpreter\n";
  std::cout << "  havel --target jit script.hv         - Run with LLVM JIT enabled\n";
  std::cout << "  havel --target ir script.hv          - Emit LLVM IR (.ll)\n";
  std::cout << "  havel --target asm script.hv         - Emit native assembly (.s)\n";
  std::cout << "  havel --target aot script.hv         - Emit object (.o) + shared binary (.so)\n";
  std::cout << "  havel --full-aot --os windows script.hv - Emit full AOT set for Windows\n";
  std::cout << "  havel --target wasm script.hv        - Emit WebAssembly binary (.wasm)\n";
  std::cout << "  havel --target elf script.hv         - Emit standalone ELF executable\n";
  std::cout << " havel --minimal script.hv - Run script in MINIMAL mode\n";
  std::cout << " havel --eval 'print(1+2)' - Run inline code\n";
  std::cout << " havel --repl --minimal - Start REPL in MINIMAL mode\n";
  std::cout << "\nFull mode (default):\n";
  std::cout << "  All features enabled: hotkeys, GUI, display, IO, etc.\n";
  std::cout << "  Use for normal automation and hotkey scripts.\n";
  std::cout << "\nMinimal mode (--minimal or --run):\n";
  std::cout << "  Executes scripts without IO, hotkeys, display, or GUI.\n";
  std::cout
      << "  Useful for testing scripts that auto-exit or don't need input.\n";
  std::cout << "  Example: havel --run scripts/test_types.hv\n";
  std::cout << "\nREPL options:\n";
  std::cout << "  --output-log PATH   Save REPL session output to file\n";
  std::cout << "  --history-file PATH Read/write REPL history from/to file (default: ~/.havel_history)\n";
  std::cout << "\nDebugging flags:\n";
    std::cout << " --debug-bytecode Print bytecode to console\n";
    std::cout << " --debug-gc       Print GC collection info\n";
std::cout << " --debug-engine Print engine scheduling info\n";
std::cout << " --debug-io Print IO subsystem info\n";
std::cout << " --debug-hotkeys Print hotkey registration info\n";
    std::cout << " --diff Compare bytecode with previous run\n";
  std::cout << "  Snapshots saved to: /tmp/havel-bytecode/\n";
}

// REPL implementation using bytecode VM
int havel::init::HavelLauncher::runRepl(const LaunchConfig &cfg) {
  try {
    if (cfg.minimalMode) {
        info("Starting Havel REPL in minimal mode (no IO/hotkeys)...");

        havel::HavelEngine engine({
            .debugBytecode = cfg.debugBytecode,
            .debugLexer = cfg.debugLexer,
            .debugParser = cfg.debugParser,
            .debugAst = cfg.debugAst,
            .stopOnError = cfg.stopOnError
        });
        engine.initializeMinimal();

        havel::repl::REPLConfig replConfig;
        replConfig.debugMode = cfg.debugMode;
        replConfig.stopOnError = cfg.stopOnError;
        replConfig.debugBytecode = cfg.debugBytecode;
        replConfig.debugLexer = cfg.debugLexer;
        replConfig.debugParser = cfg.debugParser;
        replConfig.debugAst = cfg.debugAst;
        replConfig.outputLogFile = cfg.outputLogFile;
        replConfig.historyFile = cfg.historyFile;

        havel::repl::REPL repl(replConfig);
        repl.attach(engine.vm(), engine.hostBridge(), collectKnownGlobals(engine.vm()));

        return repl.run();
    } else {
      info("Starting Havel REPL with full features (hotkeys, GUI)...");
      
      // Debug: Show mode information
        havel::debug("Running in REPL mode (full):");
        havel::debug(" - GUI: enabled");
        havel::debug(" - IO/Hotkeys: enabled");

      // Full mode - initialize Qt and havel::Havel
      auto* replBackend = host::UIManager::instance().backend();
      host::UIBackend::ApplicationMetadata replMeta;
      replMeta.applicationName = "havel";
      replMeta.organizationName = "havel";
      replMeta.quitOnLastWindowClosed = false;
      replBackend->setApplicationMetadata(replMeta);

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
        replConfig.debugBytecode = cfg.debugBytecode;
        replConfig.debugLexer = cfg.debugLexer;
        replConfig.debugParser = cfg.debugParser;
        replConfig.debugAst = cfg.debugAst;
        replConfig.outputLogFile = cfg.outputLogFile;
        replConfig.historyFile = cfg.historyFile;

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
      repl.attach(bytecodeVM, hostBridge, collectKnownGlobals(bytecodeVM));

      // Run REPL
      return repl.run();
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

    // Run test as subprocess 
    std::string cmd = std::format("timeout {} {} --run {}", cfg.testTimeout, selfPath, testFile);
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
    std::string content = readScriptFile(f);
    if (!content.empty()) {
      combinedCode += content + "\n";
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
          "error", err.what(), primaryFile,
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

info("Compilation successful, {} functions", chunk->getFunctionCount());

#ifdef HAVEL_ENABLE_LLVM
// Handle AOT LLVM output
if (cfg.emitLLVM || cfg.emitAsm || cfg.emitObj || cfg.emitWasm || cfg.emitBinary || cfg.emitElf) {
    // Import LLVM JIT for translation
    havel::compiler::BytecodeOrcJIT jit;
    jit.setShowWarnings(cfg.aotWarnings);
    jit.setLinkedLibraries(cfg.linkLibs);
    if (cfg.emitLLVM || cfg.debugJIT) {
        jit.setDumpIR(true);
    }
    const std::string normalizedOS = normalizeTargetOS(cfg.targetOS);
    if (normalizedOS == "linux") {
        jit.setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Linux);
    } else if (normalizedOS == "windows") {
        jit.setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Windows);
    } else if (normalizedOS == "macos") {
        jit.setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::MacOS);
    } else if (normalizedOS == "wasm") {
        jit.setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Wasm);
    } else {
        jit.setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Native);
    }

    // Generate LLVM IR for each function
    llvm::LLVMContext ctx;
    auto module = std::make_unique<llvm::Module>(primaryFile + "_module", ctx);

for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
    const auto* func = chunk->getFunction(i);
    if (func && !havel::compiler::BytecodeOrcJIT::hasUnsupportedOpcodes(*func)) {
        jit.translate(*func, *module);
    } else if (func && cfg.aotWarnings) {
        warn("AOT: skipping function '{}' — contains async/concurrency opcodes not supported in AOT",
             func->name);
    }
}

    // Verify module
    if (llvm::verifyModule(*module, &llvm::errs())) {
        std::string failPath = "/tmp/havel_aot_verify_fail.ll";
        std::error_code ec;
        llvm::raw_fd_ostream failOut(failPath, ec, llvm::sys::fs::OF_None);
        if (!ec) {
            module->print(failOut, nullptr);
            error("LLVM IR verification failed (dumped to {})", failPath);
        } else {
            error("LLVM IR verification failed");
        }
        return 1;
    }

    // Determine output base path
    std::string aotOutput = cfg.outputPath.empty() ? primaryFile : cfg.outputPath;
    if (aotOutput.size() >= 3 && aotOutput.rfind(".hv") == aotOutput.size() - 3) {
        aotOutput = aotOutput.substr(0, aotOutput.size() - 3);
    }
    if (aotOutput.empty()) {
        aotOutput = "output";
    }

    if (cfg.emitLLVM) {
        std::string llPath = aotOutput + ".ll";
        std::error_code ec;
        llvm::raw_fd_ostream out(llPath, ec, llvm::sys::fs::OF_None);
        if (ec) {
            error("Cannot open output file: {}", llPath);
            return 1;
        }
        module->print(out, nullptr);
        info("LLVM IR written to: {}", llPath);
    }

    if (cfg.emitAsm || cfg.emitObj || cfg.emitBinary || cfg.emitElf) {
        // Initialize target for native code gen
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        std::string targetTripleStr = cfg.arch.empty()
            ? mapTargetTripleForOS(cfg.targetOS, llvm::sys::getDefaultTargetTriple())
            : cfg.arch;
        llvm::Triple targetTriple(targetTripleStr);
        module->setTargetTriple(targetTriple);

        std::string err;
        auto target = llvm::TargetRegistry::lookupTarget(targetTripleStr, err);
        if (!target) {
            error("Cannot find target: {}", err);
            return 1;
        }
        info("AOT Target: {} ({})", target->getName(), targetTripleStr);

        llvm::TargetOptions opt;
        if (cfg.asmSyntax == AsmSyntax::INTEL) {
            opt.MCOptions.OutputAsmVariant = 1;
        }

        auto targetMachine = target->createTargetMachine(
            targetTriple, llvm::sys::getHostCPUName(), "", opt, llvm::Reloc::PIC_);

        module->setDataLayout(targetMachine->createDataLayout());

        std::string nativeObjPath;

        if (cfg.emitAsm) {
            std::string asmPath = aotOutput + ".s";
            std::error_code ec;
            llvm::raw_fd_ostream out(asmPath, ec, llvm::sys::fs::OF_None);
            if (ec) {
                error("Cannot open output file: {}", asmPath);
                return 1;
            }
            llvm::legacy::PassManager pm;
            if (targetMachine->addPassesToEmitFile(pm, out, nullptr,
                llvm::CodeGenFileType::AssemblyFile)) {
                error("Target '{}' cannot emit assembly", targetTripleStr);
                return 1;
            }
            pm.run(*module);
            info("Assembly written to: {}", asmPath);
        }

        if (cfg.emitObj || cfg.emitBinary || cfg.emitElf) {
            nativeObjPath = aotOutput + ".o";
            std::error_code ec;
            llvm::raw_fd_ostream out(nativeObjPath, ec, llvm::sys::fs::OF_None);
            if (ec) {
                error("Cannot open output file: {}", nativeObjPath);
                return 1;
            }
            llvm::legacy::PassManager pm;
            if (targetMachine->addPassesToEmitFile(pm, out, nullptr,
                llvm::CodeGenFileType::ObjectFile)) {
                error("Target '{}' cannot emit object files", targetTripleStr);
                return 1;
            }
            pm.run(*module);
            info("Object file written to: {}", nativeObjPath);
        }

        const bool coreProfile = (cfg.profile == "core");

        if (cfg.emitBinary) {
            const std::string shExt = sharedLibraryExtensionForOS(cfg.targetOS);
            std::string soPath = aotOutput + shExt;
            std::string linkCmd;
            if (normalizeTargetOS(cfg.targetOS) == "windows") {
                linkCmd = "clang++ -shared \"" + nativeObjPath + "\" -o \"" + soPath + "\"";
            } else if (normalizeTargetOS(cfg.targetOS) == "macos") {
                linkCmd = "clang++ -dynamiclib \"" + nativeObjPath + "\" -o \"" + soPath + "\"";
            } else {
                linkCmd = "clang++ -shared -fPIC \"" + nativeObjPath + "\" -o \"" + soPath + "\"";
            }
            if (!coreProfile) {
                appendLinkLibraries(linkCmd, jit.linkedLibraries());
                if (jit.linkedLibraries().empty()) {
                    appendDefaultLlvmLinkLibraries(linkCmd);
                }
            }
            int linkRc = std::system(linkCmd.c_str());
            if (linkRc != 0) {
                error("Failed to link native shared binary with command: {}", linkCmd);
                return 1;
            }
            info("Native shared binary written to: {}", soPath);
        }

        if (cfg.emitElf) {
            const bool targetWindows = normalizeTargetOS(cfg.targetOS) == "windows";
            std::string binPath = aotOutput + (targetWindows ? ".exe" : "");
            std::string stubPath = aotOutput + "_stub.cpp";
            {
                std::ofstream stub(stubPath);
                const std::string initSymbol = coreProfile
                    ? "havel_vm_init_standalone_core"
                    : "havel_vm_init_standalone";
                stub << "#include <cstdint>\n"
                     << "extern \"C\" uint64_t __main__(void*, uint64_t*, uint32_t);\n"
                     << "extern \"C\" void* " << initSymbol << "(const char**, uint32_t);\n"
                     << "int main() {\n"
                     << "    const char* strings[] = {\n";
                const auto& chunkStrings = chunk->getAllStrings();
                for (size_t i = 0; i < chunkStrings.size(); ++i) {
                    const std::string& s = chunkStrings[i];
                    // Escape string
                    std::string escaped;
                    for (char c : s) {
                        if (c == '"') escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else if (c == '\n') escaped += "\\n";
                        else escaped += c;
                    }
                    stub << "        \"" << escaped << "\",\n";
                }
                stub << "    };\n"
                     << "    void* vm = " << initSymbol << "(strings, " << chunkStrings.size() << ");\n"
                     << "    uint64_t dummy_args[1024];\n"
                     << "    for(int i=0; i<1024; ++i) dummy_args[i] = 0x7ffb000000000000ULL;\n"
                     << "    __main__(vm, dummy_args, 0);\n"
                     << "    return 0;\n"
                     << "}\n";
            }

            std::string linkCmd;
            if (targetWindows) {
                linkCmd = "clang++ \"" + stubPath + "\" \"" + nativeObjPath + "\" -o \"" + binPath + "\"";
            } else {
                linkCmd = "clang++ -flto -fuse-ld=lld \"" + stubPath + "\" \"" + nativeObjPath + "\" -o \"" + binPath + "\"";
                std::string exePath = Env::executable();
                if (!exePath.empty()) {
                    std::string libDir = std::filesystem::path(exePath).parent_path().string();
                    linkCmd += " -L\"" + libDir + "\"";
                }
                if (coreProfile) {
                    linkCmd += " -lhavel_aot_core_shim -lhavel_lang -lhavel_core";
                } else {
                    linkCmd += " -lhavel_lang -lhavel_core -lhavel_modules -lhavel_gui";
                }
            }
            appendLinkLibraries(linkCmd, jit.linkedLibraries());
            if (jit.linkedLibraries().empty()) {
                appendDefaultLlvmLinkLibraries(linkCmd);
            }
            int linkRc = std::system(linkCmd.c_str());
            if (linkRc != 0) {
                error("Failed to link native AOT executable with command: {}", linkCmd);
                std::filesystem::remove(stubPath);
                return 1;
            }
            info("Native AOT executable written to: {}", binPath);
            std::filesystem::remove(stubPath);
        }
    }

    if (cfg.emitWasm) {
        std::string targetTripleStr = "wasm32-unknown-unknown";
        llvm::Triple targetTriple(targetTripleStr);
        module->setTargetTriple(targetTriple);

        std::string err;
        auto target = llvm::TargetRegistry::lookupTarget(targetTripleStr, err);
        if (!target) {
            error("Cannot find WebAssembly target: {}", err);
            return 1;
        }

        llvm::TargetOptions opt;
        auto targetMachine = target->createTargetMachine(
            targetTriple, "generic", "", opt, llvm::Reloc::PIC_);

        module->setDataLayout(targetMachine->createDataLayout());

        std::string wasmPath = aotOutput + ".wasm";
        std::error_code ec;
        llvm::raw_fd_ostream out(wasmPath, ec, llvm::sys::fs::OF_None);
        if (ec) {
            error("Cannot open output file: {}", wasmPath);
            return 1;
        }
        llvm::legacy::PassManager pm;
        if (targetMachine->addPassesToEmitFile(pm, out, nullptr,
            llvm::CodeGenFileType::ObjectFile)) {
            error("Target '{}' cannot emit WebAssembly object", targetTripleStr);
            return 1;
        }
        pm.run(*module);
        info("WebAssembly binary written to: {}", wasmPath);
    }

    return 0;
}
#else
if (cfg.emitLLVM || cfg.emitAsm || cfg.emitObj || cfg.emitWasm || cfg.emitBinary) {
    error("AOT compilation requires LLVM support. Rebuild with ENABLE_LLVM=ON");
    return 1;
}
#endif

// Serialize and write bytecode
  havel::compiler::ValueSerializer serializer;
  auto data = serializer.serializeChunk(*chunk);

  info("Serialization complete, {} bytes", data.size());
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
