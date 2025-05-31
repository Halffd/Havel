#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <functional>
#include <sol/sol.hpp>

namespace havel {
    // Forward declarations to avoid circular dependencies
    class IO;
    class WindowManager;

    class ScriptEngine {
    public:
        ScriptEngine(IO& io, WindowManager& windowManager);
        ~ScriptEngine() = default;

        // Script loading and execution
        bool LoadScript(const std::string& filename);
        bool ExecuteString(const std::string& code);
        
        // AHK-style hotkey registration
        bool AddHotkey(const std::string& hotkeyStr, const std::string& action);
        bool AddHotkey(const std::string& hotkeyStr, std::function<void()> callback);
        bool AddContextualHotkey(const std::string& hotkeyStr, const std::string& context, const std::string& action);

    private:
        // Lua environment
        sol::state lua;
        IO& io;
        WindowManager& windowManager;
        
        // API binding
        void RegisterFunctions();
        void ParseHotkeyString(const std::string& hotkeyStr, std::string& key, std::string& modifiers);
        std::string ConvertAHKModifiers(const std::string& modifiers);
        bool CheckContext(const std::string& context);
    };
} 