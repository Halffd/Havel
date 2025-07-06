#pragma once
#include <memory>
#include "IO.hpp"
#include "window/WindowManager.hpp"
#include "ConfigManager.hpp"
#include "Logger.hpp"
#include "HotkeyManager.hpp"
#include "gui/AutomationSuite.hpp"
#include "compiler/Engine.hpp"
#include "runtime/Interpreter.hpp"
// src/core/HavelCore.hpp
class HavelCore {
    public:
        // Singleton pattern for core services
        static HavelCore& instance();
        
        // Service accessors
        IO& getIO() { return *io; }
        WindowManager& getWindowManager() { return *windowManager; }
        ConfigManager& getConfigManager() { return *configManager; }
        
        // Component management
        void initializeSystem();
        void initializeGUI();
        void initializeHotkeys();
        void initializeCompiler();
        
        void shutdown();
        
    private:
        // Core services (always available)
        std::unique_ptr<IO> io;
        std::unique_ptr<WindowManager> windowManager;
        std::unique_ptr<ConfigManager> configManager;
        std::unique_ptr<Logger> logger;
        
        // Optional components
        std::unique_ptr<HotkeyManager> hotkeyManager;
        std::unique_ptr<AutomationSuite> automationSuite;
        std::unique_ptr<compiler::Engine> compilerEngine;
        std::unique_ptr<runtime::Interpreter> interpreter;
    };