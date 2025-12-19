#pragma once
#include "core/IO.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <string>

namespace havel {
    class HotkeyManager;  // Forward declaration

    class KeyTap {
        private:
            std::string keyName;
            std::function<void()> onTap;
            std::function<void()> onCombo;
            std::string tapCondition;
            std::string comboCondition;
            std::function<bool()> tapConditionFunc;
            std::function<bool()> comboConditionFunc;
            HotkeyManager& hotkeyManager;

            // New state variables based on requirements
            bool keyHeld = false;
            bool combo = false;
            bool grabDown = true;
            bool grabUp = true;

        public:
            KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key,
                   std::function<void()> tapAction,
                   const std::string& tapCond = "",
                   std::function<void()> comboAction = nullptr,
                   const std::string& comboCond = "", bool grabDown = true, bool grabUp = true);
            KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key,
                   std::function<void()> tapAction,
                   std::function<bool()> tapCondFunc,
                   std::function<void()> comboAction = nullptr,
                   std::function<bool()> comboCondFunc = nullptr, bool grabDown = true, bool grabUp = true);

            ~KeyTap();
            void setup();
    };
}