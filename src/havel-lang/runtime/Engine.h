/*
 * Engine.h - Stubbed (interpreter removed)
 * Use bytecode VM directly instead.
 */
#pragma once
#include <string>

namespace havel {

// Stubbed - use bytecode VM instead
struct EngineConfig {};

class Engine {
public:
    Engine(const EngineConfig&) {}
    
    // Stubbed - use bytecode VM instead
    void* RunScript(const std::string&) { return nullptr; }
    void* ExecuteCode(const std::string&) { return nullptr; }
    void* ExecuteJIT(const std::string&) { return nullptr; }
};

} // namespace havel
