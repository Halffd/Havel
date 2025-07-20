#include "HavelLauncher.hpp"
#include "HavelCore.hpp"
#include "qt.hpp"
// Qt includes
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtCore/QDebug>

// Standard includes
#include <iostream>
#include <string>

namespace havel::init {

int HavelLauncher::run(int argc, char* argv[]) {
    try {
        // Create a temporary QCoreApplication for command line parsing
        QCoreApplication tmpApp(argc, argv);
        
        Mode mode = parseMode(argc, argv);
        
        // Process the actual application based on mode
        switch(mode) {
            case Mode::DAEMON:
                return runDaemon(argc, argv);
            case Mode::GUI_ONLY:
                return runGuiOnly(argc, argv);
            case Mode::COMPILER:
                return runCompiler(argc, argv);
            case Mode::INTERPRETER:
                return runInterpreter(argc, argv);
            case Mode::CLI:
                return runCli(argc, argv);
            default:
                std::cerr << "Unknown mode" << std::endl;
                return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

int HavelLauncher::runDaemon(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("Havel Daemon");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeHotkeys();
    core.initializeGUI();
    
    std::cout << "Havel Daemon started" << std::endl;
    std::cout << "Havel v" << QCoreApplication::applicationVersion().toStdString() << std::endl;
    
    return app.exec();
}

int HavelLauncher::runGuiOnly(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Havel GUI");
    
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeGUI();
    
    qDebug() << "Havel GUI started";
    return app.exec();
}

int HavelLauncher::runCompiler(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("Havel Compiler");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Havel Compiler");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("source", "Source file(s) to compile");
    
    // Add custom options
    QCommandLineOption outputOption(QStringList() << "o" << "output", 
                                  "Output file", "output");
    parser.addOption(outputOption);
    
    // Process the actual command line arguments given by the user
    parser.process(app);
    
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        std::cerr << "Error: No input files specified" << std::endl;
        parser.showHelp(1);
    }
    
    HavelCore& core = HavelCore::instance();
    core.initializeSystem();
    core.initializeCompiler();
    
    std::cout << "Compiling: " << args.first().toStdString() << std::endl;
    
    // If we have an output option, use it
    if (parser.isSet(outputOption)) {
        QString outputFile = parser.value(outputOption);
        std::cout << "Output file: " << outputFile.toStdString() << std::endl;
    }
    
    return HavelLauncher::compileFiles();
}

int havel::init::HavelLauncher::compileFiles() {
    std::cout << "Compiling files..." << std::endl;
    // Placeholder for compileFiles implementation
    return 0;
}

int havel::init::HavelLauncher::runInterpreter(int argc, char* argv[]) {
    // Placeholder for runInterpreter implementation
    return 0;
}

int havel::init::HavelLauncher::runCli(int argc, char* argv[]) {
    // Placeholder for runCli implementation
    return 0;
}

HavelLauncher::Mode havel::init::HavelLauncher::parseMode(int argc, char* argv[]) {
    // Placeholder for parseMode implementation
    return Mode::CLI;
}

}