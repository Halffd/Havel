#include "HavelLauncher.hpp"
#include "Havel.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "core/io/InputBackend.hpp"
#include "core/init/Havel.hpp"
#include "core/util/Env.hpp"
#include "havel-lang/common/Debug.hpp"
#include "havel-lang/compiler/BytecodeOrcJIT.h"
#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/compiler/core/BootstrapByteCompiler.hpp"
#include "havel-lang/compiler/core/ModuleGlobals.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include "lexer/BootstrapLexer.hpp"
#include "havel-lang/parser/BootstrapParser.h"
#include "havel-lang/runtime/HavelEngine.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"
#include "havel-lang/tools/REPL.hpp"
#include "havel-lang/utils/ErrorPrinter.hpp"
#include "modules/HostModules.hpp"
#include "utils/DebugFlags.hpp"
#include "utils/ExitHandler.hpp"
#include "utils/Logger.hpp"
#include "core/BrightnessManager.hpp"
#include <iostream>

#ifdef HAVEL_ENABLE_LLVM
// LLVM headers for AOT
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#endif

#include "host/ui/UIManager.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>

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
  if (os == "win")
    return "windows";
  if (os == "mac" || os == "darwin")
    return "macos";
  return os;
}

static std::string mapTargetTripleForOS(const std::string &requestedOS,
                                        const std::string &fallbackTriple) {
  std::string os = normalizeTargetOS(requestedOS);
  if (os.empty() || os == "native")
    return fallbackTriple;
  llvm::Triple hostTriple(fallbackTriple);
  std::string arch = hostTriple.getArchName().str();
  if (arch.empty())
    arch = "x86_64";
  if (os == "linux")
    return arch + "-pc-linux-gnu";
  if (os == "windows")
    return arch + "-pc-windows-msvc";
  if (os == "macos")
    return arch + "-apple-darwin";
  if (os == "wasm")
    return "wasm32-unknown-unknown";
  return fallbackTriple;
}

static std::string sharedLibraryExtensionForOS(const std::string &requestedOS) {
  const std::string os = normalizeTargetOS(requestedOS);
  if (os == "windows")
    return ".dll";
  if (os == "macos")
    return ".dylib";
  return ".so";
}

static void appendLinkLibraries(std::string &linkCmd,
                                const std::vector<std::string> &libs) {
  for (const auto &lib : libs) {
    if (lib.empty())
      continue;
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

// Append native (system) link flags baked in at CMake time from
// COMMON_LIBS. Required when linking the AOT executable: havel_core
// archive references X11/Wayland/DBus/PulseAudio/etc. symbols, so the
// standalone clang++ link command must supply the matching system
// libraries or fail with undefined symbols (XNextEvent, etc.).
//
// Also append -L<exeDir> so the build directory's own archives
// (libwayland-protos.a, etc.) can be found by the standalone clang++
// invocation. Without this the SO link using system /usr/bin/ld fails
// to resolve `-lwayland-protos` even though the executable link
// (which uses lld with explicit -Lbuild-release) succeeds.
static void appendDefaultNativeLinkLibraries(std::string &linkCmd) {
  // Add havel build dir to library search path.
  std::string exePath = Env::executable();
  if (!exePath.empty()) {
    std::string libDir =
        std::filesystem::path(exePath).parent_path().string();
    linkCmd += " -L\"";
    linkCmd += libDir;
    linkCmd += "\"";
  }
#ifdef HAVEL_DEFAULT_NATIVE_LINK_FLAGS
  constexpr const char *kDefaultNativeLinkFlags =
      HAVEL_DEFAULT_NATIVE_LINK_FLAGS;
  if (kDefaultNativeLinkFlags[0] != '\0') {
    linkCmd += " ";
    linkCmd += kDefaultNativeLinkFlags;
  }
#else
  (void)linkCmd;
#endif

  // AOT standalone executables are linked as static archives.  Under
  // --as-needed the linker only pulls members that resolve symbols
  // currently referenced by the link unit.  The wayland protocol
  // interface objects (zxdg_output_v1_interface, wl_registry_interface,
  // ...) live in wayland-protos but no symbol in the LLVM-generated AOT
  // main references them directly, so lld drops that archive member.
  // havel_core transitively requires those symbols on some platforms.
  // Force the entire archive in for the native-AOT link.
  linkCmd +=
      " -Wl,--whole-archive -lwayland-protos -Wl,--no-whole-archive";
}
#endif

// ─── Shared Helpers ──────────────────────────────────────────────

static std::optional<std::pair<std::string, std::string>>
loadScriptFiles(const std::vector<std::string> &files) {
  std::string code;
  std::string names;
  for (const auto &f : files) {
    std::string content = readScriptFile(f);
    if (content.empty()) {
      error("Failed to read script file: {}", f);
      return std::nullopt;
    }
    code += content + "\n";
    if (!names.empty())
      names += " + ";
    names += f;
  }
  return {{code, names}};
}

static void appendEval(std::string &code, std::string &names,
                       const std::string &eval) {
  if (!eval.empty()) {
    code += eval + "\n";
    if (!names.empty())
      names += " + ";
    names += "<eval>";
  }
}

static void readFromStdIn(std::string &code, std::string &names) {
  if (code.empty() && !isatty(STDIN_FILENO)) {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    code = ss.str();
    names = "<stdin>";
  }
}

static havel::EngineConfig makeEngineConfig(const havel::init::LaunchConfig &cfg) {
  return {.debugBytecode = cfg.debugBytecode,
          .debugLexer = cfg.debugLexer,
          .debugParser = cfg.debugParser,
          .debugAst = cfg.debugAst,
          .debugEmitter = cfg.debugEmitter,
          .stopOnError = cfg.stopOnError,
          .leanMinimalStartup = cfg.minimalMode,
          .headlessMode = cfg.headlessMode,
          .pureStdlib = cfg.pureStdlib,
          .vmConfig = cfg.vmConfig,
          .serviceIncludes = cfg.serviceIncludes,
          .serviceExcludes = cfg.serviceExcludes};
}

static havel::repl::REPLConfig makeREPLConfig(const havel::init::LaunchConfig &cfg) {
  havel::repl::REPLConfig replConfig;
  replConfig.debugMode = cfg.debugMode;
  replConfig.stopOnError = cfg.stopOnError;
  replConfig.debugBytecode = cfg.debugBytecode;
  replConfig.debugLexer = cfg.debugLexer;
  replConfig.debugParser = cfg.debugParser;
  replConfig.debugAst = cfg.debugAst;
  replConfig.outputLogFile = cfg.outputLogFile;
  replConfig.historyFile = cfg.historyFile;
  return replConfig;
}

static std::shared_ptr<HostAPI> createHostAPI(havel::Havel &inst) {
  return std::make_shared<HostAPI>(inst.getIOPtr(), inst.getHotkeyManagerPtr(),
                                   Configs::Get(), inst.getWindowManagerPtr(),
                                   inst.getAudioManager(), nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, inst.getBrightnessManagerPtr());
}

static int runLint(const std::string &code, const std::string &primaryFile,
                   const havel::init::LaunchConfig &cfg) {
  havel::parser::Parser parser{{.lexer = cfg.debugLexer,
                                .parser = cfg.debugParser,
                                .ast = cfg.debugAst}};
  std::unique_ptr<havel::ast::Program> program;
  try {
    program = parser.produceAST(code);
  } catch (const std::exception &) {
  }

  if (parser.hasErrors()) {
    for (const auto &err : parser.getErrors()) {
      std::string sourceLine;
      if (err.line > 0) {
        std::istringstream ss(code);
        std::string line;
        for (size_t i = 1; i <= err.line; ++i) {
          if (!std::getline(ss, line))
            break;
          if (i == err.line) {
            sourceLine = line;
            break;
          }
        }
      }
      std::string formatted =
          havel::ErrorPrinter::formatError("error", err.message, primaryFile,
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
    } catch (const std::exception &) {
    }
    if (compiler.hasErrors()) {
      for (const auto &err : compiler.errors()) {
        std::string sourceLine;
        if (err.line > 0) {
          std::istringstream ss(code);
          std::string line;
          for (size_t i = 1; i <= err.line; ++i) {
            if (!std::getline(ss, line))
              break;
            if (i == err.line) {
              sourceLine = line;
              break;
            }
          }
        }
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.what(), primaryFile, err.line, err.column, 1,
            sourceLine);
        std::cerr << formatted;
      }
      error("Compilation failed with {} error(s)", compiler.errors().size());
      return 1;
    }
  }
  info("Linting successful");
  return 0;
}

static std::unique_ptr<havel::ast::Program>
parseScript(const std::string &code, const havel::init::LaunchConfig &cfg) {
  havel::parser::Parser parser{{.lexer = cfg.debugLexer,
                                .parser = cfg.debugParser,
                                .ast = cfg.debugAst}};
  try {
    return parser.produceAST(code);
  } catch (const std::exception &) {
    return nullptr;
  }
}

static bool programHasHotkeys(const havel::ast::Program &program) {
  for (const auto &stmt : program.body) {
    if (!stmt)
      continue;
    if (stmt->kind == havel::ast::NodeType::HotkeyBinding)
      return true;
    if (stmt->kind == havel::ast::NodeType::WhenBlockStatement) {
      const auto &wb = static_cast<const havel::ast::WhenBlock &>(*stmt);
      for (const auto &inner : wb.statements) {
        if (inner && inner->kind == havel::ast::NodeType::HotkeyBinding)
          return true;
      }
    }
  }
  return false;
}

static void installMinimalSignalHandlers() {
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = [](int sig) {
    ExitReason reason = ExitReason::SignalInt;
    if (sig == SIGTERM) reason = ExitReason::SignalTerm;
    else if (sig == SIGQUIT) reason = ExitReason::SignalQuit;
    else if (sig == SIGSEGV) reason = ExitReason::SignalCrash;
    havel::exit(reason, 0);
  };
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGQUIT, &sa, nullptr);
  sigaction(SIGSEGV, &sa, nullptr);
}

static int runBytecodeFiles(const havel::init::LaunchConfig &cfg,
                            const std::vector<std::string> &hvcFiles) {
  for (const auto &f : hvcFiles) {
    std::ifstream file(f, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      error("Cannot open bytecode file: {}", f);
      return 2;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
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
    havel::compiler::VMConfig vmCfg = cfg.vmConfig;
    havel::compiler::VMConfig vmCfg2 = vmCfg;
    havel::compiler::VM tempVm(vmCfg2);
    ctx.vm = &tempVm;
    // Create a local BrightnessManager for non-self-hosted mode
    auto localBrightnessManager = std::make_shared<havel::BrightnessManager>();
    ctx.brightnessManager = localBrightnessManager.get();
    auto bridge = havel::createModules(ctx);
    auto *vm = static_cast<havel::compiler::VM *>(ctx.vm);
    const bool coreProfile = (cfg.profile == "core") || cfg.minimalMode;
#ifdef HAVEL_ENABLE_LLVM
    const bool wantJIT = cfg.useJIT || cfg.vmConfig.tiering_enabled ||
                         (std::getenv("HAVEL_TIERING") &&
                          std::string(std::getenv("HAVEL_TIERING")) != "0");
    if (wantJIT && vm->getJITCompiler()) {
      auto* jit = static_cast<havel::compiler::BytecodeOrcJIT*>(vm->getJITCompiler());
      jit->setCompilationVM(vm);
      jit->setDebugMode(cfg.debugJIT);
      jit->setDumpIR(cfg.dumpIR);
      jit->setDumpAsmToFile(cfg.outputAsmToFile);
      jit->setShowWarnings(cfg.aotWarnings);
      vm->setHotFunctionCallback(
          [jit](const havel::compiler::BytecodeFunction &func) {
            if (!jit->isCompiled(func.name))
              jit->compileFunction(func);
          });
      vm->setHotTraceCallback(
          [jit](const havel::compiler::BytecodeFunction &func,
                uint32_t start_ip,
                uint64_t hot_count) {
            if (jit) {
              jit->compileTrace(func, start_ip, hot_count);
            }
          });
    }
#endif
    bridge->install(coreProfile ? havel::InstallProfile::Core
                                : havel::InstallProfile::Full,
                    !coreProfile);
    if (coreProfile) {
      if (cfg.pureStdlib)
        havel::registerPureStdLib(*vm);
      else
        havel::registerCoreStdLib(*vm);
    }
    for (const auto &[name, fn] : bridge->options().host_functions)
      vm->registerHostFunction(name, fn);
    info("Loaded bytecode file: {} ({} function(s))", f,
         chunk->getFunctionCount());
    if (cfg.debugBytecode)
      info("Bytecode loaded successfully: {}", f);
    auto chunkPtr =
        std::make_shared<compiler::BytecodeChunk>(std::move(*chunk));
    vm->setMainChunkShared(chunkPtr);
    vm->setCurrentScriptDir(std::filesystem::path(f).parent_path().string());
    {
      std::vector<std::string> pargs;
      pargs.reserve(1 + cfg.scriptArgs.size());
      pargs.push_back(cfg.programName);
      pargs.insert(pargs.end(), cfg.scriptArgs.begin(), cfg.scriptArgs.end());
      vm->setProgramArgs(pargs);
    }
    if (!cfg.scriptArgs.empty()) {
      auto arrRef = vm->createHostArray();
      for (const auto &arg : cfg.scriptArgs) {
        auto strRef = vm->createRuntimeString(arg);
        vm->pushHostArrayValue(arrRef,
                               havel::compiler::Value::makeStringId(strRef.id));
      }
      vm->setAppArgs(arrRef.id);
    }
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

// ─── Strategy Classes ─────────────────────────────────────────────

class DaemonStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int argc, char *argv[]) override {
    auto result = loadScriptFiles(cfg.scriptFiles);
    if (!result) return 1;
    auto [combinedCode, combinedNames] = *result;

    // LINT-ONLY MODE
    if (cfg.lintOnly && !combinedCode.empty()) {
      info("Linting scripts: {}", combinedNames);
      return runLint(combinedCode,
                     combinedNames.empty() ? "input" : combinedNames, cfg);
    }

    auto *backend = host::UIManager::instance().backend();
    if (!backend) {
      error("No UI backend available. Havel needs a UI backend (Qt6 or GTK4) "
            "for the system tray. Run 'havel --run <script>' for headless "
            "execution.");
      return 1;
    }
    host::UIBackend::ApplicationMetadata meta;
    meta.argc = &argc;
    meta.argv = argv;
    meta.applicationName = "havel";
    meta.applicationVersion = "1.0";
    meta.organizationName = "havel";
    meta.quitOnLastWindowClosed = false;
    backend->setApplicationMetadata(meta);

    while (true) {
      {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i)
          args.emplace_back(argv[i]);

        havel::Havel havel_inst(cfg.isStartup, "", false, true, args);
        if (!havel_inst.isInitialized()) {
          error("Failed to initialize havel::Havel");
          return 1;
        }

        if (!combinedCode.empty()) {
          auto *bytecodeVM = havel_inst.getBytecodeVM();
          auto *modules = havel_inst.getModules();
          if (bytecodeVM && modules) {
            info("Executing combined scripts with bytecode VM: {}",
                 combinedNames);
            havel::compiler::PipelineOptions options = modules->options();
            options.compile_unit_name = combinedNames;
            options.vm_override = bytecodeVM;
            options.debugBytecode = cfg.debugBytecode;
            auto *ee = havel_inst.getExecutionEngine();
            if (ee) {
              ee->setScriptReady(true);
              options.yield_callback = [ee]() { ee->processGoroutinesInline(); };
            }
            auto exec_t0 = havel::startup_now();
            try {
              havel::compiler::runBytecodePipeline(combinedCode, "__main__",
                                                   options);
              havel::startup_timing_report("runBytecodePipeline", exec_t0);
              info("Execution completed successfully");
            } catch (const std::exception &e) {
              havel::startup_timing_report("runBytecodePipeline", exec_t0);
              error("Execution error: {}", e.what());
            }
          }
        }

        info("Havel started successfully - running in system tray");
        havel_inst.setShutdownCallback(
            [](int code) { host::UIManager::instance().backend()->quitEventLoop(code); });

        if (auto *io = havel_inst.getIO()) {
          backend->setIdleCallback([io]() { io->PumpOnce(); });
        }

        int exitCode = backend->runEventLoop();
        if (exitCode != 42)
          return exitCode;
      }
      backend->resetPerRunState();
      info("Restart requested - relaunching application");
    }
  }
};

class ScriptStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int argc, char *argv[]) override {
    // Check for .hvc bytecode files
    std::vector<std::string> hvcFiles, hvFiles;
    for (const auto &f : cfg.scriptFiles) {
      if (f.size() >= 4 && f.substr(f.size() - 4) == ".hvc")
        hvcFiles.push_back(f);
      else
        hvFiles.push_back(f);
    }
    if (!hvcFiles.empty() && hvFiles.empty() && cfg.evalString.empty())
      return runBytecodeFiles(cfg, hvcFiles);

