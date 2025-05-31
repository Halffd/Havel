#pragma once
#include <string>
#include <cstdint>

namespace havel {

class Theme {
public:
    struct Colors {
        // Base colors
        uint32_t background{0x282c34};
        uint32_t foreground{0xabb2bf};
        uint32_t accent{0x61afef};
        uint32_t warning{0xe5c07b};
        uint32_t error{0xe06c75};
        
        // Additional colors
        uint32_t selection{0x3e4451};
        uint32_t highlight{0x2c313c};
        uint32_t border{0x181a1f};
        uint32_t shadow{0x0000007f};
        
        // Widget colors
        uint32_t buttonNormal{0x353b45};
        uint32_t buttonHover{0x3e4451};
        uint32_t buttonPress{0x2c313c};
        uint32_t inputBackground{0x1b1d23};
        uint32_t tooltipBackground{0x21252b};
    };
    
    struct Fonts {
        std::string regular{"Sans:size=10"};
        std::string bold{"Sans:bold:size=10"};
        std::string mono{"Monospace:size=10"};
        std::string title{"Sans:size=12:weight=bold"};
        std::string small{"Sans:size=8"};
        
        float dpi{96.0f};
        float scaling{1.0f};
    };
    
    struct Metrics {
        int padding{8};
        int margin{4};
        int borderWidth{1};
        int cornerRadius{4};
        int shadowRadius{8};
        int iconSize{16};
        int minButtonWidth{80};
        int minButtonHeight{24};
        float opacity{0.95f};
    };
    
    Colors colors;
    Fonts fonts;
    Metrics metrics;
};

} // namespace havel 