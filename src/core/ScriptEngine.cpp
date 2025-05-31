#include "ScriptEngine.hpp"
#include "IO.hpp"
#include "../window/WindowManager.hpp"
#include "../common/types.hpp"
#include <regex>
#include <fmt/format.h>
#include "../window/WindowManagerDetector.hpp"

namespace havel {

ScriptEngine::ScriptEngine(IO& io, WindowManager& windowManager)
    : io(io), windowManager(windowManager) {
    // Initialize Lua
    lua.open_libraries(
        sol::lib::base,
        sol::lib::string,
        sol::lib::math,
        sol::lib::table,
        sol::lib::io,
        sol::lib::os
    );
    
    // Register functions
    RegisterFunctions();
}

void ScriptEngine::RegisterFunctions() {
    // Register AddHotkey function
    lua.set_function("AddHotkey", [this](const std::string& hotkeyStr, const std::string& action) {
        return this->AddHotkey(hotkeyStr, action);
    });
    
    // Register other functions
    // ...
}

bool ScriptEngine::LoadScript(const std::string& filename) {
    try {
        lua.script_file(filename);
        return true;
    } catch (const sol::error& e) {
        std::cerr << "Error loading script: " << e.what() << std::endl;
        return false;
    }
}

bool ScriptEngine::ExecuteString(const std::string& code) {
    try {
        lua.script(code);
        return true;
    } catch (const sol::error& e) {
        std::cerr << "Error executing code: " << e.what() << std::endl;
        return false;
    }
}

bool ScriptEngine::AddHotkey(const std::string& hotkeyStr, const std::string& action) {
    // Parse the hotkey string (e.g., "Ctrl+Shift+A")
    std::string key, modifiers;
    ParseHotkeyString(hotkeyStr, key, modifiers);
    
    if (!key.empty()) {
        std::string convertedModifiers = ConvertAHKModifiers(modifiers);
        
        // Use the IO's Hotkey method instead of AddHotkey
        return io.Hotkey(hotkeyStr, [this, action]() {
            this->ExecuteString(action);
        });
    }
    return false;
}

bool ScriptEngine::AddHotkey(const std::string& hotkeyStr, std::function<void()> callback) {
    // Parse the hotkey string (e.g., "Ctrl+Shift+A")
    std::string key, modifiers;
    ParseHotkeyString(hotkeyStr, key, modifiers);
    
    if (!key.empty()) {
        std::string convertedModifiers = ConvertAHKModifiers(modifiers);
        
        // Use the IO's Hotkey method
        return io.Hotkey(hotkeyStr, callback);
    }
    return false;
}

bool ScriptEngine::AddContextualHotkey(const std::string& hotkeyStr, const std::string& context, const std::string& action) {
    // Parse the hotkey string (e.g., "Ctrl+Shift+A")
    std::string key, modifiers;
    ParseHotkeyString(hotkeyStr, key, modifiers);
    
    if (!key.empty()) {
        std::string convertedModifiers = ConvertAHKModifiers(modifiers);
        
        // Create a context check function
        std::vector<std::function<bool()>> contexts;
        contexts.push_back([this, context]() {
            return this->CheckContext(context);
        });
        
        // Use the IO's Hotkey method with context
        return io.Hotkey(hotkeyStr, [this, action]() {
            this->ExecuteString(action);
        });
    }
    return false;
}

void ScriptEngine::ParseHotkeyString(const std::string& hotkeyStr, std::string& key, std::string& modifiers) {
    // Simple parsing for now
    size_t plusPos = hotkeyStr.find_last_of('+');
    if (plusPos != std::string::npos) {
        modifiers = hotkeyStr.substr(0, plusPos);
        key = hotkeyStr.substr(plusPos + 1);
    } else {
        key = hotkeyStr;
        modifiers = "";
    }
}

std::string ScriptEngine::ConvertAHKModifiers(const std::string& modifiers) {
    std::string result = modifiers;
    // Convert AHK modifiers to system modifiers
    // ^ -> Ctrl, ! -> Alt, + -> Shift, # -> Win/Super
    std::replace(result.begin(), result.end(), '^', 'C');
    std::replace(result.begin(), result.end(), '!', 'A');
    std::replace(result.begin(), result.end(), '+', 'S');
    std::replace(result.begin(), result.end(), '#', 'W');
    return result;
}

bool ScriptEngine::CheckContext(const std::string& context) {
    // Check if a window with a specific title is active
    if (context.find("WinActive(") == 0) {
        std::regex winActivePattern("WinActive\\(\\s*\"([^\"]*?)\"\\s*\\)");
        std::smatch matches;
        
        if (std::regex_match(context, matches, winActivePattern)) {
            std::string windowTitle = matches[1].str();
            // Use static method for FindByTitle
            return WindowManager::FindByTitle(windowTitle) != 0;
        }
    }
    
    // Simple config check
    if (context.find("config.") == 0) {
        std::string configKey = context.substr(7); // Remove "config."
        sol::optional<bool> configValue = lua["config"][configKey];
        return configValue.value_or(false);
    }
    
    return false;
}

} // namespace H 