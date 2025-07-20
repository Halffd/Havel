#pragma once
#include <memory>
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"
#include "core/HotkeyManager.hpp"
#include "gui/AutomationSuite.hpp"
#include "runtime/Engine.h"
#include "runtime/Interpreter.hpp"

namespace havel {
// src/core/HavelCore.hpp
class HavelCore {
    public:
        // Singleton pattern for core services
        static HavelCore& instance();
        
        // Service accessors
        havel::IO& getIO() { return *io; }
        havel::WindowManager& getWindowManager() { return *windowManager; }
        
        // Component management
        void initializeSystem();
        void initializeGUI();
        void initializeHotkeys();
        void initializeCompiler();
        
        void shutdown();
        
    private:
        // Core services (always available)
        std::unique_ptr<havel::IO> io;
        std::unique_ptr<havel::WindowManager> windowManager;
        
        // Optional components
        std::unique_ptr<havel::HotkeyManager> hotkeyManager;
        std::unique_ptr<havel::engine::Engine> compilerEngine;
        std::unique_ptr<havel::Interpreter> interpreter;
    };
}