#include "HavelCore.hpp"
#include "utils/Logger.hpp"  
#include "window/WindowManager.hpp"
#include "core/HotkeyManager.hpp"
#include "runtime/Engine.h"
#include "runtime/Interpreter.hpp"
namespace havel {

HavelCore& HavelCore::instance() {
    static HavelCore instance;
    return instance;
}

void HavelCore::initializeSystem() {
    // Initialize core services
    io = std::make_unique<IO>();
    windowManager = std::make_unique<WindowManager>();
}

void HavelCore::initializeGUI() {
    if (!windowManager) {
        error("Window manager not initialized");
        return;
    }
    info("GUI components initialized");
}

void HavelCore::initializeHotkeys() {
    
    
    //hotkeyManager = std::make_unique<HotkeyManager>(io, windowManager, mpv, scriptEngine);
    hotkeyManager->RegisterDefaultHotkeys();
    
    info("Hotkey system initialized");
}

void HavelCore::initializeCompiler() {
    compilerEngine = std::make_unique<engine::Engine>();
    interpreter = std::make_unique<Interpreter>();
    
    // Initialize compiler components
    interpreter->InitializeStandardLibrary();
    
    info("Compiler and interpreter initialized");
}

void HavelCore::shutdown() {
    info("Shutting down HavelCore...");
    
    // Shutdown in reverse order of initialization
    if (hotkeyManager) {
        hotkeyManager.reset();
    }
    
    if (windowManager) {
        windowManager.reset();
    }
    
    if (compilerEngine) {
        compilerEngine.reset();
    }
    
    if (interpreter) {
        interpreter.reset();
    }
    
    
    
    if (io) {
        io.reset();
    }
    
    info("HavelCore shutdown complete");
}

} // namespace havel
