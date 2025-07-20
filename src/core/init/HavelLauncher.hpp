#pragma once
#include "qt.hpp"
namespace havel::init {

class HavelLauncher {
public:
    enum class Mode {
        DAEMON,      // Full system with hotkeys + GUI
        GUI_ONLY,    // Just GUI tools
        COMPILER,    // Havel-lang compiler
        INTERPRETER, // Havel-lang interpreter
        CLI          // Command-line tools
    };
    
    int run(int argc, char* argv[]);
    
private:
    Mode parseMode(int argc, char* argv[]);
    int runDaemon(int argc, char* argv[]);
    int runGuiOnly(int argc, char* argv[]);
    int runCompiler(int argc, char* argv[]);
    int runInterpreter(int argc, char* argv[]);
    int runCli(int argc, char* argv[]);
    static int compileFiles();
    void showHelp();
};

} // namespace havel::init