/*
 * IOModule.cpp
 *
 * IO control module for Havel language.
 * Host binding - connects language to IO system.
 */
#include "IOModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <algorithm>
#include <cctype>
#include <optional>

namespace havel::modules {

// Global storage for KeyTap instances to keep them alive
static std::vector<std::unique_ptr<KeyTap>> g_keyTapStorage;
static std::mutex g_keyTapMutex;

static std::string toLowerString(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::optional<int> parseMouseButton(const HavelValue& value) {
    if (value.isNumber()) {
        return static_cast<int>(value.asNumber());
    }
    if (!value.isString()) {
        return std::nullopt;
    }

    std::string raw = toLowerString(value.asString());
    if (raw.empty()) {
        return std::nullopt;
    }

    bool isNumeric = std::all_of(raw.begin(), raw.end(),
                                 [](unsigned char c) { return std::isdigit(c) != 0; });
    if (isNumeric) {
        try {
            return std::stoi(raw);
        } catch (...) {
            return std::nullopt;
        }
    }

    if (raw == "left" || raw == "lmb" || raw == "button1") return 1;
    if (raw == "right" || raw == "rmb" || raw == "button2") return 2;
    if (raw == "middle" || raw == "mmb" || raw == "button3") return 3;
    if (raw == "wheelup" || raw == "scrollup" || raw == "button4") return 4;
    if (raw == "wheeldown" || raw == "scrolldown" || raw == "button5") return 5;

    return std::nullopt;
}

void registerIOModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    if (!hostAPI || !hostAPI->GetIO()) {
        return;  // Skip if IO not available
    }

    IO* io = hostAPI->GetIO();

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
    // Key sending functions
    // =========================================================================

    (*ioObj)["send"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->send() requires keys to send");
        }
        std::string keys = valueToString(args[0]);
        io->Send(keys.c_str());
        return HavelValue(nullptr);
    }));

    (*ioObj)["sendKey"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->sendKey() requires key name");
        }
        std::string key = valueToString(args[0]);
        io->SendX11Key(key, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        io->SendX11Key(key, false);
        return HavelValue(nullptr);
    }));

    (*ioObj)["keyDown"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->keyDown() requires key name");
        }
        std::string key = valueToString(args[0]);
        io->SendX11Key(key, true);
        return HavelValue(nullptr);
    }));

    (*ioObj)["keyUp"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->keyUp() requires key name");
        }
        std::string key = valueToString(args[0]);
        io->SendX11Key(key, false);
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // Key mapping functions
    // =========================================================================

    (*ioObj)["map"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("io->map() requires (from, to)");
        }

        std::string from = valueToString(args[0]);
        std::string to = valueToString(args[1]);
        io->Map(from, to);
        return HavelValue(nullptr);
    }));

    (*ioObj)["remap"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("io->remap() requires (key1, key2)");
        }

        std::string key1 = valueToString(args[0]);
        std::string key2 = valueToString(args[1]);
        io->Remap(key1, key2);
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // IO control functions
    // =========================================================================

    (*ioObj)["block"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        // Emergency release all keys to block input
        io->EmergencyReleaseAllKeys();
        return HavelValue(nullptr);
    }));

    (*ioObj)["unblock"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        // Ungrab all hotkeys to unblock input
        io->UngrabAll();
        return HavelValue(nullptr);
    }));

    (*ioObj)["suspend"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->Suspend());
    }));

    (*ioObj)["resume"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        if (io->isSuspended) {
            return HavelValue(io->Suspend());
        }
        return HavelValue(true);
    }));

    (*ioObj)["isSuspended"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->IsSuspended());
    }));

    (*ioObj)["grab"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        io->EmergencyReleaseAllKeys();
        return HavelValue(nullptr);
    }));

    (*ioObj)["ungrab"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        io->UngrabAll();
        return HavelValue(nullptr);
    }));

    (*ioObj)["emergencyRelease"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        io->EmergencyReleaseAllKeys();
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // Key state functions
    // =========================================================================

    (*ioObj)["getKeyState"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->getKeyState() requires key name");
        }

        std::string key = valueToString(args[0]);
        return HavelValue(io->GetKeyState(key));
    }));

    (*ioObj)["isKeyPressed"] = HavelValue(BuiltinFunction([io, valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->isKeyPressed() requires key name");
        }

        std::string key = valueToString(args[0]);
        return HavelValue(io->IsKeyPressed(key));
    }));

    (*ioObj)["isShiftPressed"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->IsShiftPressed());
    }));

    (*ioObj)["isCtrlPressed"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->IsCtrlPressed());
    }));

    (*ioObj)["isAltPressed"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->IsAltPressed());
    }));

    (*ioObj)["isWinPressed"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->IsWinPressed());
    }));

    (*ioObj)["getCurrentModifiers"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(io->GetCurrentModifiers()));
    }));

    // =========================================================================
    // KeyTap - Advanced hotkey with conditions
    // =========================================================================

    (*ioObj)["keyTap"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("io->keyTap() requires at least a key name");
        }
        
        std::string keyName;
        if (args[0].isString()) {
            keyName = args[0].asString();
        } else {
            return HavelRuntimeError("io->keyTap(): first argument must be key name string");
        }
        
        // Parse optional arguments
        std::function<void()> onTap;
        std::variant<std::string, std::function<bool()>> tapCondition;
        std::variant<std::string, std::function<bool()>> comboCondition;
        std::function<void()> onCombo;
        bool grabDown = true;
        bool grabUp = true;
        
        if (args.size() > 1 && args[1].isFunction()) {
            onTap = [func = args[1].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() {
                func({});
            };
        }
        
        if (args.size() > 2) {
            if (args[2].isString()) {
                tapCondition = args[2].asString();
            } else if (args[2].isFunction()) {
                tapCondition = [func = args[2].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() -> bool {
                    auto result = func({});
                    if (auto* val = std::get_if<HavelValue>(&result)) {
                        return val->isBool() ? val->get<bool>() : val->isNumber() ? (val->asNumber() != 0) : true;
                    }
                    return false;
                };
            }
        }
        
        if (args.size() > 3) {
            if (args[3].isString()) {
                comboCondition = args[3].asString();
            } else if (args[3].isFunction()) {
                comboCondition = [func = args[3].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() -> bool {
                    auto result = func({});
                    if (auto* val = std::get_if<HavelValue>(&result)) {
                        return val->isBool() ? val->get<bool>() : val->isNumber() ? (val->asNumber() != 0) : true;
                    }
                    return false;
                };
            }
        }
        
        if (args.size() > 4 && args[4].isFunction()) {
            onCombo = [func = args[4].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() {
                func({});
            };
        }
        
        if (args.size() > 5 && args[5].isBool()) {
            grabDown = args[5].get<bool>();
        }
        
        if (args.size() > 6 && args[6].isBool()) {
            grabUp = args[6].get<bool>();
        }

        // Check if HotkeyManager is available
        auto* hm = hostAPI->GetHotkeyManager();
        if (!hm) {
            return HavelRuntimeError("io.keyTap() requires HotkeyManager (not available in pure mode)");
        }

        // Create KeyTap instance and store it to keep it alive
        // Pass shared_ptr to ensure KeyTap keeps HotkeyManager alive
        auto keyTap = std::make_unique<KeyTap>(
            std::shared_ptr<HotkeyManager>(hm, [](HotkeyManager*){}), keyName, onTap, tapCondition,
            comboCondition, onCombo, grabDown, grabUp
        );

        // Store instance FIRST to ensure it stays alive during setup
        {
            std::lock_guard<std::mutex> lock(g_keyTapMutex);
            g_keyTapStorage.push_back(std::move(keyTap));
        }

        // Now setup callbacks (KeyTap is already stored, so it won't be destroyed)
        g_keyTapStorage.back()->setup();

        return HavelValue(keyName + " KeyTap created");
    }));

    // =========================================================================
    // Mouse functions
    // =========================================================================

    auto mouseObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    (*mouseObj)["move"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("mouse.move(dx, dy) requires 2 arguments");
        }

        int dx = static_cast<int>(args[0].asNumber());
        int dy = static_cast<int>(args[1].asNumber());

        if (!io->MouseMove(dx, dy)) {
            return HavelRuntimeError("MouseMove failed");
        }
        return HavelValue(true);
    }));

    (*mouseObj)["moveTo"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("mouse.moveTo(x, y) requires 2 arguments");
        }

        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int speed = args.size() > 2 ? static_cast<int>(args[2].asNumber()) : 1;
        int accel = args.size() > 3 ? static_cast<int>(args[3].asNumber()) : 0;

        if (!io->MouseMoveTo(x, y, speed, accel)) {
            return HavelRuntimeError("MouseMoveTo failed");
        }
        return HavelValue(true);
    }));

    (*mouseObj)["click"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = 1;
        if (!args.empty()) {
            auto parsed = parseMouseButton(args[0]);
            if (!parsed || *parsed <= 0) {
                return HavelRuntimeError("mouse.click() requires a valid button");
            }
            button = *parsed;
        }
        io->MouseClick(button);
        return HavelValue(true);
    }));

    (*mouseObj)["doubleClick"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = 1;
        if (!args.empty()) {
            auto parsed = parseMouseButton(args[0]);
            if (!parsed || *parsed <= 0) {
                return HavelRuntimeError("mouse.doubleClick() requires a valid button");
            }
            button = *parsed;
        }
        // Double click = two clicks in sequence
        io->MouseClick(button);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        io->MouseClick(button);
        return HavelValue(true);
    }));

    (*mouseObj)["press"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = 1;
        if (!args.empty()) {
            auto parsed = parseMouseButton(args[0]);
            if (!parsed || *parsed <= 0) {
                return HavelRuntimeError("mouse.press() requires a valid button");
            }
            button = *parsed;
        }
        if (!io->MouseDown(button)) {
            return HavelRuntimeError("MouseDown failed");
        }
        return HavelValue(true);
    }));

    (*mouseObj)["release"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        int button = 1;
        if (!args.empty()) {
            auto parsed = parseMouseButton(args[0]);
            if (!parsed || *parsed <= 0) {
                return HavelRuntimeError("mouse.release() requires a valid button");
            }
            button = *parsed;
        }
        if (!io->MouseUp(button)) {
            return HavelRuntimeError("MouseUp failed");
        }
        return HavelValue(true);
    }));

    (*mouseObj)["down"] = (*mouseObj)["press"];
    (*mouseObj)["up"] = (*mouseObj)["release"];

    (*mouseObj)["scroll"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("mouse.scroll() requires 1 or 2 arguments");
        }
        if (!args[0].isNumber()) {
            return HavelRuntimeError("mouse.scroll() requires numeric arguments");
        }
        double dy = 0.0;  // Vertical scroll (default)
        double dx = 0.0;  // Horizontal scroll
        if (args.size() == 1) {
            dy = args[0].asNumber();  // Single arg = vertical scroll
        } else {
            if (!args[1].isNumber()) {
                return HavelRuntimeError("mouse.scroll() requires numeric arguments");
            }
            dy = args[0].asNumber();  // First arg = vertical (dy)
            dx = args[1].asNumber();  // Second arg = horizontal (dx)
        }
        if (!io->Scroll(dy, dx)) {
            return HavelRuntimeError("MouseScroll failed");
        }
        return HavelValue(true);
    }));

    (*mouseObj)["getPosition"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        auto pos = io->GetMousePosition();
        auto posObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*posObj)["x"] = HavelValue(static_cast<double>(pos.first));
        (*posObj)["y"] = HavelValue(static_cast<double>(pos.second));
        return HavelValue(posObj);
    }));
    (*mouseObj)["pos"] = (*mouseObj)["getPosition"];  // Alias

    (*mouseObj)["setSensitivity"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("mouse.setSensitivity() requires sensitivity value");
        }
        double sensitivity = args[0].asNumber();
        io->SetMouseSensitivity(sensitivity);
        return HavelValue(nullptr);
    }));

    (*mouseObj)["getSensitivity"] = HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->GetMouseSensitivity());
    }));

    // Register io and mouse modules
    (*ioObj)["mouse"] = HavelValue(mouseObj);
    env.Define("io", HavelValue(ioObj));
    
    // =========================================================================
    // Global convenience aliases (fast path for common operations)
    // =========================================================================
    
    // Mouse operations
    env.Define("click", (*mouseObj)["click"]);
    env.Define("doubleClick", (*mouseObj)["doubleClick"]);
    env.Define("mousePress", (*mouseObj)["press"]);
    env.Define("mouseRelease", (*mouseObj)["release"]);
    env.Define("mouseMove", (*mouseObj)["move"]);
    env.Define("mouseMoveRel", (*mouseObj)["moveRel"]);
    env.Define("scroll", (*mouseObj)["scroll"]);
    
    // =========================================================================
    // Global window information (for use in when blocks and conditions)
    // =========================================================================
    
    env.Define("title", HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->GetActiveWindowTitle());
    })));
    
    env.Define("class", HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(io->GetActiveWindowClass());
    })));
    
    // Get current active window executable name
    env.Define("exe", HavelValue(BuiltinFunction([io](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(WindowManager::getProcessName(io->GetActiveWindowPID()));
    })));

    // Global suspend/resume aliases (AHK-style)
    env.Define("suspend", HavelValue(BuiltinFunction([io](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            // suspend() with no args = toggle
            return HavelValue(io->Suspend());
        }
        // suspend(true/false) = explicit suspend/resume
        bool shouldSuspend = args[0].isBool() ? args[0].asBool() : (args[0].asNumber() != 0);
        if (shouldSuspend) {
            return HavelValue(io->Suspend());
        } else {
            return HavelValue(io->Resume());
        }
    })));

    // Note: process module provides process.find(), process.exists(), etc.
    // Use process.name(pid) to get process name by PID
}

} // namespace havel::modules
