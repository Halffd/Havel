#include "KeyTap.hpp"
#include "core/HotkeyManager.hpp"
#include <iostream>

namespace havel {

KeyTap::~KeyTap() {
    // Destructor implementation can be empty since we don't have threads anymore
}

void KeyTap::setup() {
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
        hotkeyManager.io.Hotkey(keyDown, [this, func]() {
            if (func && func()) {
                keyHeld = true;
                combo = false;
            }
        });
    } else if (std::holds_alternative<std::string>(tapCondition)) {
        auto condStr = std::get<std::string>(tapCondition);
        if (!condStr.empty()) {
            hotkeyManager.AddContextualHotkey(keyDown, condStr, [this]() {
                keyHeld = true;
                combo = false;
            });
        } else {
            hotkeyManager.AddHotkey(keyDown, [this]() {
                keyHeld = true;
                combo = false;
            });
        }
    } else {
        hotkeyManager.AddHotkey(keyDown, [this]() {
            keyHeld = true;
            combo = false;
        });
    }

    // Combo behavior (press) - if different condition
    if (onCombo) {
        if (std::holds_alternative<std::function<bool()>>(comboCondition)) {
            // Using function-based condition for combo
            auto func = std::get<std::function<bool()>>(comboCondition);
            hotkeyManager.io.Hotkey("@|" + keyName, [this, func]() {
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
        hotkeyManager.io.Hotkey(keyUp, [this, func]() {
            if (keyHeld && !combo && func && func()) {
                onTap();   // clean tap
            }
            keyHeld = false;
        });
    } else if (std::holds_alternative<std::string>(tapCondition)) {
        auto condStr = std::get<std::string>(tapCondition);
        if (!condStr.empty()) {
            hotkeyManager.AddContextualHotkey(keyUp, condStr, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
            });
        } else {
            hotkeyManager.AddHotkey(keyUp, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
            });
        }
    } else {
        hotkeyManager.AddHotkey(keyUp, [this]() {
            if (keyHeld && !combo) {
                onTap();   // clean tap
            }
            keyHeld = false;
        });
    }
}

}