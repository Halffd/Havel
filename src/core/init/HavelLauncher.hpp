#pragma once

#include "havel-lang/compiler/vm/VM.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include "../../host/ServiceRegistry.hpp"

namespace havel::init {

struct LaunchConfig {
  enum class Mode {
    DAEMON,
    SCRIPT,
    SCRIPT_ONLY,
    REPL,
    SCRIPT_AND_REPL,
    CLI,
    TEST
  };

  enum class Target {
    INTERPRET,
    JIT,
    AOT,
    ASM,
    IR,
    WASM,
    ELF,
    BIN
  };

  enum class AsmSyntax {
    ATT,
    INTEL
  };

  Mode mode = Mode::DAEMON;
  std::vector<std::string> scriptFiles;
  std::vector<std::string> scriptArgs;
  bool isStartup = false;
  bool debugMode = false;
  bool debugParser = false;
  bool debugAst = false;
  bool debugLexer = false;
  bool debugBytecode = false;
  bool debugGc = false;
  bool debugEngine = false;
bool debugIo = false;
  bool debugHotkeys = false;
  bool diffBytecode = false;
  bool stopOnError = false;
  bool fullRepl = false;
  bool minimalMode = false;
  bool pureStdlib = false;
  bool lintOnly = false;
  bool buildOnly = false;
  bool use_cpp_modules = false; // true to use C++ built-in modules, false to use self-hosted modules if available
  std::string outputPath;
  std::string outputLogFile;
  std::string historyFile;
  std::string testDir;
  int testTimeout = 30;
  bool useJIT = true;
  bool debugJIT = false;
  bool dumpIR = false;
  bool outputAsmToFile = false;
  Target target = Target::INTERPRET;
  bool emitLLVM = false;
  bool emitAsm = false;
  bool emitObj = false;
  bool emitWasm = false;
  bool emitBinary = false;
  bool emitElf = false;
  bool fullAot = false;
  bool aotWarnings = true;
  std::string aotOutput;
  std::string arch;
  std::string targetOS;
  std::string profile = "full";
  std::vector<std::string> linkLibs;
  AsmSyntax asmSyntax = AsmSyntax::ATT;
  std::string evalString;
  std::string inputBackend;
  compiler::VMConfig vmConfig;
  host::ServiceFilter serviceIncludes;
  host::ServiceFilter serviceExcludes;
  bool listServices = false;
};

class RunStrategy {
public:
  virtual ~RunStrategy() = default;
  virtual int execute(const LaunchConfig &cfg, int argc, char *argv[]) = 0;
};

class HavelLauncher {
public:
   int run(int argc, char *argv[]);
   void setSelfHostedConfig(bool useCpp, const std::string& selfHostedPath) {
      use_cpp_modules_config_ = useCpp;
      self_hosted_modules_path_config_ = selfHostedPath;
   }

private:
   LaunchConfig parseArgs(int argc, char *argv[]);
   std::unique_ptr<RunStrategy> createStrategy(const LaunchConfig &cfg);
   void showHelp();
   int runBuild(const LaunchConfig &cfg);
   int runBytecodeFiles(const LaunchConfig &cfg,
                        const std::vector<std::string> &hvcFiles);

   bool use_cpp_modules_config_ = false;
   std::string self_hosted_modules_path_config_;
};

} // namespace havel::init
