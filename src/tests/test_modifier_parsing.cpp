#include <iostream>
#include <string>
#include <vector>
#include "../core/IO.hpp"

using namespace havel;

int main() {
    std::cout << "Testing 2-modifier hotkey parsing...\n\n";

    // Test cases for 2-modifier hotkeys
    std::vector<std::string> testCases = {
        "lctrl+rctrl",     // Both control keys
        "lshift+rshift",   // Both shift keys
        "lalt+ralt",       // Both alt keys
        "lctrl+ralt",      // Mixed specific modifiers
        "ctrl+rshift",     // Generic + specific
        "lctrl+lshift+rshift", // Three modifiers
        "shift+rshift",    // Generic shift + specific right shift
        "^r",              // Generic ctrl + key (existing functionality)
        "+tab",            // Generic shift + key (existing functionality)
    };

    for (const auto& testCase : testCases) {
        std::cout << "Testing: \"" << testCase << "\"\n";
        
        ParsedHotkey parsed = IO::ParseHotkeyString(testCase);
        
        std::cout << "  Key part: \"" << parsed.keyPart << "\"\n";
        std::cout << "  Modifiers: 0x" << std::hex << parsed.modifiers << std::dec << "\n";
        
        // Decode the modifier bits for clarity
        std::vector<std::string> activeMods;
        if (parsed.modifiers & (1 << 0)) activeMods.push_back("LCtrl");
        if (parsed.modifiers & (1 << 1)) activeMods.push_back("RCtrl");
        if (parsed.modifiers & (1 << 2)) activeMods.push_back("LShift");
        if (parsed.modifiers & (1 << 3)) activeMods.push_back("RShift");
        if (parsed.modifiers & (1 << 4)) activeMods.push_back("LAlt");
        if (parsed.modifiers & (1 << 5)) activeMods.push_back("RAlt");
        if (parsed.modifiers & (1 << 6)) activeMods.push_back("LMeta");
        if (parsed.modifiers & (1 << 7)) activeMods.push_back("RMeta");
        
        std::cout << "  Active modifiers: ";
        for (size_t i = 0; i < activeMods.size(); ++i) {
            if (i > 0) std::cout << " + ";
            std::cout << activeMods[i];
        }
        std::cout << "\n";
        
        std::cout << "  Wildcard: " << (parsed.wildcard ? "yes" : "no") << "\n";
        std::cout << "  Grab: " << (parsed.grab ? "yes" : "no") << "\n";
        std::cout << "  Repeat: " << (parsed.repeat ? "yes" : "no") << "\n";
        std::cout << "  Evdev: " << (parsed.isEvdev ? "yes" : "no") << "\n";
        std::cout << "\n";
    }

    std::cout << "Test completed successfully!\n";
    return 0;
}