#include "HavelLauncher.hpp"
#include <QApplication>
#include "HavelCore.hpp"
namespace havel::init {
// src/HavelLauncher.cpp
int HavelLauncher::run() {
    Mode mode = parseMode(argc, argv);
    
    switch(mode) {
        case Mode::DAEMON:
            return runDaemon();
        case Mode::GUI_ONLY:
            return runGuiOnly();
        case Mode::COMPILER:
            return runCompiler();
        case Mode::INTERPRETER:
            return runInterpreter();
        case Mode::CLI:
            return runCli();
    }
}

int HavelLauncher::runDaemon() {
    QApplication app(argc, argv);
    
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeHotkeys();
    core.initializeGUI();
    
    // Run Qt event loop
    return app.exec();
}

int HavelLauncher::runGuiOnly() {
    QApplication app(argc, argv);
    
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeGUI();
    
    // No hotkeys, just GUI tools
    return app.exec();
}

int HavelLauncher::runCompiler() {
    // No Qt needed for compiler
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeCompiler();
    
    // Parse command line args and compile
    return compileFiles();
}
}
