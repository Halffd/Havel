#pragma once
#include "qt.hpp"
#include <string>

namespace havel::init {

class HavelLauncher {
public:
  enum class Mode {
    DAEMON,          // Full system with hotkeys + GUI (default)
    SCRIPT,          // Execute .hv script file with full IO
    SCRIPT_ONLY,     // Execute .hv script without IO/hotkeys (pure testing)
    REPL,            // Interactive REPL
    SCRIPT_AND_REPL, // Execute script then enter REPL
    CLI              // Command-line tools
  };

  int run(int argc, char *argv[]);

private:
  struct LaunchConfig {
    Mode mode = Mode::DAEMON;
    std::string scriptFile;
    bool isStartup = false;
    bool debugMode = false;
    bool debugParser = false;
    bool debugAst = false;
    bool debugLexer = false;
    bool debugBytecode = false;
    bool stopOnError = false; // Stop on first error/warning
    bool fullRepl = false; // Full REPL with all features (hotkeys, GUI, etc.)
  };

  LaunchConfig parseArgs(int argc, char *argv[]);
  int runDaemon(const LaunchConfig &cfg, int argc, char *argv[]);
  int runGuiOnly(const LaunchConfig &cfg, int argc, char *argv[]);
  int runScript(const LaunchConfig &cfg, int argc, char *argv[]);
  int runScriptOnly(const LaunchConfig &cfg, int argc,
                    char *argv[]); // Pure script execution without IO
  int runScriptAndRepl(const LaunchConfig &cfg, int argc, char *argv[]);
  int runRepl(const LaunchConfig &cfg);
  int runCli(int argc, char *argv[]);
  void showHelp();
};

} // namespace havel::init