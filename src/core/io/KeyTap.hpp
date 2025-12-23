#pragma once
#include "core/IO.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <string>
#include <variant>

namespace havel {
    class HotkeyManager;  // Forward declaration

    class KeyTap {
        private:
            std::string keyName;
            std::function<void()> onTap;
            std::function<void()> onCombo;
            std::variant<std::string, std::function<bool()>> tapCondition;
            std::variant<std::string, std::function<bool()>> comboCondition;
            HotkeyManager& hotkeyManager;

            // State variables
            bool keyHeld = false;
            bool combo = false;
            bool grabDown = true;
            bool grabUp = true;

            // Helper to evaluate conditions
            bool evaluateCondition(const std::variant<std::string, std::function<bool()>>& cond) const {
                if (std::holds_alternative<std::function<bool()>>(cond)) {
                    auto func = std::get<std::function<bool()>>(cond);
                    return func ? func() : true;
                }
                // For string conditions, you'd implement your string evaluation logic here
                return true;
            }

        public:
            // Single unified constructor using templates
            template<typename TapCond, typename ComboCond = std::monostate>
            KeyTap(IO& ioRef, HotkeyManager& hotkeyManagerRef, const std::string& key,
                   std::function<void()> tapAction,
                   TapCond tapCond = TapCond{},
                   std::function<void()> comboAction = nullptr,
                   ComboCond comboCond = ComboCond{},
                   bool grabDownFlag = true,
                   bool grabUpFlag = true)
                : keyName(key), 
                  onTap(tapAction), 
                  onCombo(comboAction),
                  hotkeyManager(hotkeyManagerRef),
                  grabDown(grabDownFlag),
                  grabUp(grabUpFlag) {
                
                // Handle tap condition
                if constexpr (std::is_same_v<TapCond, std::string> || 
                              std::is_convertible_v<TapCond, std::string>) {
                    tapCondition = std::string(tapCond);
                } else if constexpr (std::is_same_v<TapCond, std::function<bool()>> ||
                                     std::is_invocable_r_v<bool, TapCond>) {
                    tapCondition = std::function<bool()>(tapCond);
                } else {
                    tapCondition = std::string("");
                }

                // Handle combo condition
                if constexpr (std::is_same_v<ComboCond, std::string> || 
                              std::is_convertible_v<ComboCond, std::string>) {
                    comboCondition = std::string(comboCond);
                } else if constexpr (std::is_same_v<ComboCond, std::function<bool()>> ||
                                     std::is_invocable_r_v<bool, ComboCond>) {
                    comboCondition = std::function<bool()>(comboCond);
                } else {
                    comboCondition = std::string("");
                }
            }

            ~KeyTap();
            void setup();
    };
}