    auto result = loadScriptFiles(cfg.scriptFiles);
    if (!result) return 1;
    auto [combinedCode, combinedNames] = *result;
    appendEval(combinedCode, combinedNames, cfg.evalString);

    // Parse once to check for hotkey bindings
    auto program = parseScript(combinedCode, cfg);
    bool hasHotkeys = program && programHasHotkeys(*program);

    if (hasHotkeys) {
      // Full mode with UI backend
      if (debugging::debug_io)
        debug("Hotkeys detected — using full execution mode");

      auto *backend = host::UIManager::instance().backend();
      if (!backend) {
        error("No UI backend available to run hotkey scripts. Install a UI "
              "backend (Qt6 or GTK4) or run 'havel --run <script>' for "
              "headless execution (hotkeys will not register).");
        return 1;
      }
      host::UIBackend::ApplicationMetadata meta;
      meta.applicationName = "havel";
      meta.applicationVersion = "1.0";
      meta.organizationName = "havel";
      meta.quitOnLastWindowClosed = true;
      backend->setApplicationMetadata(meta);

      std::vector<std::string> args;
      for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);

      havel::Havel havel_inst(false, combinedNames, false, true, args);
      if (!havel_inst.isInitialized()) {
        error("Failed to initialize havel::Havel");
        return 1;
      }

      auto *bytecodeVM = havel_inst.getBytecodeVM();
      auto *modules = havel_inst.getModules();
      if (!bytecodeVM || !modules) {
        error("Bytecode VM not available");
        return 1;
      }

      auto *hkManager = havel_inst.getHotkeyManagerPtr();
      auto hostAPI = createHostAPI(havel_inst);
      havel::initializeServiceRegistry(hostAPI, cfg.serviceIncludes,
                                       cfg.serviceExcludes, cfg.headlessMode);
      hostAPI->SetVM(bytecodeVM);
      bytecodeVM->setTimerCheckFunction(
          [modules]() { modules->checkTimers(); });

      auto *ee = havel_inst.getExecutionEngine();
      if (ee)
        ee->setScriptReady(true);

      try {
        havel::compiler::PipelineOptions options = modules->options();
        options.compile_unit_name = combinedNames;
        options.vm_override = bytecodeVM;
        options.debugBytecode = cfg.debugBytecode;
        if (ee) {
          // Pump goroutine scheduler from the main fiber yield hook so a
          // goroutine spawned by a hotkey script runs while main blocks in
          // a long sleep inside the chunked-sleep bytecode path.
          options.yield_callback = [ee]() { ee->processGoroutinesInline(); };
        }
        auto exec_t0 = havel::startup_now();
        havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
        havel::startup_timing_report("runBytecodePipeline", exec_t0);
      } catch (const std::exception &e) {
        error("Execution error: {}", e.what());
        return 1;
      }

      havel_inst.setShutdownCallback(
          [](int code) { host::UIManager::instance().backend()->quitEventLoop(code); });

      if (auto *io = havel_inst.getIO()) {
        backend->setIdleCallback([io]() { io->PumpOnce(); });
      }

      if (!hkManager || hkManager->getHotkeyList().empty()) {
        info("No hotkeys registered — running event loop for goroutines");
      } else {
        info("Scripts loaded. Hotkeys registered. Press Ctrl+C to exit.");
      }

      // If the script requested exit during the pipeline (e.g. a hotkey or
      // goroutine ran exit() while main was executing), do not enter the UI
      // event loop. havel::exit() must NOT be called from the event thread
      // (it destroys the VM and calls std::exit() while the main thread is
      // still alive). Instead return the requested code so the Havel
      // instance destructor runs cleanup() on this thread.
      if (bytecodeVM && bytecodeVM->exitRequested()) {
        return bytecodeVM->exit_code_.load();
      }
      return backend->runEventLoop();
    }

    // Headless mode
    if (debugging::debug_io) debug("ScriptStrategy: going headless (no hotkeys in AST)");
    try {
      havel::HavelEngine engine(makeEngineConfig(cfg));
      engine.initializeMinimal();
      auto exec_t0 = havel::startup_now();
      engine.execute(combinedCode, "__main__", combinedNames);
      havel::startup_timing_report("engine.execute", exec_t0);
      engine.shutdown();
      return 0;
    } catch (const std::exception &e) {
      error("Execution error: {}", e.what());
      return 1;
    }
  }
};

class ScriptOnlyStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int argc, char *argv[]) override {
    std::vector<std::string> hvcFiles, hvFiles;
    for (const auto &f : cfg.scriptFiles) {
      if (f.size() >= 4 && f.substr(f.size() - 4) == ".hvc")
        hvcFiles.push_back(f);
      else
        hvFiles.push_back(f);
    }
    if (!hvcFiles.empty() && hvFiles.empty())
      return runBytecodeFiles(cfg, hvcFiles);

    auto result = loadScriptFiles(cfg.scriptFiles);
    if (!result) return 1;
    auto [combinedCode, combinedNames] = *result;
    appendEval(combinedCode, combinedNames, cfg.evalString);
    readFromStdIn(combinedCode, combinedNames);

    havel::parser::Parser parser{{.lexer = cfg.debugLexer,
                                  .parser = cfg.debugParser,
                                  .ast = cfg.debugAst}};
    std::unique_ptr<havel::ast::Program> program;
    try {
      program = parser.produceAST(combinedCode);
    } catch (const std::exception &) {
    }

    if (parser.hasErrors() || !program) {
      for (const auto &err : parser.getErrors())
        std::cerr << "Parse error: " << err.message << " at line " << err.line
                  << " col " << err.column << std::endl;
      error("Failed to parse script");
      return 1;
    }

    if (programHasHotkeys(*program)) {
      if (debugging::debug_io)
        debug("Hotkey bindings detected - starting full IO/event loop");
      LaunchConfig fullCfg = cfg;
      fullCfg.mode = LaunchConfig::Mode::SCRIPT;
      return ScriptStrategy().execute(fullCfg, argc, argv);
    }

    if (cfg.lintOnly) {
      std::string primary = combinedNames.empty() ? "input" : combinedNames;
      return runLint(combinedCode, primary, cfg);
    }

    if (debugging::debug_io)
      debug("Running Havel scripts (pure mode): {}", combinedNames);

    installMinimalSignalHandlers();

    bool autoExit = Configs::Get().Get<bool>("Debug.AutoExit", false);
    if (autoExit) {
      int delay = Configs::Get().Get<int>("Debug.AutoExitDelay", 15);
      std::thread([delay]() {
        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if (!Configs::Get().Get<bool>("Debug.AutoExit", false))
          return;
        if (debugging::debug_io)
          debug("AutoExit enabled - exiting after {} seconds", delay);
        havel::exit(ExitReason::Normal, 0);
      }).detach();
    }

    try {
      havel::HavelEngine engine(makeEngineConfig(cfg));
      auto t0 = std::chrono::high_resolution_clock::now();
      engine.initializeMinimal();

      if (!cfg.scriptArgs.empty()) {
        auto &vm = *engine.vm();
        auto arrRef = vm.createHostArray();
        for (const auto &arg : cfg.scriptArgs) {
          auto strRef = vm.createRuntimeString(arg);
          vm.pushHostArrayValue(
              arrRef, havel::compiler::Value::makeStringId(strRef.id));
        }
        vm.setAppArgs(arrRef.id);
      }
      {
        std::vector<std::string> pargs;
        pargs.reserve(1 + cfg.scriptArgs.size());
        pargs.push_back(cfg.programName);
        pargs.insert(pargs.end(), cfg.scriptArgs.begin(), cfg.scriptArgs.end());
        engine.vm()->setProgramArgs(pargs);
      }

      auto t1 = std::chrono::high_resolution_clock::now();
      auto exec_t0 = havel::startup_now();
      engine.execute(combinedCode, "__main__", combinedNames);
      havel::startup_timing_report("engine.execute", exec_t0);
      auto t2 = std::chrono::high_resolution_clock::now();
      engine.shutdown();
      auto t3 = std::chrono::high_resolution_clock::now();

      if (cfg.debugMode) {
        double init_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        double exec_ms =
            std::chrono::duration<double, std::milli>(t2 - t1).count();
        double shut_ms =
            std::chrono::duration<double, std::milli>(t3 - t2).count();
        double total_ms =
            std::chrono::duration<double, std::milli>(t3 - t0).count();
        info("benchmark: init={:.1f}ms exec={:.1f}ms shutdown={:.1f}ms "
             "total={:.1f}ms",
             init_ms, exec_ms, shut_ms, total_ms);
      }
      return 0;
    } catch (const std::exception &e) {
      error("Bytecode error: {}", e.what());
      return 1;
    }
  }
};

