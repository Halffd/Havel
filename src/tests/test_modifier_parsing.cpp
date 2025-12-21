#include <iostream>
#include <string>
#include <vector>
#include <cctype>

// Simple test structure to mimic ParsedHotkey
struct TestParsedHotkey {
    std::string keyPart;
    unsigned int modifiers = 0;
    bool isEvdev = false;
    bool isX11 = false;
    bool grab = true;
    bool suspend = false;
    bool repeat = true;
    bool wildcard = false;
};

// Test function that mimics the enhanced ParseModifiersAndFlags
TestParsedHotkey testParseModifiersAndFlags(const std::string &input, bool isEvdev) {
    TestParsedHotkey result;
    result.isEvdev = isEvdev;

    size_t i = 0;
    
    // Parse text-based modifiers (loop until no more found)
    std::vector<std::string> textModifiers = {"ctrl", "shift", "alt", "meta", "win"};
    bool foundTextModifier = false;
    
    do {
        foundTextModifier = false;
        
        for (const auto& mod : textModifiers) {
            if (i + mod.size() <= input.size() && 
                input.substr(i, mod.size()) == mod) {
                // Check if this is followed by '+' or end of string
                if (i + mod.size() == input.size() || 
                    input[i + mod.size()] == '+') {
                    // Found text modifier
                    if (mod == "ctrl") {
                        result.modifiers |= isEvdev ? (1 << 0) : 0x00000004; // ControlMask
                    } else if (mod == "shift") {
                        result.modifiers |= isEvdev ? (1 << 1) : 0x00000001; // ShiftMask
                    } else if (mod == "alt") {
                        result.modifiers |= isEvdev ? (1 << 2) : 0x00000008; // Mod1Mask
                    } else if (mod == "meta" || mod == "win") {
                        result.modifiers |= isEvdev ? (1 << 3) : 0x00000040; // Mod4Mask
                    }
                    
                    i += mod.size();
                    foundTextModifier = true;
                    
                    // Skip the '+' if present
                    if (i < input.size() && input[i] == '+') {
                        ++i;
                    }
                    
                    break; // Break inner for loop to restart with new position
                }
            }
        }
    } while (foundTextModifier);
    
    // If no text modifiers found, continue with original single-character parsing
    while (i < input.size()) {
        char c = input[i];

        // Check for escaped/doubled characters (e.g., "++", "##")
        if (i + 1 < input.size() && input[i] == input[i + 1]) {
            // Found doubled character - treat remaining as literal key
            // Skip first character and treat rest as key
            result.keyPart = input.substr(i + 1);
            return result;
        }

        switch (c) {
        case '@':
            result.isEvdev = true;
            break;
        case '%':
            result.isX11 = true;
            break;
        case '^':
            result.modifiers |= isEvdev ? (1 << 0) : 0x00000004; // ControlMask
            break;
        case '+':
            result.modifiers |= isEvdev ? (1 << 1) : 0x00000001; // ShiftMask
            break;
        case '!':
            result.modifiers |= isEvdev ? (1 << 2) : 0x00000008; // Mod1Mask
            break;
        case '#':
            result.modifiers |= isEvdev ? (1 << 3) : 0x00000040; // Mod4Mask
            break;
        case '*':
            // Wildcard - ignore all current modifiers but don't set repeat=false
            result.wildcard = true;
            break;
        case '|':
            result.repeat = false;
            break;
        case '~':
            result.grab = false;
            break;
        case '$':
            result.suspend = true;
            break;
        default:
            // Not a modifier - rest is the key part
            result.keyPart = input.substr(i);
            return result;
        }
        ++i;
    }

    // If we got here, entire string was modifiers
    result.keyPart = "";
    return result;
}

void testModifierParsing() {
    std::cout << "=== Testing Enhanced Modifier Parsing ===" << std::endl;
    
    // Test cases
    struct TestCase {
        std::string input;
        std::string expectedKeyPart;
        unsigned int expectedModifiers;
        std::string description;
    };
    
    std::vector<TestCase> testCases = {
        // Text-based modifiers (the main fix)
        {"ctrl+capslock", "capslock", 0x00000004, "ctrl+capslock should parse ctrl modifier and capslock key"},
        {"shift+capslock", "capslock", 0x00000001, "shift+capslock should parse shift modifier and capslock key"},
        {"alt+capslock", "capslock", 0x00000008, "alt+capslock should parse alt modifier and capslock key"},
        {"ctrl+shift+capslock", "capslock", 0x00000005, "ctrl+shift+capslock should parse both modifiers and capslock key"},
        
        // Original single-character modifiers (should still work)
        {"^c", "c", 0x00000004, "^c should parse ctrl modifier and c key"},
        {"+c", "c", 0x00000001, "+c should parse shift modifier and c key"},
        {"!c", "c", 0x00000008, "!c should parse alt modifier and c key"},
        {"#+c", "c", 0x00000041, "#+c should parse meta+shift modifiers and c key"},
        
        // Mixed cases
        {"ctrl+F1", "F1", 0x00000004, "ctrl+F1 should parse ctrl modifier and F1 key"},
        {"meta+win+space", "space", 0x00000040, "meta+win+space should parse meta modifier and space key"},
        
        // Edge cases
        {"capslock", "", 0, "capslock alone should have no modifiers"},
        {"ctrl", "", 0x00000004, "ctrl alone should have ctrl modifier and empty key part"}
    };
    
    bool allPassed = true;
    
    for (const auto& test : testCases) {
        auto result = testParseModifiersAndFlags(test.input, false);
        
        bool passed = (result.keyPart == test.expectedKeyPart && 
                      result.modifiers == test.expectedModifiers);
        
        std::cout << (passed ? "✓" : "✗") << " " << test.description << std::endl;
        std::cout << "  Input: \"" << test.input << "\"" << std::endl;
        std::cout << "  Expected key: \"" << test.expectedKeyPart << "\", got: \"" << result.keyPart << "\"" << std::endl;
        std::cout << "  Expected mods: 0x" << std::hex << test.expectedModifiers << ", got: 0x" << result.modifiers << std::dec << std::endl;
        
        if (!passed) {
            allPassed = false;
            std::cout << "  ❌ FAILED!" << std::endl;
        }
        std::cout << std::endl;
    }
    
    std::cout << "=== Test Results ===" << std::endl;
    if (allPassed) {
        std::cout << "✅ All tests passed! The text-based modifier parsing enhancement is working correctly." << std::endl;
    } else {
        std::cout << "❌ Some tests failed. Please check the implementation." << std::endl;
    }
}

int main() {
    testModifierParsing();
    return 0;
}