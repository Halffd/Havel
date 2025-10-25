#pragma once

namespace havel::debugging {
    // Parser/AST debugging
    inline bool debug_ast = false;
    inline bool debug_parser = false;
    inline bool debug_lexer = false;
    
    // Runtime debugging
    inline bool debug_repl = false;
    inline bool debug_interpreter = false;
    
    // System debugging
    inline bool debug_hotkeys = false;
    inline bool debug_io = false;
    inline bool debug_evdev = false;
}