// Helper for all REPL strategies
static int setupFullReplCommon(const havel::init::LaunchConfig &cfg) {
  auto *replBackend = host::UIManager::instance().backend();
  if (!replBackend) {
    error(
        "No UI backend available. Use --minimal or --run for REPL without UI.");
    return -1;
  }
  host::UIBackend::ApplicationMetadata meta;
  meta.applicationName = "havel";
  meta.organizationName = "havel";
  meta.quitOnLastWindowClosed = false;
  replBackend->setApplicationMetadata(meta);
  return 0;
}

static int runMinimalReplLoop(havel::HavelEngine &engine,
                              const havel::init::LaunchConfig &cfg) {
  havel::repl::REPL repl(makeREPLConfig(cfg));
  repl.attach(engine.vm(), engine.modules(), collectKnownGlobals(engine.vm()));
  repl.setPumpCallback([&engine]() { engine.tickGoroutines(); });
  return repl.run();
}

static int runFullReplLoop(const havel::init::LaunchConfig &cfg, havel::Havel &havel_inst) {
  auto *bytecodeVM = havel_inst.getBytecodeVM();
  auto *modules = havel_inst.getModules();
  if (!bytecodeVM || !modules)
    return 1;

  havel::repl::REPL repl(makeREPLConfig(cfg));
  auto hostAPI = createHostAPI(havel_inst);
  havel::initializeServiceRegistry(hostAPI, cfg.serviceIncludes,
                                   cfg.serviceExcludes, cfg.headlessMode);
  hostAPI->SetVM(bytecodeVM);
  repl.attach(bytecodeVM, havel_inst.getModules(),
              collectKnownGlobals(bytecodeVM));

  auto *io = havel_inst.getIOPtr();
  auto *hkManager = havel_inst.getHotkeyManagerPtr();
  if (io) {
    repl.setPumpCallback([io]() { io->PumpOnce(); });
    repl.setUngrabCallback([io, hkManager]() {
      if (hkManager)
        hkManager->suspendGrabs();
      else
        io->UngrabAll();
    });
    repl.setResumeGrabsCallback([hkManager]() {
      if (hkManager)
        hkManager->resumeGrabs();
    });
  }
  return repl.run();
}

class ScriptAndReplStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int argc, char *argv[]) override {
    try {
      auto result = loadScriptFiles(cfg.scriptFiles);
      if (!result) return 1;
      auto [combinedCode, combinedNames] = *result;

      if (cfg.minimalMode) {
        if (cfg.scriptFiles.empty()) {
          error("No script file provided");
          return 1;
        }
        info("Running scripts and starting REPL in minimal mode...");
        havel::HavelEngine engine(makeEngineConfig(cfg));
        engine.initializeMinimal();
        info("Executing script code...");
        try {
          engine.execute(combinedCode, "__main__",
                         combinedNames.empty() ? "script" : combinedNames);
        } catch (const std::exception &e) {
          error("Script execution failed: {}", e.what());
          return 1;
        }
        return runMinimalReplLoop(engine, cfg);
      }

      info("Running scripts and starting REPL with full features...");
      if (setupFullReplCommon(cfg) != 0)
        return 1;

      std::vector<std::string> args;
      havel::Havel havel_inst(false, combinedNames, true, true, args);
      if (!havel_inst.isInitialized())
        return 1;

      auto *bytecodeVM = havel_inst.getBytecodeVM();
      auto *modules = havel_inst.getModules();
      if (!bytecodeVM || !modules)
        return 1;

      bytecodeVM->setTimerCheckFunction(
          [modules]() { modules->checkTimers(); });

      auto *ee = havel_inst.getExecutionEngine();
      if (ee)
        ee->setScriptReady(true);

      try {
        havel::compiler::PipelineOptions options = modules->options();
        options.compile_unit_name = combinedNames;
        options.vm_override = bytecodeVM;
        options.debugBytecode = cfg.debugBytecode;
        if (ee)
          options.yield_callback = [ee]() { ee->processGoroutinesInline(); };
        havel::compiler::runBytecodePipeline(combinedCode, "__main__", options);
      } catch (const std::exception &e) {
        error("Script execution error: {}", e.what());
        return 1;
      }

      auto *hkManager = havel_inst.getHotkeyManagerPtr();
      if (hkManager)
        hkManager->printHotkeys();
      info("Script loaded. Hotkeys registered. Enter REPL...");

      return runFullReplLoop(cfg, havel_inst);

    } catch (const std::exception &e) {
      error("Script+REPL error: {}", e.what());
      return 1;
    }
  }
};

class ReplStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int, char *[]) override {
    try {
      if (cfg.minimalMode) {
        info("Starting Havel REPL in minimal mode (no IO/hotkeys)...");
        auto result = loadScriptFiles(cfg.scriptFiles);
        if (!result) return 1;
        auto [combinedCode, combinedNames] = *result;

        havel::HavelEngine engine(makeEngineConfig(cfg));
        engine.initializeMinimal();

        if (!combinedCode.empty()) {
          info("Executing script code...");
          try {
            engine.execute(combinedCode, "__main__",
                           combinedNames.empty() ? "script" : combinedNames);
          } catch (const std::exception &e) {
            error("Script execution failed: {}", e.what());
            return 1;
          }
        }

        return runMinimalReplLoop(engine, cfg);
      }

      info("Starting Havel REPL with full features (hotkeys, GUI)...");
      havel::debug("Running in REPL mode (full):");
      havel::debug(" - GUI: enabled");
      havel::debug(" - IO/Hotkeys: enabled");

      if (setupFullReplCommon(cfg) != 0)
        return 1;

      std::vector<std::string> args;
      havel::Havel havel_inst(false, "", true, true, args);
      if (!havel_inst.isInitialized()) {
        error("Failed to initialize havel::Havel");
        return 1;
      }

      return runFullReplLoop(cfg, havel_inst);

    } catch (const std::exception &e) {
      error("REPL error: {}", e.what());
      return 1;
    }
  }
};

class SelfHostedStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int argc, char *argv[]) override {
    info("Engine: self-hosted (Havel)");

    char selfBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
    std::string binDir;
    if (len > 0) {
      selfBuf[len] = '\0';
      binDir = std::filesystem::path(selfBuf).parent_path().string();
    }

    std::vector<std::string> searchPaths = {
        binDir + "/../modules/lang/launcher.hv",
        binDir + "/../../modules/lang/launcher.hv",
        "modules/lang/launcher.hv",
        "../modules/lang/launcher.hv",
        "../../modules/lang/launcher.hv",
    };

    std::string launcherPath;
    for (const auto &candidate : searchPaths) {
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec) && !ec) {
        launcherPath = candidate;
        break;
      }
    }
    if (launcherPath.empty()) {
      error("Cannot find modules/lang/launcher.hv");
      return 1;
    }

    std::error_code ec;
    auto absPath = std::filesystem::canonical(launcherPath, ec);
    if (!ec)
      launcherPath = absPath.string();

    // Load user script files to check for hotkeys (like ScriptStrategy does)
    std::string combinedCode;
    std::string combinedNames;
    for (const auto &f : cfg.scriptFiles) {
      std::string content = readScriptFile(f);
      if (content.empty()) {
        error("Failed to read script file: {}", f);
        return 1;
      }
      combinedCode += content + "\n";
      if (!combinedNames.empty())
        combinedNames += " + ";
      combinedNames += f;
    }
    if (!cfg.evalString.empty()) {
      combinedCode += cfg.evalString + "\n";
      if (!combinedNames.empty())
        combinedNames += " + ";
      combinedNames += "<eval>";
    }

    // Parse user scripts to check for hotkeys
    auto program = parseScript(combinedCode, cfg);
    bool hasHotkeys = program && programHasHotkeys(*program);

    std::string launcherCode = readScriptFile(launcherPath);
    if (launcherCode.empty()) {
      error("Cannot read launcher.hv at {}", launcherPath);
      return 1;
    }

    std::vector<std::string> appArgList;

    // Mode flag
    switch (cfg.launchMode) {
    case LaunchConfig::Mode::REPL:
      appArgList.push_back("--repl");
      break;
    case LaunchConfig::Mode::SCRIPT_AND_REPL:
      appArgList.push_back("--repl");
      break;
    case LaunchConfig::Mode::SCRIPT:
      appArgList.push_back("--run");
      break;
    case LaunchConfig::Mode::TEST:
      appArgList.push_back("--test");
      appArgList.push_back(cfg.testDir);
      appArgList.push_back(std::to_string(cfg.testTimeout));
      break;
    default:
      // SCRIPT_ONLY: pass script files directly
      break;
    }

    // Debug flags
    if (cfg.debugBytecode)
      appArgList.push_back("--debug-bytecode");
    if (cfg.debugLexer)
      appArgList.push_back("--debug-lexer");
    if (cfg.debugParser)
      appArgList.push_back("--debug-parser");
    if (cfg.debugAst)
      appArgList.push_back("--debug-ast");
    if (cfg.stopOnError)
      appArgList.push_back("--error");
    if (cfg.headlessMode)
      appArgList.push_back("--headless");

    // Script files
    for (const auto &f : cfg.scriptFiles)
      appArgList.push_back(f);

    // Eval string
    if (cfg.lintOnly)
      appArgList.push_back("--lint");
    if (!cfg.evalString.empty()) {
      appArgList.push_back("--eval");
      appArgList.push_back(cfg.evalString);
    }


    // Script args (after --)
    if (!cfg.scriptArgs.empty()) {
      appArgList.push_back("--");
      for (const auto &a : cfg.scriptArgs)
        appArgList.push_back(a);
    }

    installMinimalSignalHandlers();

    // If user script has hotkeys and not headless, run with UI event loop
    if (hasHotkeys && !cfg.headlessMode) {
      return executeWithUIBackend(cfg, argc, argv, launcherCode, launcherPath, appArgList, combinedNames);
    }

    // Headless / no hotkeys: run via engine.execute (which calls processGoroutines)
    try {
      havel::HavelEngine engine(makeEngineConfig(cfg));
      engine.initializeMinimal();

      auto &vm = *engine.vm();
      auto arrRef = vm.createHostArray();
      for (const auto &arg : appArgList) {
        auto strRef = vm.createRuntimeString(arg);
        vm.pushHostArrayValue(arrRef,
                              havel::compiler::Value::makeStringId(strRef.id));
      }
      vm.setAppArgs(arrRef.id);

      auto exec_t0 = havel::startup_now();
      engine.execute(launcherCode, "__main__", launcherPath);
      havel::startup_timing_report("engine.execute", exec_t0);
      engine.shutdown();
      return 0;
    } catch (const std::exception &e) {
      error("Self-hosted error: {}", e.what());
      return 1;
    }
  }

private:
  int executeWithUIBackend(const havel::init::LaunchConfig &cfg,
                           int argc, char *argv[],
                           const std::string& launcherCode,
                           const std::string& launcherPath,
                           const std::vector<std::string>& appArgList,
                           const std::string& combinedNames) {
    auto *backend = host::UIManager::instance().backend();
    if (!backend) {
      error("No UI backend available to run hotkey scripts. Install a UI "
            "backend (Qt6 or GTK4) or run with --headless for "
            "headless execution (hotkeys will not register).");
      return 1;
    }
    host::UIBackend::ApplicationMetadata meta;
    meta.argc = &argc;
    meta.argv = argv;
    meta.applicationName = "havel";
    meta.applicationVersion = "1.0";
    meta.organizationName = "havel";
    meta.quitOnLastWindowClosed = true;
    backend->setApplicationMetadata(meta);

    try {
      havel::HavelEngine engine(makeEngineConfig(cfg));
      engine.initializeMinimal();

      auto &vm = *engine.vm();
      auto arrRef = vm.createHostArray();
      for (const auto &arg : appArgList) {
        auto strRef = vm.createRuntimeString(arg);
        vm.pushHostArrayValue(arrRef,
                              havel::compiler::Value::makeStringId(strRef.id));
      }
      vm.setAppArgs(arrRef.id);

      // Compile and run launcher synchronously - it spawns user script goroutines
      auto exec_t0 = havel::startup_now();
      engine.compileAndRunMainSync(launcherCode, "__main__", launcherPath);
      havel::startup_timing_report("engine.compileAndRunMainSync", exec_t0);

      // If script requested exit during launcher execution, don't enter event loop
      if (engine.vm()->exitRequested()) {
        return engine.vm()->exitCode();
      }

      // Set up idle callback to drive goroutine scheduler and check for exit
      backend->setIdleCallback([&engine, backend]() {
        engine.tickGoroutines();
        if (engine.vm()->exitRequested()) {
          backend->quitEventLoop(engine.vm()->exitCode());
        }
      });

      info("Scripts loaded. Hotkeys registered. Press Ctrl+C to exit.");
      return backend->runEventLoop();
    } catch (const std::exception &e) {
      error("Self-hosted UI error: {}", e.what());
      return 1;
    }
  }
};

class TestStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &cfg, int, char *[]) override {
    if (cfg.testDir.empty()) {
      error("No test directory specified. Usage: havel --test <directory>");
      return 1;
    }

    std::vector<std::string> testFiles;
    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(cfg.testDir)) {
        if (entry.is_regular_file()) {
          std::string path = entry.path().string();
          if (path.size() >= 3 && path.substr(path.size() - 3) == ".hv")
            testFiles.push_back(path);
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

    std::sort(testFiles.begin(), testFiles.end());

    std::string selfPath = "/proc/self/exe";
    char selfBuf[PATH_MAX];
    ssize_t len = readlink(selfPath.c_str(), selfBuf, sizeof(selfBuf) - 1);
    if (len == -1)
      selfPath = "havel";
    else {
      selfBuf[len] = '\0';
      selfPath = std::string(selfBuf);
    }

    int passed = 0, failed = 0, total = static_cast<int>(testFiles.size());
    info("Running {} tests from '{}'", total, cfg.testDir);

    for (const auto &testFile : testFiles) {
      std::string testName = testFile.substr(testFile.find_last_of('/') + 1);
      std::string cmd = std::format("timeout {} {} --run {}", cfg.testTimeout,
                                    selfPath, testFile);
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe) {
        error("  FAIL {} - failed to run", testName);
        failed++;
        continue;
      }
      std::string output;
      char buf[256];
      while (fgets(buf, sizeof(buf), pipe))
        output += buf;
      int status = pclose(pipe);
      int exitCode = WEXITSTATUS(status);

      if (exitCode == 0) {
        info("  PASS {}", testName);
        passed++;
      } else {
        std::string errLine;
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
          if (line.find("[ERROR]") != std::string::npos ||
              line.find("Error:") != std::string::npos) {
            errLine = line;
            errLine.erase(std::remove(errLine.begin(), errLine.end(), '\r'),
                          errLine.end());
            break;
          }
        }
        if (exitCode == 124)
          error("  FAIL {} - timeout ({}s)", testName, cfg.testTimeout);
        else if (!errLine.empty()) {
          size_t pos = errLine.find("[ERROR]");
          if (pos != std::string::npos)
            errLine = errLine.substr(pos + 7);
          else {
            pos = errLine.find("Error:");
            if (pos != std::string::npos)
              errLine = errLine.substr(pos + 6);
          }
          while (!errLine.empty() && errLine[0] == ' ')
            errLine.erase(0, 1);
          error("  FAIL {} - {}", testName, errLine);
        } else
          error("  FAIL {} - exit code {}", testName, exitCode);
        failed++;
      }
    }

    std::cout << "\n";
    info("Test Results:");
    info("  Total:  {}", total);
    info("  Passed: {}", passed);
    info("  Failed: {}", failed);
    return failed > 0 ? 1 : 0;
  }
};

class CliStrategy : public RunStrategy {
public:
  int execute(const havel::init::LaunchConfig &, int, char *[]) override {
    error("CLI not available - interpreter removed");
    return 1;
  }
};

int HavelLauncher::run(int argc, char *argv[]) {
  try {
    LaunchConfig cfg = parseArgs(argc, argv);

    // Apply self-hosted config from main() (only if not --no-self-hosted)
    if (!cfg.noSelfHosted && !self_hosted_modules_path_config_.empty()) {
      cfg.vmConfig.self_hosted_modules_path = self_hosted_modules_path_config_;
    }

    if (cfg.target == LaunchConfig::Target::INTERPRET) {
      cfg.useJIT = false;
    } else if (cfg.target == LaunchConfig::Target::JIT) {
      cfg.useJIT = true;
    }

    Configs::Get().Set("Compiler.JIT", cfg.useJIT ? "true" : "false");
    Configs::Get().Set("Compiler.DebugJIT", cfg.debugJIT ? "true" : "false");
    Configs::Get().Set("Compiler.DumpIR", cfg.dumpIR ? "true" : "false");
    Configs::Get().Set("Compiler.OutputAsm",
                       cfg.outputAsmToFile ? "true" : "false");
    Configs::Get().Set("Compiler.JITWarnings",
                       cfg.aotWarnings ? "true" : "false");
#ifdef HAVEL_ENABLE_LLVM
    Configs::Get().Set("Compiler.JITTargetOS", normalizeTargetOS(cfg.targetOS));
#else
    Configs::Get().Set("Compiler.JITTargetOS", cfg.targetOS);
#endif

    if (cfg.buildOnly)
      return runBuild(cfg);

    if (!cfg.noSelfHosted && !cfg.vmConfig.self_hosted_modules_path.empty()) {
      namespace fs = std::filesystem;
      fs::path langDir =
          fs::path(cfg.vmConfig.self_hosted_modules_path) / "modules" / "lang";
      if (fs::exists(langDir) && !fs::is_empty(langDir)) {
        if (cfg.mode == LaunchConfig::Mode::REPL ||
            cfg.mode == LaunchConfig::Mode::SCRIPT ||
            cfg.mode == LaunchConfig::Mode::SCRIPT_ONLY ||
            cfg.mode == LaunchConfig::Mode::SCRIPT_AND_REPL ||
            cfg.mode == LaunchConfig::Mode::TEST) {
          cfg.launchMode = cfg.mode;
          cfg.mode = LaunchConfig::Mode::SELF_HOSTED;
          cfg.minimalMode = true;
          cfg.pureStdlib = true;
        }
      } else {
        // The self-hosted tree (<exe>/../out/modules/lang) only exists in a
        // source checkout. System-installed binaries (/usr/bin/havel) never
        // have it - fall back to the C++ pipeline instead of failing.
        cfg.vmConfig.self_hosted_modules_path.clear();
      }
    } else if (!cfg.noSelfHosted && cfg.vmConfig.self_hosted_modules_path.empty()) {
      // Try to derive self-hosted path from binary location: binary/../out
      namespace fs = std::filesystem;
      auto exePath = Env::executable();
      if (!exePath.empty()) {
        fs::path candidate = fs::path(exePath).parent_path().parent_path() / "out";
        if (fs::exists(candidate / "modules" / "lang")) {
          cfg.vmConfig.self_hosted_modules_path = candidate.string();
          fs::path langDir = candidate / "modules" / "lang";
          if (!fs::is_empty(langDir)) {
            if (cfg.mode == LaunchConfig::Mode::REPL ||
                cfg.mode == LaunchConfig::Mode::SCRIPT ||
                cfg.mode == LaunchConfig::Mode::SCRIPT_ONLY ||
                cfg.mode == LaunchConfig::Mode::SCRIPT_AND_REPL ||
                cfg.mode == LaunchConfig::Mode::TEST) {
              cfg.launchMode = cfg.mode;
              cfg.mode = LaunchConfig::Mode::SELF_HOSTED;
              cfg.minimalMode = true;
              cfg.pureStdlib = true;
            }
          }
        }
        // If not found, fall through to --no-self-hosted behaviour silently
      }
    }

    if (!cfg.diffPipelinePath.empty()) {
      return diffPipeline(cfg);
    }

    auto strategy = createStrategy(cfg);
    return strategy->execute(cfg, argc, argv);
  } catch (const std::exception &e) {
    error("Fatal error: {}", e.what());
    return 1;
  }
}

std::unique_ptr<RunStrategy>
HavelLauncher::createStrategy(const havel::init::LaunchConfig &cfg) {
  using Mode = LaunchConfig::Mode;
  switch (cfg.mode) {
  case Mode::DAEMON:
    return std::make_unique<DaemonStrategy>();
  case Mode::SCRIPT:
    return std::make_unique<ScriptStrategy>();
  case Mode::SCRIPT_ONLY:
    return std::make_unique<ScriptOnlyStrategy>();
  case Mode::REPL:
    return std::make_unique<ReplStrategy>();
  case Mode::SCRIPT_AND_REPL:
    return std::make_unique<ScriptAndReplStrategy>();
  case Mode::TEST:
    return std::make_unique<TestStrategy>();
  case Mode::CLI:
    return std::make_unique<CliStrategy>();
  case Mode::SELF_HOSTED:
    return std::make_unique<SelfHostedStrategy>();
  default:
    error("Unknown mode");
    return std::make_unique<CliStrategy>();
  }
}

