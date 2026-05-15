#pragma once

#include "utils/DebugFlags.hpp"

namespace havel::debugging {

// Parser/AST debugging
inline bool debug_ast = false;
inline bool debug_parser = false;
inline bool debug_lexer = false;

// Runtime debugging
inline bool debug_repl = false;
inline bool debug_interpreter = false;
inline bool debug_gc = false;
inline bool debug_engine = false;

}
