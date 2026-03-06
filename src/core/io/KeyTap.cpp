#include "KeyTap.hpp"
#include "core/HotkeyManager.hpp"
#include "utils/Logger.hpp"
#include <iostream>

namespace havel {

KeyTap::~KeyTap() {
    // Destructor implementation can be empty since we don't have threads anymore
}

void KeyTap::setup() {
    debug("KeyTap::setup() called for key: '{}'", keyName);
    
    // Only register ONE key down hotkey
    std::string downPrefix = "@|";
    if(!grabDown) {
        downPrefix += "~";
    }
    std::string keyDown = downPrefix + keyName;
    std::string keyUp = downPrefix + keyName + ":up";
    
    debug("KeyTap: Registering keyDown='{}', keyUp='{}'", keyDown, keyUp);
    
    // Register any-key callback to detect combos
    hotkeyManager.RegisterAnyKeyPressCallback([this](const std::string& key) {
        if (keyHeld && key != keyName) {
            combo = true;
            // If we have a combo action, trigger it immediately
            if (onCombo && evaluateCondition(comboCondition)) {
                onCombo();
                // Clear states so tap doesn't also trigger
                keyHeld = false;
                combo = false;
            }
        }
    });
    
    // Key down - just set held state
    if (std::holds_alternative<std::function<bool()>>(tapCondition)) {
        auto func = std::get<std::function<bool()>>(tapCondition);
        hotkeyManager.AddHotkey(keyDown, [this, func]() {
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
    
    // Key up - check for tap
    if (std::holds_alternative<std::function<bool()>>(tapCondition)) {
        auto func = std::get<std::function<bool()>>(tapCondition);
        hotkeyManager.AddHotkey(keyUp, [this, func]() {
            if (keyHeld && !combo && func && func()) {
                onTap();   // clean tap
            }
            keyHeld = false;
            combo = false;
        });
    } else if (std::holds_alternative<std::string>(tapCondition)) {
        auto condStr = std::get<std::string>(tapCondition);
        if (!condStr.empty()) {
            hotkeyManager.AddContextualHotkey(keyUp, condStr, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
                combo = false;
            });
        } else {
            hotkeyManager.AddHotkey(keyUp, [this]() {
                if (keyHeld && !combo) {
                    onTap();   // clean tap
                }
                keyHeld = false;
                combo = false;
            });
        }
    } else {
        hotkeyManager.AddHotkey(keyUp, [this]() {
            if (keyHeld && !combo) {
                onTap();   // clean tap
            }
            keyHeld = false;
            combo = false;
        });
    }
    
    debug("KeyTap::setup() complete for '{}'", keyName);
}
}