LaunchConfig HavelLauncher::parseArgs(int argc, char *argv[]) {
  LaunchConfig cfg;
  if (argc > 0)
    cfg.programName = argv[0];

  // Read logging config from environment variables first
  if (const char* env = std::getenv("HAVEL_LOG_LEVEL")) {
    std::string level = env;
    if (level == "debug") Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
    else if (level == "info") Logger::getInstance().setLogLevel(Logger::LOG_INFO);
    else if (level == "warning") Logger::getInstance().setLogLevel(Logger::LOG_WARNING);
    else if (level == "error") Logger::getInstance().setLogLevel(Logger::LOG_ERROR);
    else if (level == "fatal") Logger::getInstance().setLogLevel(Logger::LOG_FATAL);
  }
  if (const char* env = std::getenv("HAVEL_LOG_FILE")) {
    Logger::getInstance().setLogFile(env);
  }
  if (std::getenv("HAVEL_LOG_NO_COLOR")) {
    Logger::getInstance().setColoredOutput(false);
  }
  if (const char* env = std::getenv("HAVEL_LOG_ORIGIN_FILTER")) {
    std::string filter = env;
    size_t p1 = filter.find(':');
    size_t p2 = filter.find(':', p1 + 1);
    std::string category = (p1 != std::string::npos) ? filter.substr(0, p1) : filter;
    std::string subsystem = "";
    int priority = 0;
    if (p1 != std::string::npos) {
      if (p2 != std::string::npos) {
        subsystem = filter.substr(p1 + 1, p2 - p1 - 1);
        priority = std::stoi(filter.substr(p2 + 1));
      } else {
        subsystem = filter.substr(p1 + 1);
      }
    }
    Logger::Origin origin("", "", 0, category.empty() ? nullptr : category.c_str(),
                          subsystem.empty() ? nullptr : subsystem.c_str(), priority);
    Logger::getInstance().setOriginFilter(origin);
  }

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
    } else if (arg == "--debug-emitter") {
      cfg.debugEmitter = true;
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
    } else if (arg == "--log-level" && i + 1 < argc) {
      std::string level = argv[++i];
      if (level == "debug") Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
      else if (level == "info") Logger::getInstance().setLogLevel(Logger::LOG_INFO);
      else if (level == "warning") Logger::getInstance().setLogLevel(Logger::LOG_WARNING);
      else if (level == "error") Logger::getInstance().setLogLevel(Logger::LOG_ERROR);
      else if (level == "fatal") Logger::getInstance().setLogLevel(Logger::LOG_FATAL);
      else error("Unknown log level: " + level);
    } else if (arg == "--log-file" && i + 1 < argc) {
      Logger::getInstance().setLogFile(argv[++i]);
    } else if (arg == "--log-no-color") {
      Logger::getInstance().setColoredOutput(false);
    } else if (arg == "--log-origin-filter" && i + 1 < argc) {
      std::string filter = argv[++i];
      // Format: category[:subsystem][:priority]
      size_t p1 = filter.find(':');
      size_t p2 = filter.find(':', p1 + 1);
      std::string category = (p1 != std::string::npos) ? filter.substr(0, p1) : filter;
      std::string subsystem = "";
      int priority = 0;
      if (p1 != std::string::npos) {
        if (p2 != std::string::npos) {
          subsystem = filter.substr(p1 + 1, p2 - p1 - 1);
          priority = std::stoi(filter.substr(p2 + 1));
        } else {
          subsystem = filter.substr(p1 + 1);
        }
      }
      Logger::Origin origin("", "", 0, category.empty() ? nullptr : category.c_str(),
                            subsystem.empty() ? nullptr : subsystem.c_str(), priority);
      Logger::getInstance().setOriginFilter(origin);
    } else if (arg == "--diff" || arg == "-diff") {
      cfg.diffBytecode = true;
      cfg.debugBytecode = true;
    } else if (arg == "--diff-pipeline" && i + 1 < argc) {
      cfg.diffPipelinePath = argv[++i];
    } else if (arg == "--error" || arg == "-e") {
      // Stop on first error/warning
      cfg.stopOnError = true;
    } else if (arg == "--minimal" || arg == "-m") {
      // Minimal mode - no IO/hotkeys/GUI
      cfg.minimalMode = true;
    } else if (arg == "--headless") {
      // Headless mode - skip X11/Brightness/EventListener initialization
      cfg.headlessMode = true;
      cfg.minimalMode = true;
    } else if (arg == "--repl" || arg == "-r" || arg == "--interactive" ||
               arg == "-i") {
      repl = true;
    } else if (arg == "--gui") {
      cfg.mode = LaunchConfig::Mode::DAEMON; // GUI mode is now DAEMON
    } else if (arg == "--full-repl" || arg == "-fr") {
      repl = true;
    } else if (arg == "--no-jit") {
      cfg.useJIT = false;
      if (cfg.target == LaunchConfig::Target::JIT) {
        cfg.target = LaunchConfig::Target::INTERPRET;
      }
    } else if (arg == "--target") {
      if (i + 1 >= argc) {
        error("--target requires one of: interpret, jit, aot, asm, ir, wasm");
        continue;
      }
      std::string target = argv[++i];
      if (target == "interpret") {
        cfg.target = LaunchConfig::Target::INTERPRET;
        cfg.useJIT = false;
      } else if (target == "jit") {
        cfg.target = LaunchConfig::Target::JIT;
        cfg.useJIT = true;
      } else if (target == "aot") {
        cfg.target = LaunchConfig::Target::AOT;
        cfg.buildOnly = true;
        cfg.emitObj = true;
        cfg.emitBinary = true;
      } else if (target == "asm") {
        cfg.target = LaunchConfig::Target::ASM;
        cfg.buildOnly = true;
        cfg.emitAsm = true;
      } else if (target == "ir") {
        cfg.target = LaunchConfig::Target::IR;
        cfg.buildOnly = true;
        cfg.emitLLVM = true;
      } else if (target == "wasm") {
        cfg.target = LaunchConfig::Target::WASM;
        cfg.buildOnly = true;
        cfg.emitWasm = true;
      } else if (target == "elf" || target == "bin") {
        cfg.target = LaunchConfig::Target::ELF;
        cfg.buildOnly = true;
        cfg.emitElf = true;
        cfg.emitObj = true;
      } else {
        error("Unknown --target '{}'. Expected: interpret, jit, aot, asm, ir, "
              "wasm, elf, bin",
              target);
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
      cfg.target = LaunchConfig::Target::AOT;
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
        if (cfg.mode == LaunchConfig::Mode::DAEMON)
          cfg.mode = LaunchConfig::Mode::SCRIPT_ONLY;
        cfg.minimalMode = true;
        cfg.pureStdlib = true;
      }
    } else if (arg == "--run" || arg == "run") {
      cfg.mode = LaunchConfig::Mode::SCRIPT_ONLY;
      cfg.minimalMode = true;
      cfg.pureStdlib = true;
    } else if (arg == "--pure-stdlib") {
      cfg.pureStdlib = true;
    } else if (arg == "--strict-semantics") {
      cfg.strictSemantics = true;
    } else if (arg == "--no-strict-semantics") {
      cfg.strictSemantics = false;
    } else if (arg == "--convert" && i + 1 < argc) {
      cfg.mode = LaunchConfig::Mode::CLI;
      cfg.buildOnly = true;
      std::string target = argv[++i];
      if (target == "jit") {
        cfg.target = LaunchConfig::Target::JIT;
      } else if (target == "aot") {
        cfg.target = LaunchConfig::Target::AOT;
        cfg.emitBinary = true;
      } else if (target == "asm") {
        cfg.target = LaunchConfig::Target::ASM;
        cfg.emitAsm = true;
      } else if (target == "ir") {
        cfg.target = LaunchConfig::Target::IR;
        cfg.emitLLVM = true;
      } else {
        error("Unknown conversion target: {}", target);
      }
    } else if (arg == "--self-hosted") {
      cfg.mode = LaunchConfig::Mode::SELF_HOSTED;
      cfg.minimalMode = true;
      cfg.pureStdlib = true;
    } else if (arg == "--no-self-hosted") {
      cfg.noSelfHosted = true;
    } else if (arg == "--self-hosted-path" && i + 1 < argc) {
      cfg.vmConfig.self_hosted_modules_path = argv[++i];
    } else if (arg == "--test" || arg == "-t") {
      // Test mode - run all .hv files in a directory
      cfg.mode = LaunchConfig::Mode::TEST;
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
        } catch (const std::exception &) {
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
      cfg.target = LaunchConfig::Target::IR;
      cfg.emitLLVM = true;
      cfg.buildOnly = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        cfg.scriptFiles.push_back(argv[++i]);
      }
    } else if (arg == "--emit-asm") {
      cfg.target = LaunchConfig::Target::ASM;
      cfg.emitAsm = true;
      cfg.buildOnly = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        cfg.scriptFiles.push_back(argv[++i]);
      }
    } else if (arg == "--emit-obj") {
      cfg.target = LaunchConfig::Target::AOT;
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
          cfg.asmSyntax = LaunchConfig::AsmSyntax::INTEL;
        } else if (syntax == "att") {
          cfg.asmSyntax = LaunchConfig::AsmSyntax::ATT;
        } else {
          error("Unknown assembly syntax: {}. Supported: intel, att", syntax);
        }
      }
    } else if (arg == "--io") {
      if (i + 1 >= argc) {
        error("--io requires one of: evdev, x11, wayland, windows, auto");
        continue;
      }
      std::string backend = argv[++i];
      if (backend != "auto" &&
          !InputBackend::ParseBackendType(backend)) {
        error("Unknown --io backend '{}'. Supported: evdev, x11, wayland, "
              "windows, auto",
              backend);
        continue;
      }
      // Runtime-only config write (not persisted). EventListener reads
      // IO.Backend when it initializes its input backend.
      Configs::Get().Set("IO.Backend", backend, false);
    } else if (arg == "--enable-service") {
      if (i + 1 < argc) {
        cfg.serviceIncludes.insert(argv[++i]);
      }
    } else if (arg == "--disable-service") {
      if (i + 1 < argc) {
        cfg.serviceExcludes.insert(argv[++i]);
      }
    } else if (arg == "--list-services") {
      cfg.listServices = true;
    } else if (arg == "--heap-max") {
      if (i + 1 < argc)
        cfg.vmConfig.heap_max_bytes = std::stoull(argv[++i]);
    } else if (arg == "--gc-budget") {
      if (i + 1 < argc)
        cfg.vmConfig.gc_budget = std::stoul(argv[++i]);
    } else if (arg == "--gc-incremental") {
      cfg.vmConfig.gc_incremental = true;
    } else if (arg == "--gc-stop-the-world") {
      cfg.vmConfig.gc_stop_the_world = true;
      cfg.vmConfig.gc_incremental = false;
    } else if (arg == "--gc-full-interval") {
      if (i + 1 < argc)
        cfg.vmConfig.gc_full_collection_interval = std::stoul(argv[++i]);
    } else if (arg == "--gc-promotion-age") {
      if (i + 1 < argc)
        cfg.vmConfig.gc_promotion_age =
            static_cast<uint8_t>(std::stoul(argv[++i]));
    } else if (arg == "--max-call-depth") {
      if (i + 1 < argc)
        cfg.vmConfig.max_call_depth = std::stoul(argv[++i]);
    } else if (arg == "--max-instructions") {
      if (i + 1 < argc)
        cfg.vmConfig.max_instructions = std::stoull(argv[++i]);
    } else if (arg == "--tick-instructions") {
      if (i + 1 < argc)
        cfg.vmConfig.goroutine_tick_instructions = std::stoull(argv[++i]);
    } else if (arg == "--hotkey-tick-instructions") {
      if (i + 1 < argc)
        cfg.vmConfig.goroutine_hotkey_tick_instructions =
            std::stoull(argv[++i]);
    } else if (arg == "--tier1-threshold") {
      if (i + 1 < argc)
        cfg.vmConfig.tier1_threshold = std::stoull(argv[++i]);
    } else if (arg == "--tier2-threshold") {
      if (i + 1 < argc)
        cfg.vmConfig.tier2_threshold = std::stoull(argv[++i]);
    } else if (arg == "--tiering") {
      cfg.vmConfig.tiering_enabled = true;
      cfg.useJIT = true;
    } else if (arg == "--timer-interval") {
      if (i + 1 < argc)
        cfg.vmConfig.timer_check_interval = std::stoul(argv[++i]);
    } else if (arg == "--") {
      // Everything after -- is script arguments (app.args), not flags
      for (int j = i + 1; j < argc; j++) {
        cfg.scriptArgs.push_back(argv[j]);
      }
      break;
    } else if (arg == "--help" || arg == "-h") {
      showHelp();
      havel::exit(ExitReason::Normal, 0);
    } else if (arg == "--version" || arg == "-v") {
      std::cout << "havel " << HAVEL_VERSION_STRING << "\n";
      havel::exit(ExitReason::Normal, 0);
    } else if (arg == "lexer") {
      cfg.mode = LaunchConfig::Mode::CLI;
      return cfg;
    } else {
      if (arg.size() < 3 || arg.substr(arg.size() - 3) != ".hv") {
        warning("Script file {} does not end with .hv extension", arg);
      }
      cfg.scriptFiles.push_back(arg);
      if (cfg.mode == LaunchConfig::Mode::DAEMON) {
        cfg.mode = LaunchConfig::Mode::SCRIPT;
      }
    }
  }

  // Determine mode based on flags and script file
  if (repl && !cfg.scriptFiles.empty()) {
    cfg.mode = LaunchConfig::Mode::SCRIPT_AND_REPL;
  } else if (repl) {
    cfg.mode = LaunchConfig::Mode::REPL;
  } else if (cfg.scriptFiles.empty() &&
             cfg.mode == LaunchConfig::Mode::DAEMON) {
    cfg.mode = LaunchConfig::Mode::REPL;
  }
  // Check for debug flags (but don't force minimal mode when --repl is
  // explicitly used)
  if (Configs::Get().Get<bool>("Debug.ForceMinimal", false) &&
      cfg.mode != LaunchConfig::Mode::SCRIPT_AND_REPL) {
    cfg.minimalMode = true;
    debug("Debug.ForceMinimal is set - forcing minimal mode");
  }
  if (Configs::Get().Get<bool>("Debug.ForceMinimal", false) &&
      cfg.mode == LaunchConfig::Mode::SCRIPT_AND_REPL) {
    debug("Debug.ForceMinimal is set but --repl takes precedence");
  }

  // --list-services: print catalog and exit
  if (cfg.listServices) {
    havel::declareAllServices();
    auto &reg = host::ServiceRegistry::instance();
    auto all = reg.listServices();
    std::cout << "Available services:\n";
    for (auto &info : all) {
      std::cout << "  " << info.name;
      if (!info.group.empty())
        std::cout << " [" << info.group << "]";
      std::cout << "\n";
    }
    std::cout
        << "\nUse --enable-service <name> to include only specific services\n";
    std::cout << "Use --disable-service <name> to exclude specific services\n";
    havel::exit(ExitReason::Normal, 0);
  }

  // Otherwise use the mode already set (GUI_ONLY, SCRIPT_ONLY, SCRIPT, CLI)

  return cfg;
}

