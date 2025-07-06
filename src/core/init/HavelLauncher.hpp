// src/HavelLauncher.hpp
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
        
        int run();
        
    private:
        Mode parseMode(int argc, char* argv[]);
        int runDaemon();
        int runGuiOnly();
        int runCompiler();
        int runInterpreter();
        int runCli();
    };
}