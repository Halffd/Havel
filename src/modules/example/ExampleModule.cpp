// ExampleModule.cpp
// Example of proper module structure
// Use this as a template for new modules

#include "ExampleModule.hpp"
#include "../havel-lang/runtime/ModuleMacros.hpp"

namespace havel::modules {

// ============================================================================
// Standard Library Module Example
// ============================================================================

STD_MODULE_DESC(example, "Example standard library module") {
    // Helper function (not exported)
    auto helperFunction = [](const std::string& input) -> std::string {
        return "Helper: " + input;
    };
    
    // Exported function: example.hello(name)
    env.Define("hello", HavelValue(BuiltinFunction([helperFunction](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("hello() requires a name");
        }
        
        std::string name = args[0].isString() ? args[0].asString() : "World";
        return HavelValue("Hello, " + name + "!");
    }));
    
    // Exported function: example.add(a, b)
    env.Define("add", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("add() requires two arguments");
        }
        
        double a = args[0].asNumber();
        double b = args[1].asNumber();
        return HavelValue(a + b);
    }));
    
    // Exported function: example.double(text)
    env.Define("double", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("double() requires text");
        }
        
        std::string text = args[0].asString();
        return HavelValue(text + text);
    }));
}

// ============================================================================
// Host Module Example
// ============================================================================

HOST_MODULE_DESC(system, "System information and operations") {
    // Check if hostAPI is available
    if (!hostAPI) {
        havel::error("System module requires host API");
        return;
    }
    
    // Exported function: system.getActiveWindowTitle()
    env.Define("getActiveWindowTitle", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(hostAPI->GetActiveWindowTitle());
    }));
    
    // Exported function: system.getActiveWindowClass()
    env.Define("getActiveWindowClass", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(hostAPI->GetActiveWindowClass());
    }));
    
    // Exported function: system.getCurrentMode()
    env.Define("getCurrentMode", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(hostAPI->GetCurrentMode());
    }));
    
    // Exported function: system.setCurrentMode(mode)
    env.Define("setCurrentMode", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setCurrentMode() requires mode name");
        }
        
        std::string mode = args[0].asString();
        hostAPI->SetCurrentMode(mode);
        return HavelValue(true);
    }));
    
    // Exported function: system.sendKeys(keys)
    env.Define("sendKeys", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("sendKeys() requires keys");
        }
        
        std::string keys = args[0].asString();
        hostAPI->SendKeys(keys);
        return HavelValue(true);
    }));
    
    // Exported function: system.getClipboard()
    env.Define("getClipboard", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(hostAPI->GetClipboardText());
    }));
    
    // Exported function: system.setClipboard(text)
    env.Define("setClipboard", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setClipboard() requires text");
        }
        
        std::string text = args[0].asString();
        hostAPI->SetClipboardText(text);
        return HavelValue(true);
    }));
}

} // namespace havel::modules