void havel::init::HavelLauncher::showHelp() {
  std::cout << R"(
Usage: havel [options] <script.hv>

Options:
  -h, --help          Show this help
  -v, --version       Print version and exit
  -d, --debug         Enable debug mode
  -dp, --debug-parser Enable parser debugging
  -da, --debug-ast    Enable AST debugging
  -dl, --debug-lexer  Enable lexer debugging
  -dbc, --debug-bytecode  Enable bytecode debugging
  -dgc, --debug-gc    Enable GC debugging
  -de, --debug-engine Enable engine debugging
  -dio, --debug-io    Enable IO debugging
  -dhk, --debug-hotkeys Enable hotkey debugging
  -e, --error         Stop on first error/warning
  -E, --eval CODE     Run inline Havel code
  -m, --minimal       Minimal mode (no IO/hotkeys/GUI)
  --headless            Headless mode (skip X11/BrightnessManager/EventListener)
  -r, --repl          Start interactive REPL
  -i, --interactive   Same as --repl
  --run               Run script in minimal mode
  --self-hosted       Run via pure Havel pipeline
  --no-self-hosted    Use C++ parser (legacy)
  -t, --test DIR      Run all .hv files in a directory
  --lint FILE         Check syntax and compilation errors
  --build FILE        Compile to bytecode (.hvc)
  -o, --output PATH   Output path for --build
  --emit-llvm FILE    Output LLVM IR (.ll)
  --emit-asm FILE     Output native assembly (.s)
  --emit-obj FILE     Output object file (.o)
  --full-aot          Emit all artifacts in one run
  --target MODE       Target: interpret|jit|aot|asm|ir|wasm|elf|bin
  --os NAME           Target OS: native|linux|windows|macos|wasm
  --arch TRIPLE       Target architecture
  --syntax TYPE       Assembly syntax: att|intel
  --no-jit            Disable JIT compilation
  --debug-jit         Print LLVM IR and Assembly
  --io BACKEND        Input backend: evdev|x11|wayland|windows|auto
                      (default evdev; also settable via IO.Backend config)
  --enable-service NAME  Include service
  --disable-service NAME Exclude service
  --list-services     List available services
  --log-level LEVEL     Set log level: debug, info, warning, error, fatal
  --log-file PATH       Set log file path
  --log-no-color        Disable colored output
  --log-origin-filter   Filter by origin: category[:subsystem][:priority]

Environment variables:
  HAVEL_LOG_LEVEL          Same as --log-level
  HAVEL_LOG_FILE           Same as --log-file
  HAVEL_LOG_NO_COLOR       Same as --log-no-color
  HAVEL_LOG_ORIGIN_FILTER  Same as --log-origin-filter

Modes:
  havel                   - Start REPL (full features)
  havel script.hv         - Run script (full features)
  havel --run script.hv   - Run script (minimal mode)
  havel --repl script.hv  - Run script then REPL
  havel --test DIR        - Run tests in directory
  havel --lint FILE       - Lint script without running
  havel --build FILE      - Compile to bytecode
  havel --target jit FILE - Run with JIT enabled

VM Configuration:
  --heap-max <bytes>     --gc-budget <n>      --gc-incremental
  --gc-stop-the-world    --gc-full-interval   --gc-promotion-age
  --max-call-depth       --max-instructions   --tick-instructions
  --hotkey-tick-instructions --tier1-threshold  --tier2-threshold
  --tiering              --timer-interval
)" << std::flush;
}

