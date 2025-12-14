#include "KeyTap.hpp"
#include "core/HotkeyManager.hpp"
#include <iostream>

namespace havel {

KeyTap::KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key,
               std::function<void()> tapAction,
               const std::string& tapCond,
               std::function<void()> comboAction,
               const std::string& comboCond, bool grabDown, bool grabUp)
    : keyName(key), onTap(tapAction), onCombo(comboAction),
      tapCondition(tapCond), comboCondition(comboCond),
      hotkeyManager(hotkeyManagerRef), grabDown(grabDown), grabUp(grabUp) {}

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
    if (!tapCondition.empty()) {
        hotkeyManager.AddContextualHotkey(keyDown, tapCondition, [this]() {
            keyHeld = true;
            combo = false;
        });
    } else {
        hotkeyManager.AddHotkey(keyDown, [this]() {
            keyHeld = true;
            combo = false;
        });
    }

    // Combo behavior (press) - if different condition
    if (onCombo && !comboCondition.empty()) {
        hotkeyManager.AddContextualHotkey("@" + keyName, comboCondition, [this]() {
            onCombo();
        });
    }

    // Key up behavior (release) - check for clean tap
    if (!tapCondition.empty()) {
        hotkeyManager.AddContextualHotkey(keyUp, tapCondition, [this]() {
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
}


}