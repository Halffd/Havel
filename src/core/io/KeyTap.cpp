#include "KeyTap.hpp"
#include "core/HotkeyManager.hpp"
#include <iostream>

namespace havel {

KeyTap::KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key, 
               std::function<void()> tapAction, 
               const std::string& tapCond,
               std::function<void()> comboAction,
               const std::string& comboCond) 
    : keyName(key), onTap(tapAction), onCombo(comboAction),
      tapCondition(tapCond), comboCondition(comboCond),
      io(ioRef), hotkeyManager(hotkeyManagerRef) {}

KeyTap::~KeyTap() {
    stopMonitoring();
}

void KeyTap::startMonitoring() {
    keyComboDetected = false;
    monitorActive = true;
    
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
    
    monitorThread = std::thread([this]() {
        while (monitorActive) {
            if (io.IsAnyKeyPressedExcept(keyName)) {
                keyComboDetected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

void KeyTap::stopMonitoring() {
    monitorActive = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void KeyTap::setup() {
    std::string keyDown = "@~" + keyName;
    std::string keyUp = "@" + keyName + ":up";
    
    // Tap behavior (press)
    if (!tapCondition.empty()) {
        hotkeyManager.AddContextualHotkey(keyDown, tapCondition, [this]() {
            std::cout << "[KeyTap] Starting tap monitoring for " << keyName << std::endl;
            startMonitoring();
        });
    } else {
        hotkeyManager.AddContextualHotkey(keyDown, "", [this]() {
            std::cout << "[KeyTap] Starting tap monitoring for " << keyName << std::endl;
            startMonitoring();
        });
    }
    
    // Combo behavior (press) - if different condition
    if (onCombo && !comboCondition.empty()) {
        hotkeyManager.AddContextualHotkey("@" + keyName, comboCondition, [this]() {
            std::cout << "[KeyTap] Triggering combo action for " << keyName << std::endl;
            onCombo();
        });
    }
    
    // Tap behavior (release)
    if (!tapCondition.empty()) {
        hotkeyManager.AddContextualHotkey(keyUp, tapCondition, [this]() {
            std::cout << "[KeyTap] " << keyName << " released, checking tap" << std::endl;
            stopMonitoring();
            
            if (!keyComboDetected) {
                std::cout << "[KeyTap] Clean tap detected!" << std::endl;
                onTap();
            }
        });
    } else {
        hotkeyManager.AddContextualHotkey(keyUp, "", [this]() {
            std::cout << "[KeyTap] " << keyName << " released, checking tap" << std::endl;
            stopMonitoring();
            
            if (!keyComboDetected) {
                std::cout << "[KeyTap] Clean tap detected!" << std::endl;
                onTap();
            }
        });
    }
}

}