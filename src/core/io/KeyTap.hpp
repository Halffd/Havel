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
            HotkeyManager& hotkeyManager;

            // New state variables based on requirements
            bool keyHeld = false;
            bool combo = false;

        public:
            KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key,
                   std::function<void()> tapAction,
                   const std::string& tapCond = "",
                   std::function<void()> comboAction = nullptr,
                   const std::string& comboCond = "");

            ~KeyTap();
            void setup();
    };
}