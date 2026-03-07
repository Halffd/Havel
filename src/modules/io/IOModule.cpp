/*
 * IOModule.cpp
 * 
 * IO control module for Havel language.
 * Host binding - connects language to IO system.
 */
#include "IOModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/IO.hpp"

namespace havel::modules {

void registerIOModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.io) {
        return;  // Skip if IO not available
    }
    
    auto& io = *ctx.io;
    
    // Create io module object
    auto ioObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
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
    // Key mapping functions
    // =========================================================================
    
    (*ioObj)["map"] = HavelValue(BuiltinFunction([&io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("io.map() requires (from, to)");
        }
        
        std::string from = valueToString(args[0]);
        std::string to = valueToString(args[1]);
        io.Map(from, to);
        return HavelValue(nullptr);
    }));
    
    (*ioObj)["remap"] = HavelValue(BuiltinFunction([&io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("io.remap() requires (key1, key2)");
        }
        
        std::string key1 = valueToString(args[0]);
        std::string key2 = valueToString(args[1]);
        io.Remap(key1, key2);
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // IO control functions
    // =========================================================================
    
    (*ioObj)["block"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        // Note: Actual blocking would require HotkeyManager integration
        std::cout << "[INFO] IO input blocked" << std::endl;
        return HavelValue(nullptr);
    }));
    
    (*ioObj)["unblock"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        // Note: Actual unblocking would require HotkeyManager integration
        std::cout << "[INFO] IO input unblocked" << std::endl;
        return HavelValue(nullptr);
    }));
    
    (*ioObj)["suspend"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io.Suspend());
    }));
    
    (*ioObj)["resume"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        if (io.isSuspended) {
            return HavelValue(io.Suspend());
        }
        return HavelValue(true);
    }));
    
    (*ioObj)["grab"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        // Note: Actual grab would require HotkeyManager integration
        std::cout << "[INFO] IO input grabbed" << std::endl;
        return HavelValue(nullptr);
    }));
    
    (*ioObj)["ungrab"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        // Note: Actual ungrab would require HotkeyManager integration
        std::cout << "[INFO] IO input ungrabbed" << std::endl;
        return HavelValue(nullptr);
    }));
    
    (*ioObj)["testKeycode"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        std::cout << "[INFO] Press any key to see its keycode... (Not yet implemented)" << std::endl;
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // Key state functions
    // =========================================================================
    
    (*ioObj)["getKeyState"] = HavelValue(BuiltinFunction([&io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io.getKeyState() requires key name");
        }
        
        std::string key = valueToString(args[0]);
        return HavelValue(io.GetKeyState(key));
    }));
    
    (*ioObj)["isShiftPressed"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io.IsShiftPressed());
    }));
    
    (*ioObj)["isCtrlPressed"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io.IsCtrlPressed());
    }));
    
    (*ioObj)["isAltPressed"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io.IsAltPressed());
    }));
    
    (*ioObj)["isWinPressed"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io.IsWinPressed());
    }));

    // =========================================================================
    // Mouse functions
    // =========================================================================
    
    auto mouseObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*mouseObj)["move"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 2) {
            return HavelRuntimeError("mouse.move(dx, dy) requires 2 arguments");
        }
        
        int dx = static_cast<int>(args[0].asNumber());
        int dy = static_cast<int>(args[1].asNumber());
        
        if (!io.MouseMove(dx, dy)) {
            return HavelRuntimeError("MouseMove failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["moveTo"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2 || args.size() > 4) {
            return HavelRuntimeError("mouse.moveTo(x, y, [speed], [accel]) requires 2-4 arguments");
        }
        
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int speed = args.size() > 2 ? static_cast<int>(args[2].asNumber()) : 1;
        int accel = args.size() > 3 ? static_cast<int>(args[3].asNumber()) : 0;
        
        if (!io.MouseMoveTo(x, y, speed, accel)) {
            return HavelRuntimeError("MouseMoveTo failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["click"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(args[0].asNumber());
        
        if (!io.MouseClick(button)) {
            return HavelRuntimeError("MouseClick failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["doubleClick"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(args[0].asNumber());
        
        if (!io.MouseDoubleClick(button)) {
            return HavelRuntimeError("MouseDoubleClick failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["press"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(args[0].asNumber());
        
        if (!io.MousePress(button)) {
            return HavelRuntimeError("MousePress failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["release"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = args.empty() ? 1 : static_cast<int>(args[0].asNumber());
        
        if (!io.MouseRelease(button)) {
            return HavelRuntimeError("MouseRelease failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["scroll"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("mouse.scroll(x, y) requires 2 arguments");
        }
        
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        
        if (!io.MouseScroll(x, y)) {
            return HavelRuntimeError("MouseScroll failed");
        }
        return HavelValue(true);
    }));
    
    (*mouseObj)["getPosition"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        auto pos = io.GetMousePosition();
        auto posObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*posObj)["x"] = HavelValue(static_cast<double>(pos.first));
        (*posObj)["y"] = HavelValue(static_cast<double>(pos.second));
        return HavelValue(posObj);
    }));
    
    // Register io and mouse modules
    (*ioObj)["mouse"] = HavelValue(mouseObj);
    env.Define("io", HavelValue(ioObj));
}

} // namespace havel::modules
