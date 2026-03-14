/*
 * GUIModule.cpp
 *
 * GUI dialogs module for Havel language.
 * Host binding - connects language to GUIManager.
 */
#include "GUIModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/GUIManager.hpp"
#include "gui/HavelApp.hpp"
#include <optional>
#include <QApplication>

namespace havel::modules {

void registerGUIModule(Environment& env, IHostAPI* hostAPI) {
    bool hasManager = hostAPI->GetIO() != nullptr && hostAPI->GetGUIManager();
    GUIManager* gm = hasManager ? hostAPI->GetGUIManager() : nullptr;

    // Create gui module object
    auto guiObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };

    // =========================================================================
    // GUI initialization functions
    // =========================================================================

    (*guiObj)["initialize"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        // Check if QApplication already exists
        if (QApplication::instance()) {
            return HavelValue(true);  // Already initialized
        }

        // Create QApplication with proper argc/argv
        // Note: In REPL mode, we don't have access to original argv
        // For full CLI argument support, arguments should be passed via HostContext
        static int argc = 1;
        static char* argv[] = { (char*)"havel", nullptr };
        new QApplication(argc, argv);

        // Initialize GUI manager if available
        if (hostAPI->GetGUIManager()) {
            hostAPI->GetGUIManager()->initialize();
        }

        return HavelValue(true);
    }));

    (*guiObj)["reload"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        if (!hostAPI->GetGUIManager()) {
            return HavelRuntimeError("gui.reload() requires GUI to be initialized first");
        }

        // Reload GUI components - refresh windows and dialogs
        hostAPI->GetGUIManager()->reload();

        return HavelValue(true);
    }));

    (*guiObj)["isInitialized"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(QApplication::instance() != nullptr);
    }));

    // =========================================================================
    // Menu dialog
    // =========================================================================

    auto requireGui = [hasManager](const std::string& fn) -> std::optional<HavelRuntimeError> {
        if (!hasManager) {
            return HavelRuntimeError("gui." + fn + " requires a GUI manager. Call gui.initialize() first.");
        }
        return std::nullopt;
    };

    (*guiObj)["showMenu"] = HavelValue(BuiltinFunction([gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("showMenu")) return *err;
        if (args.size() < 2) {
            return HavelRuntimeError("gui.showMenu() requires (title, options)");
        }
        
        std::string title = valueToString(args[0]);
        
        if (!args[1].is<HavelArray>()) {
            return HavelRuntimeError("gui.showMenu() requires an array of options");
        }
        
        auto optionsVec = args[1].get<HavelArray>();
        std::vector<std::string> options;
        if (optionsVec) {
            for (const auto& opt : *optionsVec) {
                options.push_back(valueToString(opt));
            }
        }
        
        bool multiSelect = args.size() > 2 ? args[2].asBool() : false;
        std::string selected = gm->showMenu(title, options, multiSelect);
        return HavelValue(selected);
    }));
    
    // =========================================================================
    // Input dialog
    // =========================================================================
    
    (*guiObj)["input"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("input")) return *err;
        if (args.empty()) {
            return HavelRuntimeError("gui.input() requires title");
        }
        
        std::string title = valueToString(args[0]);
        std::string prompt = args.size() > 1 ? valueToString(args[1]) : "";
        std::string defaultValue = args.size() > 2 ? valueToString(args[2]) : "";
        
        std::string input = gm->showInputDialog(title, prompt, defaultValue);
        return HavelValue(input);
    }));
    
    // =========================================================================
    // Confirm dialog
    // =========================================================================
    
    (*guiObj)["confirm"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("confirm")) return *err;
        if (args.size() < 2) {
            return HavelRuntimeError("gui.confirm() requires (title, message)");
        }
        
        std::string title = valueToString(args[0]);
        std::string message = valueToString(args[1]);
        
        bool confirmed = gm->showConfirmDialog(title, message);
        return HavelValue(confirmed);
    }));
    
    // =========================================================================
    // Notification
    // =========================================================================
    
    (*guiObj)["notify"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("notify")) return *err;
        if (args.size() < 2) {
            return HavelRuntimeError("gui.notify() requires (title, message)");
        }
        
        std::string title = valueToString(args[0]);
        std::string message = valueToString(args[1]);
        std::string icon = args.size() > 2 ? valueToString(args[2]) : "info";
        
        gm->showNotification(title, message, icon);
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // File dialog
    // =========================================================================
    
    (*guiObj)["fileDialog"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("fileDialog")) return *err;
        std::string title = args.size() > 0 ? valueToString(args[0]) : "Select File";
        std::string dir = args.size() > 1 ? valueToString(args[1]) : "";
        std::string filter = args.size() > 2 ? valueToString(args[2]) : "";
        
        std::string selected = gm->showFileDialog(title, dir, filter);
        return HavelValue(selected);
    }));
    
    // =========================================================================
    // Directory dialog
    // =========================================================================
    
    (*guiObj)["directoryDialog"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("directoryDialog")) return *err;
        std::string title = args.size() > 0 ? valueToString(args[0]) : "Select Directory";
        std::string dir = args.size() > 1 ? valueToString(args[1]) : "";
        
        std::string selected = gm->showDirectoryDialog(title, dir);
        return HavelValue(selected);
    }));
    
    // =========================================================================
    // Window transparency
    // =========================================================================
    
    (*guiObj)["setTransparency"] = HavelValue(BuiltinFunction([&gm, requireGui](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("setTransparency")) return *err;
        if (args.empty()) {
            return HavelRuntimeError("window.setTransparency() requires opacity (0.0-1.0)");
        }
        
        double opacity = args[0].asNumber();
        bool success = gm->setActiveWindowTransparency(opacity);
        return HavelValue(success);
    }));
    
    // =========================================================================
    // Password dialog
    // =========================================================================

    (*guiObj)["password"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("password")) return *err;
        if (args.empty()) {
            return HavelRuntimeError("gui.password() requires title");
        }

        std::string title = valueToString(args[0]);
        std::string prompt = args.size() > 1 ? valueToString(args[1]) : "Enter password:";

        std::string password = gm->showPasswordDialog(title, prompt);
        return HavelValue(password);
    }));

    // =========================================================================
    // Color picker
    // =========================================================================

    (*guiObj)["colorPicker"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("colorPicker")) return *err;
        std::string title = args.size() > 0 ? valueToString(args[0]) : "Select Color";
        std::string defaultColor = args.size() > 1 ? valueToString(args[1]) : "#FFFFFF";

        std::string color = gm->showColorPicker(title, defaultColor);
        return HavelValue(color);
    }));

    // =========================================================================
    // Window transparency (by window ID)
    // =========================================================================

    (*guiObj)["setWindowTransparency"] = HavelValue(BuiltinFunction([&gm, requireGui](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("setWindowTransparency")) return *err;
        if (args.size() < 2) {
            return HavelRuntimeError("gui.setWindowTransparency() requires (windowId, opacity)");
        }

        uint64_t windowId = static_cast<uint64_t>(args[0].asNumber());
        double opacity = args[1].asNumber();
        bool success = gm->setWindowTransparency(windowId, opacity);
        return HavelValue(success);
    }));

    // =========================================================================
    // Window transparency (by title)
    // =========================================================================

    (*guiObj)["setTransparencyByTitle"] = HavelValue(BuiltinFunction([&gm, requireGui, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireGui("setTransparencyByTitle")) return *err;
        if (args.size() < 2) {
            return HavelRuntimeError("gui.setTransparencyByTitle() requires (title, opacity)");
        }

        std::string title = valueToString(args[0]);
        double opacity = args[1].asNumber();
        bool success = gm->setWindowTransparencyByTitle(title, opacity);
        return HavelValue(success);
    }));

    // Register gui module
    env.Define("gui", HavelValue(guiObj));
}

} // namespace havel::modules
