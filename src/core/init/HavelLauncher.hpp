#pragma once

#ifdef HAVE_QT_EXTENSION
#include "qt.hpp"
#endif

#include <string>
#include <vector>
#include <memory>

namespace havel::init {

class HavelLauncher {
public:
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
#ifdef HAVE_QT_EXTENSION
  std::unique_ptr<QApplication> app_;
#endif
  struct LaunchConfig {
    Mode mode = Mode::DAEMON;
    std::vector<std::string> scriptFiles;
    bool isStartup = false;
    bool debugMode = false;
    bool debugParser = false;
    bool debugAst = false;
    bool debugLexer = false;
    bool debugBytecode = false;
    bool diffBytecode = false;  // Compare bytecode with previous run
    bool stopOnError = false; // Stop on first error/warning
    bool fullRepl = false; // Full REPL with all features (hotkeys, GUI, etc.)
    bool minimalMode = false; // Minimal mode - no IO/hotkeys/GUI
    bool lintOnly = false; // Only lint the script and check for errors
    bool buildOnly = false; // Compile to bytecode only
    std::string outputPath; // Output path for --build (-o)
    std::string testDir;   // Directory containing test scripts
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
  void showHelp();
};

} // namespace havel::init