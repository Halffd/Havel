#include "KeyTap.hpp"
#include "core/HotkeyManager.hpp"
#include "utils/Logger.hpp"
#include <iostream>

namespace havel {

KeyTap::~KeyTap() {
    // Destructor implementation can be empty since we don't have threads anymore
}

void KeyTap::setup() {
    using havel::debug;
    
    debug("KeyTap::setup() called for key: '{}'", keyName);
    
    std::string downPrefix = "@|";
    if(!grabDown) {
        downPrefix += "~";
    }
    std::string upPrefix = "@|";
    if(!grabUp) {
        upPrefix += "~";
    }
    std::string keyDown = downPrefix + keyName;
    std::string keyUp = upPrefix + keyName + ":up";

    debug("KeyTap: Registering keyDown='{}', keyUp='{}'", keyDown, keyUp);

    // Register callback for any key press event
    hotkeyManager.RegisterAnyKeyPressCallback([this](const std::string& key) {
        if (keyHeld && key != keyName) {
            combo = true;
        }
    });

    // Key down behavior (press) - set keyHeld and reset combo
    if (std::holds_alternative<std::function<bool()>>(tapCondition)) {
        // Using function-based condition
        auto func = std::get<std::function<bool()>>(tapCondition);
        debug("KeyTap: Registering keyDown with function condition");
        hotkeyManager.AddHotkey(keyDown, [this, func]() {
            if (func && func()) {
                keyHeld = true;
                combo = false;
            }
        });
    } else if (std::holds_alternative<std::string>(tapCondition)) {
        auto condStr = std::get<std::string>(tapCondition);
        if (!condStr.empty()) {
            debug("KeyTap: Registering keyDown with string condition: '{}'", condStr);
            hotkeyManager.AddContextualHotkey(keyDown, condStr, [this]() {
                keyHeld = true;
                combo = false;
            });
        } else {
            debug("KeyTap: Registering keyDown without condition");
            hotkeyManager.AddHotkey(keyDown, [this]() {
                keyHeld = true;
                combo = false;
            });
        }
    } else {
        debug("KeyTap: Registering keyDown (no condition variant)");
        hotkeyManager.AddHotkey(keyDown, [this]() {
            keyHeld = true;
            combo = false;
        });
    }

    // Combo behavior (press) - if different condition
    if (onCombo) {
        debug("KeyTap: Registering combo handler");
        if (std::holds_alternative<std::function<bool()>>(comboCondition)) {
            // Using function-based condition for combo
            auto func = std::get<std::function<bool()>>(comboCondition);
            hotkeyManager.AddHotkey("@|" + keyName, [this, func]() {
                if (func && func()) {
                    onCombo();
                }
            });
        } else if (std::holds_alternative<std::string>(comboCondition)) {
            auto condStr = std::get<std::string>(comboCondition);
            if (!condStr.empty()) {
                hotkeyManager.AddContextualHotkey("@|" + keyName, condStr, [this]() {
                    onCombo();
                });
            } else {
                hotkeyManager.AddHotkey("@|" + keyName, [this]() {
                    onCombo();
                });
            }
        } else {
            hotkeyManager.AddHotkey("@|" + keyName, [this]() {
                onCombo();
            });
        }
    }

    // Key up behavior (release) - check for clean tap
    if (std::holds_alternative<std::function<bool()>>(tapCondition)) {
        // Using function-based condition
        auto func = std::get<std::function<bool()>>(tapCondition);
        debug("KeyTap: Registering keyUp with function condition");
        hotkeyManager.AddHotkey(keyUp, [this, func]() {
            if (keyHeld && !combo && func && func()) {
                onTap();   // clean tap
            }
            keyHeld = false;
        });
    } else if (std::holds_alternative<std::string>(tapCondition)) {
        auto condStr = std::get<std::string>(tapCondition);
        if (!condStr.empty()) {
            debug("KeyTap: Registering keyUp with string condition: '{}'", condStr);
            hotkeyManager.AddContextualHotkey(keyUp, condStr, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
            });
        } else {
            debug("KeyTap: Registering keyUp without condition");
            hotkeyManager.AddHotkey(keyUp, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
            });
        }
    } else {
        debug("KeyTap: Registering keyUp (no condition variant)");
        hotkeyManager.AddHotkey(keyUp, [this]() {
            if (keyHeld && !combo) {
                onTap();   // clean tap
            }
            keyHeld = false;
        });
    }
    
    debug("KeyTap::setup() complete for '{}'", keyName);
}

}