int havel::init::HavelLauncher::runBuild(const havel::init::LaunchConfig &cfg) {

  // Build mode: compile .hv files to .hvc bytecode
  std::string combinedCode;
  std::string primaryFile;
  bool isBytecode = false;

  if (cfg.scriptFiles.size() == 1 && cfg.scriptFiles[0].size() >= 4 &&
      cfg.scriptFiles[0].substr(cfg.scriptFiles[0].size() - 4) == ".hvc") {
    isBytecode = true;
    primaryFile = cfg.scriptFiles[0];
  } else {
    for (const auto &f : cfg.scriptFiles) {
      std::string content = readScriptFile(f);
      if (!content.empty()) {
        combinedCode += content + "\n";
        if (primaryFile.empty())
          primaryFile = f;
      } else {
        error("Cannot open script file: {}", f);
        return 1;
      }
    }
  }

  if (combinedCode.empty() && !isBytecode) {
    error("No script files to build");
    return 1;
  }

  // Compute cache path for a module. Uses the same flat-namespace naming as
  // ModuleLoader::cacheFileNameForSource (lang.<stem>.hvc / std.<stem>.hvc /
  // <stem>.<path-hash>.hvc) derived from the canonical SOURCE path, so the
  // resolver finds exactly what was written here.
  const auto companionCachePath = [](const std::string &path) {
    if (path.empty()) {
      return std::string{};
    }
    std::error_code ec;
    std::string canonical = std::filesystem::canonical(path, ec).string();
    if (ec) {
      canonical = path;
    }
    std::string cacheName = havel::ModuleLoader::cacheFileNameForSource(canonical);

    std::string cacheDir = havel::Env::cache() + "/havel";
    std::filesystem::create_directories(cacheDir, ec);
    return cacheDir + "/" + cacheName + ".hvc";
  };

  // Determine output path. Bytecode caches live only in ~/.cache/havel; the
  // default output for `--build FILE` is the flat cache path derived from the
  // canonical source path (same name the resolver looks up).
  std::string outputPath = cfg.outputPath;
  if (outputPath.empty()) {
    outputPath = companionCachePath(primaryFile);
    if (outputPath.empty()) {
      outputPath = "output.hvc";
    }
  }

  info("Building: {} -> {}", primaryFile.empty() ? "input" : primaryFile,
       outputPath);

  const std::string cachePath = companionCachePath(primaryFile);
  if (!isBytecode && !primaryFile.empty() && !cachePath.empty()) {
    std::error_code cacheEc;
    std::error_code sourceEc;
    const bool cacheExists =
        std::filesystem::exists(cachePath, cacheEc) && !cacheEc;
    const bool sourceExists =
        std::filesystem::exists(primaryFile, sourceEc) && !sourceEc;
    if (cacheExists && sourceExists) {
      const auto cacheTime =
          std::filesystem::last_write_time(cachePath, cacheEc);
      const auto sourceTime =
          std::filesystem::last_write_time(primaryFile, sourceEc);
      if (!cacheEc && !sourceEc && cacheTime >= sourceTime) {
        std::ifstream cacheIn(cachePath, std::ios::binary | std::ios::ate);
        if (cacheIn.is_open()) {
          std::streamsize size = cacheIn.tellg();
          cacheIn.seekg(0, std::ios::beg);
          std::vector<uint8_t> buffer(static_cast<size_t>(size));
          if (cacheIn.read(reinterpret_cast<char *>(buffer.data()), size)) {
            // Only write if output path differs from cache path.
            // If outputPath == cachePath, the cache file already contains
            // the correct data - don't rewrite it (avoids mtime update).
            if (outputPath != cachePath) {
              std::ofstream outFile(outputPath, std::ios::binary);
              if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char *>(buffer.data()),
                              buffer.size());
                if (!outFile.good()) {
                  error("Failed to write output file: {}", outputPath);
                  return 1;
                }
                outFile.close();
              } else {
                error("Cannot open output file: {}", outputPath);
                return 1;
              }
            }
            info("Reused bytecode cache: {} -> {}", cachePath, outputPath);
            info("Build successful: {} ({} bytes)", outputPath, buffer.size());
            return 0;
          }
        }
      }
    }
  }

  std::unique_ptr<havel::compiler::BytecodeChunk> chunk;

  if (isBytecode) {
    std::ifstream hvcFile(primaryFile, std::ios::binary | std::ios::ate);
    if (!hvcFile.is_open()) {
      error("Cannot open bytecode file: {}", primaryFile);
      return 1;
    }
    std::streamsize size = hvcFile.tellg();
    hvcFile.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!hvcFile.read(reinterpret_cast<char *>(buffer.data()), size)) {
      error("Failed to read bytecode file: {}", primaryFile);
      return 1;
    }
    havel::compiler::ValueSerializer serializer;
    auto deserializedChunk = serializer.deserializeChunk(buffer);
    if (!deserializedChunk) {
      error("Failed to deserialize bytecode file: {}", primaryFile);
      return 1;
    }
    chunk = std::make_unique<havel::compiler::BytecodeChunk>(
        std::move(*deserializedChunk));
    info("Loaded bytecode from {}", primaryFile);
  } else {
    // Parse
    havel::parser::Parser parser{{.lexer = cfg.debugLexer,
                                  .parser = cfg.debugParser,
                                  .ast = cfg.debugAst}};
    std::unique_ptr<havel::ast::Program> program;
    try {
      program = parser.produceAST(combinedCode);
    } catch (const std::exception &e) {
      error("Parse error: {}", e.what());
      return 1;
    }
    if (parser.hasErrors()) {
      for (const auto &err : parser.getErrors()) {
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.message, primaryFile, err.line, err.column, 1, "");
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
    compiler.setStrictMode(cfg.strictSemantics);
    // Populate known globals with built-in host functions for strict mode
    if (cfg.strictSemantics) {
      std::unordered_set<std::string> knownGlobals = {
        "print", "shell", "sys", "time", "str", "int", "num", "float",
        "range", "len", "any", "all", "eval", "inspect", "prototypes",
        "proto", "getproto", "setproto", "caller", "defun", "del",
        "extension.load", "extension.isLoaded", "extension.list",
        "extension.addSearchPath", "bit", "math", "json", "json.parse",
        "json.stringify", "fs", "fs.read", "fs.write", "fs.append",
        "fs.exists", "fs.remove", "fs.mkdir", "fs.listdir", "fs.isDir",
        "fs.isFile", "fs.size", "fs.mtime", "fs.copy", "fs.move",
        "process", "process.run", "process.spawn", "process.kill",
        "thread", "thread.sleep", "channel", "channel.send",
        "channel.receive", "channel.close", "interval", "timeout",
        "interval.start", "interval.stop", "timeout.start", "timeout.cancel",
        "wait", "wait.group", "defer", "try", "catch", "finally",
        "throw", "type", "typeof", "Hotkey", "Input", "Window", "Mouse",
        "Display", "Brightness", "Audio", "Media", "Image", "Log",
        "Config", "Mode", "Timer", "App", "Automation", "Browser", "Tools",
        "HotkeyManager", "EventListener", "KeyMap", "KeyTap", "LC",
        "LibMpv", "MPV", "Pixel", "Protocols", "Screen", "Screenshot",
        "Socket", "UInput", "WindowMatch", "WinWatch", "X11", "Zoom",
        "DDC", "DRMBrightness", "Compositor", "DayNight", "Device",
        "Evdev", "Gamepad", "Group", "IO", "Keyboard", "Keymap",
        "AsyncMod", "AudioMod", "AutomationMod", "BrightnessMod",
        "CompositorMod", "DayNightMod", "DisplayMod", "DeviceMod",
        "DrmBrightnessMod", "EvdevMod", "GamepadMod", "GroupMod",
        "ImageMod", "IOMod", "KeyboardMod", "KeymapMod", "KeytapMod",
        "LCMod", "LibMpvMod", "MediaMod", "ModeMod", "ModesMod",
        "MonitorMod", "MouseMod", "MpvMod", "OpencvMod", "PixelMod",
        "ProtocolsMod", "ScreenMod", "ScreenshotMod", "SocketMod",
        "UinputMod", "WindowMod", "WinmatchMod", "WinwatchMod", "X11Mod",
        "ZoomMod", "ord", "char", "chr", "bytes", "base64", "hex",
        "hash", "uuid", "crypto", "random", "semaphore", "mutex",
        "notif", "config", "dotenv", "env", "format", "fsuv", "future",
        "html", "ini", "list", "log", "map", "number", "object", "ocr",
        "os", "parser", "path", "print", "process", "promise", "random",
        "semaphore", "sqlite", "string", "sys", "terminal", "toml",
        "yaml", "time", "sleep", "async", "await", "yield", "go",
        "defer", "try", "catch", "finally", "throw", "import", "use",
        "from", "as", "let", "const", "fn", "class", "struct", "trait",
        "impl", "protocol", "enum", "match", "when", "if", "else",
        "while", "for", "loop", "break", "continue", "return", "in",
        "out", "to", "step", "by", "do", "end", "then", "case",
        "default", "switch", "typeof", "instanceof", "is", "as",
        "null", "true", "false", "nil", "self", "super", "this",
        "base", "init", "deinit", "drop", "clone", "copy", "move",
        "ref", "mut", "const", "pub", "priv", "mod", "use", "extern",
        "inline", "static", "virtual", "override", "final", "abstract",
        "sealed", "open", "closed", "public", "private", "protected",
        "internal", "fileprivate", "package", "module", "import",
        "export", "reexport", "as", "from", "where", "if", "unless",
        "until", "while", "for", "loop", "each", "every", "some",
        "none", "find", "filter", "map", "reduce", "fold", "scan",
        "zip", "enumerate", "reverse", "sort", "sorted", "min", "max",
        "sum", "product", "avg", "mean", "median", "mode", "std",
        "var", "count", "len", "length", "size", "empty", "any", "all",
        "first", "last", "head", "tail", "init", "take", "drop",
        "skip", "limit", "slice", "chunk", "split", "join", "split",
        "trim", "upper", "lower", "capitalize", "title", "camel",
        "snake", "kebab", "pascal", "replace", "regex", "match",
        "find", "search", "contains", "startsWith", "endsWith",
        "padLeft", "padRight", "center", "ljust", "rjust", "zfill",
        "format", "printf", "sprintf", "fprintf", "println", "print",
        "eprint", "eprintln", "input", "readline", "stdin", "stdout",
        "stderr", "args", "argv", "env", "getenv", "setenv", "unsetenv",
        "exit", "abort", "panic", "unreachable", "todo", "fixme",
        "note", "warning", "deprecated", "experimental", "unstable",
        "internal", "private", "public", "protected", "internal"
      };
      // Module globals registered via setGlobal() at VM/module load time.
      // These exist as globals when a built script runs, so strict-mode
      // resolution must accept them here too. Sourced from the single
      // canonical kModuleGlobals list (validated by the module-globals
      // drift-guard test) rather than a hand-maintained copy.
      for (const char *g : havel::compiler::kModuleGlobals) {
        knownGlobals.insert(g);
      }
      compiler.setKnownGlobals(knownGlobals);
    }
    try {
      chunk = compiler.compile(*program);
    } catch (const std::exception &e) {
      error("Compile error: {}", e.what());
      return 1;
    }
    if (compiler.hasErrors()) {
      for (const auto &err : compiler.errors()) {
        std::string formatted = havel::ErrorPrinter::formatError(
            "error", err.what(), primaryFile, err.line, err.column, 1, "");
        std::cerr << formatted;
      }
      error("Build failed with {} compile error(s)", compiler.errors().size());
      return 1;
    }
  }

  if (!chunk) {
    error("Compiler returned null chunk");
    return 1;
  }

  info("Compilation successful, {} functions", chunk->getFunctionCount());

#ifdef HAVEL_ENABLE_LLVM
  // Handle AOT LLVM output
  if (cfg.emitLLVM || cfg.emitAsm || cfg.emitObj || cfg.emitWasm ||
      cfg.emitBinary || cfg.emitElf) {
    // Import LLVM JIT for translation
    std::unique_ptr<havel::compiler::BytecodeOrcJIT> jit;
    const bool wantJIT = cfg.useJIT || cfg.vmConfig.tiering_enabled ||
                         (std::getenv("HAVEL_TIERING") && std::string(std::getenv("HAVEL_TIERING")) != "0");
    if (wantJIT) {
      jit = std::make_unique<havel::compiler::BytecodeOrcJIT>();
      jit->setDebugMode(cfg.debugJIT);
      jit->setDumpIR(cfg.dumpIR);
      jit->setDumpAsmToFile(cfg.outputAsmToFile);
      jit->setShowWarnings(cfg.aotWarnings);
      jit->setLinkedLibraries(cfg.linkLibs);
      if (cfg.emitLLVM || cfg.debugJIT) {
        jit->setDumpIR(true);
      }
      const std::string normalizedOS = normalizeTargetOS(cfg.targetOS);
      if (normalizedOS == "linux") {
        jit->setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Linux);
      } else if (normalizedOS == "windows") {
        jit->setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Windows);
      } else if (normalizedOS == "macos") {
        jit->setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::MacOS);
      } else if (normalizedOS == "wasm") {
        jit->setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Wasm);
      } else {
        jit->setTargetOS(havel::compiler::BytecodeOrcJIT::TargetOS::Native);
      }

      // Generate LLVM IR for each function
      llvm::LLVMContext ctx;
      auto module = std::make_unique<llvm::Module>(primaryFile + "_module", ctx);

      // First pass: find functions with unsupported opcodes
      std::vector<bool> hasUnsupported(chunk->getFunctionCount(), false);
      bool anyUnsupported = false;
      for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
        const auto *func = chunk->getFunction(i);
        if (func && havel::compiler::BytecodeOrcJIT::hasUnsupportedOpcodes(*func)) {
          hasUnsupported[i] = true;
          anyUnsupported = true;
          if (cfg.aotWarnings) {
            warn("AOT: skipping function '{}' — contains async/concurrency opcodes "
                 "not supported in AOT",
                 func->name);
          }
        }
      }

      // Second pass: also skip functions that have CALL instructions if there
      // are any unsupported functions, because CALL is dynamic and we can't
      // guarantee the callee is also AOT-compiled.
      std::vector<bool> shouldSkip = hasUnsupported;
      if (anyUnsupported) {
        for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
          if (shouldSkip[i]) continue;
          const auto *func = chunk->getFunction(i);
          if (!func) continue;
          for (const auto& instr : func->instructions) {
            if (instr.opcode == compiler::OpCode::CALL ||
                instr.opcode == compiler::OpCode::TAIL_CALL ||
                instr.opcode == compiler::OpCode::CALL_METHOD ||
                instr.opcode == compiler::OpCode::CALL_SUPER ||
                instr.opcode == compiler::OpCode::SPREAD_CALL) {
              shouldSkip[i] = true;
              if (cfg.aotWarnings) {
                warn("AOT: skipping function '{}' — calls other functions "
                     "(dynamic dispatch may reach async code)",
                     func->name);
              }
              break;
            }
          }
        }
      }

      bool anyCompiled = false;
      bool mainCompiled = false;
      for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
        const auto *func = chunk->getFunction(i);
        bool skip = shouldSkip[i];
        if (func && !skip) {
          jit->translate(*func, *module);
          anyCompiled = true;
          if (func->name == "__main__") {
            mainCompiled = true;
          }
        }
      }

      if (!anyCompiled || !mainCompiled) {
        if (!anyCompiled) {
          warn("AOT: no functions compiled (all contain or call unsupported opcodes). "
               "Emitting dummy __main__ that exits with message.");
        } else if (!mainCompiled) {
          warn("AOT: entry point '__main__' not compiled (contains or calls unsupported opcodes). "
               "Emitting dummy executable that exits with message.");
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
      std::string aotOutput =
          cfg.outputPath.empty() ? primaryFile : cfg.outputPath;
      if (aotOutput.size() >= 3 &&
          aotOutput.rfind(".hv") == aotOutput.size() - 3) {
        aotOutput = aotOutput.substr(0, aotOutput.size() - 3);
      }
      // If the user passed an explicit -o OUT with a known artifact extension
      // (.o, .ll, .s, .wasm, .exe), strip it so aotOutput is a true base path.
      // Otherwise, every emit step (.o appended -> .o.o, .ll appended -> .ll.o)
      // double-suffixes the file. Only strip well-known extensions, never
      // arbitrary ones like "ao" or random paths.
      if (!cfg.outputPath.empty()) {
        auto endsWith = [&](const char* ext) {
          size_t n = strlen(ext);
          return aotOutput.size() > n &&
                 aotOutput.compare(aotOutput.size() - n, n, ext) == 0;
        };
        if (endsWith(".o"))      aotOutput = aotOutput.substr(0, aotOutput.size() - 2);
        else if (endsWith(".ll")) aotOutput = aotOutput.substr(0, aotOutput.size() - 3);
        else if (endsWith(".s"))  aotOutput = aotOutput.substr(0, aotOutput.size() - 2);
        else if (endsWith(".wasm")) aotOutput = aotOutput.substr(0, aotOutput.size() - 5);
        else if (endsWith(".so")) aotOutput = aotOutput.substr(0, aotOutput.size() - 3);
        else if (endsWith(".exe")) aotOutput = aotOutput.substr(0, aotOutput.size() - 4);
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

        std::string targetTripleStr =
            cfg.arch.empty()
                ? mapTargetTripleForOS(cfg.targetOS,
                                       llvm::sys::getDefaultTargetTriple())
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
        if (cfg.asmSyntax == LaunchConfig::AsmSyntax::INTEL) {
          opt.MCOptions.OutputAsmVariant = 1;
        }

        auto targetMachine =
            target->createTargetMachine(targetTriple, llvm::sys::getHostCPUName(),
                                        "", opt, llvm::Reloc::PIC_);

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
          if (targetMachine->addPassesToEmitFile(
                  pm, out, nullptr, llvm::CodeGenFileType::AssemblyFile)) {
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
          if (targetMachine->addPassesToEmitFile(
                  pm, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
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
            linkCmd =
                "clang++ -shared \"" + nativeObjPath + "\" -o \"" + soPath + "\"";
          } else if (normalizeTargetOS(cfg.targetOS) == "macos") {
            linkCmd = "clang++ -dynamiclib \"" + nativeObjPath + "\" -o \"" +
                      soPath + "\"";
          } else {
            linkCmd = "clang++ -shared -fPIC \"" + nativeObjPath + "\" -o \"" +
                      soPath + "\"";
          }
          if (!coreProfile) {
            appendLinkLibraries(linkCmd, jit->linkedLibraries());
            if (jit->linkedLibraries().empty()) {
              appendDefaultLlvmLinkLibraries(linkCmd);
            }
            appendDefaultNativeLinkLibraries(linkCmd);
          }
          int linkRc = std::system(linkCmd.c_str());
          if (linkRc != 0) {
            error("Failed to link native shared binary with command: {}",
                  linkCmd);
            return 1;
          }
          info("Native shared binary written to: {}", soPath);
        }
        
        if (cfg.emitElf) {
          const bool targetWindows = normalizeTargetOS(cfg.targetOS) == "windows";
          std::string binPath = aotOutput + (targetWindows ? ".exe" : "");
          std::string stubPath = aotOutput + "_stub.cpp";
          
          // Get build directory for module search paths
          std::string buildDir;
          std::string exePath = Env::executable();
          if (!exePath.empty()) {
              buildDir = std::filesystem::path(exePath).parent_path().string();
          }
          
          info("AOT: generating stub at {}", stubPath);
          {
            std::ofstream stub(stubPath);
            if (!stub) {
              error("Failed to open stub file for writing: {}", stubPath);
              return 1;
            }
            const std::string initSymbol = coreProfile
                                               ? "havel_vm_init_standalone_core"
                                               : "havel_vm_init_standalone";
            const std::string initWithFuncsSymbol = coreProfile
                                               ? "havel_vm_init_standalone_with_functions_core"
                                               : "havel_vm_init_standalone_with_functions";
            if (mainCompiled) {
              // Collect function metadata for closures.
              // IMPORTANT: Include ALL functions (even skipped ones) to preserve
              // original function indices used by CLOSURE opcode.
              std::vector<std::string> funcNames;
              std::vector<uint32_t> funcParamCounts;
              std::vector<uint32_t> funcLocalCounts;
              std::vector<uint32_t> funcUpvalueCounts;
              std::vector<uint32_t> funcIsGenerator;
              std::vector<uint32_t> upvalueIndices;
              std::vector<uint32_t> upvalueCapturesLocal;
              
              for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
                const auto* func = chunk->getFunction(i);
                if (func) {
                  funcNames.push_back(func->name);
                  funcParamCounts.push_back(func->param_count);
                  funcLocalCounts.push_back(func->local_count);
                  funcUpvalueCounts.push_back(static_cast<uint32_t>(func->upvalues.size()));
                  funcIsGenerator.push_back(func->is_generator ? 1 : 0);
                  for (const auto& uv : func->upvalues) {
                    upvalueIndices.push_back(uv.index);
                    upvalueCapturesLocal.push_back(uv.captures_local ? 1 : 0);
                  }
                } else {
                  // Empty placeholder for null function (shouldn't happen)
                  funcNames.push_back("");
                  funcParamCounts.push_back(0);
                  funcLocalCounts.push_back(0);
                  funcUpvalueCounts.push_back(0);
                  funcIsGenerator.push_back(0);
                }
              }
              
              // Generate arrays for function metadata
              stub << "#include <cstdint>\n";
              stub << "extern \"C\" uint64_t __main__(void*, uint64_t*, uint32_t);\n";
              stub << "extern \"C\" void* " << initWithFuncsSymbol << "(\n";
              stub << "    const char**, uint32_t,\n";
              stub << "    const char**, uint32_t,\n";
              stub << "    const uint32_t*, const uint32_t*,\n";
              stub << "    const uint32_t*, const uint32_t*,\n";
              stub << "    const uint32_t*, const uint32_t*, uint32_t,\n";
              stub << "    const uint32_t*, const uint64_t*,\n";
              stub << "    const uint32_t*, const uint64_t*, uint32_t,\n";
              stub << "    const char*);\n";
              stub << "int main() {\n";
              stub << "    const char* strings[] = {\n";
              const auto &chunkStrings = chunk->getAllStrings();
              for (size_t i = 0; i < chunkStrings.size(); ++i) {
                const std::string &s = chunkStrings[i];
                std::string escaped;
                for (char c : s) {
                  if (c == '"') escaped += "\\\"";
                  else if (c == '\\') escaped += "\\\\";
                  else if (c == '\n') escaped += "\\n";
                  else escaped += c;
                }
                stub << "        \"" << escaped << "\",\n";
              }
              stub << "    };\n";
              
              // Function names
              stub << "    const char* func_names[] = {\n";
              for (size_t i = 0; i < funcNames.size(); ++i) {
                std::string escaped = funcNames[i];
                for (char& c : escaped) {
                  if (c == '"') c = '\\'; // strings don't have quotes in names
                }
                stub << "        \"" << escaped << "\",\n";
              }
              stub << "    };\n";
              
              // Function param counts
              stub << "    const uint32_t func_param_counts[] = {\n";
              for (size_t i = 0; i < funcParamCounts.size(); ++i) {
                stub << "        " << funcParamCounts[i] << ",\n";
              }
              stub << "    };\n";
              
              // Function local counts
              stub << "    const uint32_t func_local_counts[] = {\n";
              for (size_t i = 0; i < funcLocalCounts.size(); ++i) {
                stub << "        " << funcLocalCounts[i] << ",\n";
              }
              stub << "    };\n";
              
              // Function upvalue counts
              stub << "    const uint32_t func_upvalue_counts[] = {\n";
              for (size_t i = 0; i < funcUpvalueCounts.size(); ++i) {
                stub << "        " << funcUpvalueCounts[i] << ",\n";
              }
              stub << "    };\n";
              
              // Function is_generator
              stub << "    const uint32_t func_is_generator[] = {\n";
              for (size_t i = 0; i < funcIsGenerator.size(); ++i) {
                stub << "        " << funcIsGenerator[i] << ",\n";
              }
              stub << "    };\n";
              
              // Upvalue indices
              stub << "    const uint32_t upvalue_indices[] = {\n";
              for (size_t i = 0; i < upvalueIndices.size(); ++i) {
                stub << "        " << upvalueIndices[i] << ",\n";
              }
              stub << "    };\n";
              
              // Upvalue captures_local
              stub << "    const uint32_t upvalue_captures_local[] = {\n";
              for (size_t i = 0; i < upvalueCapturesLocal.size(); ++i) {
                stub << "        " << upvalueCapturesLocal[i] << ",\n";
              }
              stub << "    };\n";
              
              // Function constants - serialize for interpreter fallback
              stub << "    // Function constants (serialized)\n";
              stub << "    const uint32_t func_const_counts[] = {\n";
              for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
                const auto* func = chunk->getFunction(i);
                uint32_t count = func ? static_cast<uint32_t>(func->constants.size()) : 0;
                stub << "        " << count << ",\n";
              }
              stub << "    };\n";
              
              // Flattened constant data: raw bits for each constant
              stub << "    const uint64_t func_const_data[] = {\n";
              for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
                const auto* func = chunk->getFunction(i);
                if (func) {
                  for (const auto& c : func->constants) {
                    stub << "        " << c.rawBits() << ",\n";
                  }
                }
              }
              stub << "    };\n";
              
              // Function instructions - serialize for interpreter fallback
              stub << "    // Function instructions (serialized)\n";
              stub << "    const uint32_t func_instr_counts[] = {\n";
              for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
                const auto* func = chunk->getFunction(i);
                uint32_t count = func ? static_cast<uint32_t>(func->instructions.size()) : 0;
                stub << "        " << count << ",\n";
              }
              stub << "    };\n";
              
              // Flattened instruction data: [opcode, num_operands, operand1, operand2, ...] for each instruction
              stub << "    const uint64_t func_instr_data[] = {\n";
              for (size_t i = 0; i < chunk->getFunctionCount(); ++i) {
                const auto* func = chunk->getFunction(i);
                if (func) {
                  for (const auto& instr : func->instructions) {
                    stub << "        " << static_cast<uint32_t>(instr.opcode) << ",\n";
                    stub << "        " << instr.operands.size() << ",\n";
                    for (const auto& op : instr.operands) {
                      stub << "        " << op.rawBits() << ",\n";
                    }
                  }
                }
              }
              stub << "    };\n";
              
              std::string escapedBuildDir = buildDir;
              for (char& c : escapedBuildDir) {
                  if (c == '"') escapedBuildDir += '\\';
                  else if (c == '\\') escapedBuildDir += '\\\\';
              }
              
              stub << "    void* vm = " << initWithFuncsSymbol << "(strings, "
                   << chunkStrings.size() << ",\n";
              stub << "        func_names, " << funcNames.size() << ",\n";
              stub << "        func_param_counts, func_local_counts,\n";
              stub << "        func_upvalue_counts, func_is_generator,\n";
              stub << "        upvalue_indices, upvalue_captures_local, "
                   << upvalueIndices.size() << ",\n";
              stub << "        func_const_counts, func_const_data,\n";
              stub << "        func_instr_counts, func_instr_data, "
                   << chunk->getFunctionCount() << ",\n";
              stub << "        \"" << escapedBuildDir << "\");\n";
              stub << "    uint64_t dummy_args[1024];\n";
              stub << "    for(int i=0; i<1024; ++i) dummy_args[i] = "
                      "0x7ffb000000000000ULL;\n";
              stub << "    __main__(vm, dummy_args, 0);\n";
              stub << "    return 0;\n";
              stub << "}\n";
            } else {
              // Entry point not compiled — emit a dummy main that prints message
              stub
                  << "#include <cstdio>\n"
                  << "int main() {\n"
                  << "    fprintf(stderr, \"AOT: entry point '__main__' not compiled (contains or calls unsupported opcodes). Run with interpreter.\\n\");\n"
                  << "    return 0;\n"
                  << "}\n";
            }
          }

          std::string linkCmd;
          if (targetWindows) {
            linkCmd = "clang++ \"" + stubPath + "\" \"" + nativeObjPath +
                      "\" -o \"" + binPath + "\"";
          } else {
            linkCmd = "clang++ -flto -fuse-ld=lld \"" + stubPath + "\" \"" +
                      nativeObjPath + "\" -o \"" + binPath + "\" -Wl,--export-dynamic";
            std::string exePath = Env::executable();
            if (!exePath.empty()) {
              std::string libDir =
                  std::filesystem::path(exePath).parent_path().string();
              linkCmd += " -L\"" + libDir + "\"";
            }
            if (coreProfile) {
              linkCmd += " -lhavel_aot_core_shim -lhavel_lang_core";
            } else {
              linkCmd += " -lhavel_lang -lhavel_core";
              // havel_modules is a static archive only when module plugins
              // are disabled (CMakeLists.txt:1057-1060). With
              // ENABLE_MODULE_PLUGINS=ON, havel_modules is an INTERFACE
              // target — no libhavel_modules.a archive is produced. Linking
              // against the missing archive fails the entire
              // --full-aot executable step.
#ifndef ENABLE_MODULE_PLUGINS
              linkCmd += " -lhavel_modules";
#endif
#if defined(ENABLE_QT_UI_BACKEND) || defined(HAVE_QT_EXTENSION)
              linkCmd += " -lhavel_gui";
#endif
            }
          }
          appendLinkLibraries(linkCmd, jit->linkedLibraries());
          if (jit->linkedLibraries().empty()) {
            appendDefaultLlvmLinkLibraries(linkCmd);
          }
          appendDefaultNativeLinkLibraries(linkCmd);
          int linkRc = std::system(linkCmd.c_str());
          if (linkRc != 0) {
            error("Failed to link native AOT executable with command: {}",
                  linkCmd);
            // std::filesystem::remove(stubPath);
            return 1;
          }
          info("Native AOT executable written to: {}", binPath);
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
        if (targetMachine->addPassesToEmitFile(
                pm, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
          error("Target '{}' cannot emit WebAssembly object", targetTripleStr);
          return 1;
        }
        pm.run(*module);
        info("WebAssembly binary written to: {}", wasmPath);
      }

      return 0;
    }
  }
