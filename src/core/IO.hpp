#pragma once
#include <X11/Xlib.h>
#include "../common/types.hpp"
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <unordered_map>
#include <thread>
#include <iostream>

namespace H {

struct HotKey {
    std::string alias;
    Key key;
    int modifiers;
    std::function<void()> callback;
    std::string action;
    std::vector<std::function<bool()>> contexts;
    bool enabled = true;
    bool blockInput = false;
    bool suspend = false;
};

struct IoEvent {
    Key key;
    int modifiers;
    bool isDown;
};

class IO {
public:
    IO();
    ~IO();

    // Key sending methods
    void Send(Key key, bool down = true);
    void Send(const std::string& keys);
    void SendSpecific(const std::string& keys);
    void ControlSend(const std::string& control, const std::string& keys);
    void ProcessKeyCombination(const std::string& keys);
    void SendX11Key(const std::string &keyName, bool press);
    
    // Hotkey methods
    bool ContextActive(std::vector<std::function<bool()>> contexts);
    bool AddHotkey(const std::string& alias, Key key, int modifiers, std::function<void()> callback);
    bool Hotkey(const std::string& hotkeyStr, std::function<void()> action, int id = 0);
    bool Suspend(int id);
    bool Resume(int id);
    
    // Mouse methods
    void MouseMove(int x, int y);
    void MouseClick(int button);
    void MouseDown(int button);
    void MouseUp(int button);
    void MouseWheel(int amount);
    
    // State methods
    int GetState(const std::string& keyName, const std::string& mode = "");
    void PressKey(const std::string& keyName, bool press);
    
    // Utility methods
    void SetTimer(int milliseconds, const std::function<void()> &func);
    void MsgBox(const std::string& message);
    int GetMouse();
    int GetKeyboard();
    int ParseModifiers(std::string str);
    void AssignHotkey(HotKey hotkey, int id);
    
    // Add new methods for dynamic hotkey grabbing/ungrabbing
    bool GrabHotkey(int hotkeyId);
    bool UngrabHotkey(int hotkeyId);
    bool GrabHotkeysByPrefix(const std::string& prefix);
    bool UngrabHotkeysByPrefix(const std::string& prefix);
    
    // Static methods
    static void removeSpecialCharacters(std::string &keyName);
    static void HandleKeyEvent(XEvent& event);
    static void HandleMouseEvent(XEvent& event);
    static Key StringToButton(std::string buttonName);
    static Key handleKeyString(const std::string& keystr);
    static Key StringToVirtualKey(std::string keyName);

private:
    // X11 hotkey monitoring
    void MonitorHotkeys();
    
    // Platform specific implementations
    Display* display;
    std::map<std::string, Key> keyMap;
    std::map<std::string, HotKey> instanceHotkeys; // Renamed to avoid conflict
    std::map<std::string, bool> hotkeyStates;
    std::thread timerThread;
    bool timerRunning = false;
    
    // Static members
    static std::unordered_map<int, HotKey> hotkeys;
    static bool hotkeyEnabled;
    static int hotkeyCount;
    
    // Key mapping and sending utilities
    void InitKeyMap();
    void SendKeyEvent(Key key, bool down);
    std::vector<IoEvent> ParseKeysString(const std::string& keys);
};

} // namespace H
