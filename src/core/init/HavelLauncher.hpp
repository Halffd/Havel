#pragma once

#include <string>
#include <vector>
#include <memory>

namespace havel::init {

class HavelLauncher {
public:
  enum class Target {
    INTERPRET, // Bytecode VM interpreter
    JIT,       // LLVM JIT for hot functions
    AOT,       // Native binary artifact (.so)
    ASM,       // Native assembly (.s)
    IR,        // LLVM IR (.ll)
    WASM,      // WebAssembly binary (.wasm)
    ELF,       // Standalone ELF executable
    BIN        // Synonym for ELF
  };

  enum class AsmSyntax {
    ATT,
    INTEL
  };

  enum class Mode {
    DAEMON,          // Full system with hotkeys + GUI (default)
    SCRIPT,          // Execute .hv script file with full IO
    SCRIPT_ONLY,     // Execute .hv script without IO/hotkeys (pure testing)
    REPL,            // Interactive REPL
    SCRIPT_AND_REPL, // Execute script then enter REPL
    CLI,             // Command-line tools
    TEST             // Run all .hv scripts in a directory
  };

  int run(int argc, char *argv[]);

private:
  struct LaunchConfig {
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
		bool diffBytecode = false; // Compare bytecode with previous run
		bool stopOnError = false; // Stop on first error/warning
		bool fullRepl = false; // Full REPL with all features (hotkeys, GUI, etc.)
    bool minimalMode = false; // Minimal mode - no IO/hotkeys/GUI
        bool pureStdlib = false; // Load full pure stdlib (not just core) in minimal mode
        bool selfHosted = false; // Run via pure Havel pipeline (launcher.hv) instead of C++
    bool lintOnly = false; // Only lint the script and check for errors
		bool buildOnly = false; // Compile to bytecode only
		std::string outputPath; // Output path for --build (-o)
		std::string outputLogFile; // Output log file for REPL
		std::string historyFile;   // History file for REPL
		std::string testDir; // Directory containing test scripts
		int testTimeout = 30; // Timeout for each test in seconds
		bool useJIT = true;
		bool debugJIT = false;
		bool dumpIR = false;
		bool outputAsmToFile = false;
		Target target = Target::INTERPRET;

		// AOT compilation options
		bool emitLLVM = false; // --emit-llvm: output .ll file
		bool emitAsm = false; // --emit-asm: output .s assembly file
		bool emitObj = false; // --emit-obj: output .o object file
		bool emitWasm = false; // --target wasm: output .wasm
		bool emitBinary = false; // --target aot: output native .so
		bool emitElf = false; // --target elf/bin: output native ELF executable
		bool fullAot = false; // --full-aot: emit all native artifacts
		bool aotWarnings = true; // --aot-warnings / --no-aot-warnings
		std::string aotOutput; // -o for AOT output path
		std::string arch; // --arch: target triple
		std::string targetOS; // --os: linux|windows|macos|wasm|native
		std::string profile = "full"; // --profile: full|core
		std::vector<std::string> linkLibs; // --link-lib repeated
		AsmSyntax asmSyntax = AsmSyntax::ATT; // --syntax: assembly syntax (att/intel)
		std::string evalString; // --eval/-E: run inline code
		std::string inputBackend; // Input backend: "evdev", "x11", "wayland", "auto"
	};

  LaunchConfig parseArgs(int argc, char *argv[]);
  int runDaemon(const LaunchConfig &cfg, int argc, char *argv[]);
  int runGuiOnly(const LaunchConfig &cfg, int argc, char *argv[]);
  int runScript(const LaunchConfig &cfg, int argc, char *argv[]);
  int runScriptOnly(const LaunchConfig &cfg, int argc,
                    char *argv[]); // Pure script execution without IO
  int runBytecodeFiles(const LaunchConfig &cfg,
                       const std::vector<std::string> &hvcFiles);
  int runScriptAndRepl(const LaunchConfig &cfg, int argc, char *argv[]);
  int runRepl(const LaunchConfig &cfg);
  int runCli(int argc, char *argv[]);
  int runTest(const LaunchConfig &cfg);
    int runBuild(const LaunchConfig &cfg);
    int runSelfHosted(const LaunchConfig &cfg);
  void showHelp();
};

} // namespace havel::init