#else
    if (cfg.emitLLVM || cfg.emitAsm || cfg.emitObj || cfg.emitWasm ||
        cfg.emitBinary) {
      error("AOT compilation requires LLVM support. Rebuild with ENABLE_LLVM=ON");
      return 1;
    }
#endif

    // Serialize and write bytecode
    havel::compiler::ValueSerializer serializer;
    auto data = serializer.serializeChunk(*chunk);

    info("Serialization complete, {} bytes", data.size());

    auto writeAtomically = [&](const std::string& targetPath, const std::vector<uint8_t>& bytes) -> bool {
        std::string tempPath = targetPath + ".tmp." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::ofstream outFile(tempPath, std::ios::binary);
        if (!outFile.is_open()) {
            error("Cannot open temporary output file: {}", tempPath);
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!outFile.good()) {
            error("Failed to write output file: {}", tempPath);
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        outFile.close();
        
        std::error_code ec;
        std::filesystem::rename(tempPath, outputPath, ec);
        if (ec) {
            // Cross-device rename failed, fall back to copy + remove
            if (ec == std::errc::cross_device_link) {
                std::ifstream src(tempPath, std::ios::binary);
                std::ofstream dst(targetPath, std::ios::binary);
                if (src && dst) {
                    dst << src.rdbuf();
                    if (!dst.good()) {
                        error("Failed to copy output file: {}", targetPath);
                        std::filesystem::remove(tempPath, ec);
                        return false;
                    }
                    dst.close();
                } else {
                    error("Failed to copy output file: {}", targetPath);
                    std::filesystem::remove(tempPath, ec);
                    return false;
                }
                std::filesystem::remove(tempPath, ec);
            } else {
                error("Failed to rename temporary file to output: {}", ec.message());
                std::filesystem::remove(tempPath, ec);
                return false;
            }
        }
        return true;
    };

    if (!writeAtomically(outputPath, data)) {
        return 1;
    }

    const auto writeCompanionCache = [&](const std::string &path) {
        if (path.empty()) {
            return;
        }
        std::string cachePath = companionCachePath(path);
        if (cachePath == outputPath) {
            return; // already written as the build output
        }
        if (!writeAtomically(cachePath, data)) {
            warn("Failed to write bytecode cache file: {}", cachePath);
            return;
        }
        info("Bytecode cache written to: {}", cachePath);
    };

    writeCompanionCache(primaryFile);

    info("Build successful: {} ({} bytes)", outputPath, data.size());
    return 0;
  }

int HavelLauncher::diffPipeline(const havel::init::LaunchConfig &cfg) {
  namespace fs = std::filesystem;
  std::string currentPath = cfg.vmConfig.self_hosted_modules_path;
  if (currentPath.empty()) {
    try {
      auto exePath = fs::read_symlink("/proc/self/exe");
      currentPath = (exePath.parent_path().parent_path() / "out").string();
    } catch (...) {
      currentPath = "./out";
    }
  }
  const std::string &baselinePath = cfg.diffPipelinePath;

  auto collectHvc =
      [](const fs::path &dir) -> std::map<std::string, std::vector<uint8_t>> {
    std::map<std::string, std::vector<uint8_t>> files;
    if (!fs::exists(dir))
      return files;
    for (auto &entry : fs::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (ext != ".hvc")
        continue;
      auto rel = fs::relative(entry.path(), dir).string();
      std::ifstream f(entry.path(), std::ios::binary);
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
      files[rel] = std::move(data);
    }
    return files;
  };

  info("Diffing pipeline: current=out({}), baseline={}", currentPath,
       baselinePath);
  auto current = collectHvc(currentPath);
  auto baseline = collectHvc(baselinePath);

  int added = 0, removed = 0, changed = 0, unchanged = 0;

  for (auto &[name, data] : current) {
    auto it = baseline.find(name);
    if (it == baseline.end()) {
      info("+ {}", name);
      added++;
    } else if (data != it->second) {
      info("~ {}", name);
      changed++;
    } else {
      unchanged++;
    }
  }
  for (auto &[name, data] : baseline) {
    if (current.find(name) == current.end()) {
      info("- {}", name);
      removed++;
    }
  }

  info("Pipeline diff: {} added, {} removed, {} changed, {} unchanged", added,
       removed, changed, unchanged);
  return (added || removed || changed) ? 1 : 0;
}

// DEBUG
// #include <iostream>  // Moved to top of file

} // namespace havel::init
