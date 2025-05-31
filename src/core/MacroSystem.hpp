#pragma once
#include <string>
#include <vector>
#include <functional>

namespace havel {

class MacroSystem {
public:
    MacroSystem();
    ~MacroSystem();
    
    void RecordMacro(const std::string& name);
    void StopRecording();
    void PlayMacro(const std::string& name);
    
private:
    struct MacroAction {
        enum ActionType { 
            KeyPressAction, 
            KeyReleaseAction, 
            MouseMoveAction, 
            MouseClickAction 
        };
        ActionType type;
        int data1, data2;
        unsigned long timestamp;
    };
    
    struct Macro {
        std::string name;
        std::vector<MacroAction> actions;
    };
    
    std::vector<Macro> macros;
    bool recording = false;
    std::string currentMacro;
};

} // namespace H 