#include "havel-lang/runtime/HostAPI.hpp"
/*
 * InputModule.hpp
 *
 * Input handling module for Havel language.
 * Separates input execution logic from evaluator.
 */
#pragma once

#include "havel-lang/ast/AST.h"
#include <string>
#include <vector>

namespace havel {

// Forward declarations
class IO;

/**
 * InputModule - Execute input commands
 * 
 * Provides input command execution:
 * - Send text input
 * - Send key presses
 * - Mouse clicks
 * - Mouse movement
 */
class InputModule {
public:
    explicit InputModule(IO* io);
    ~InputModule() = default;

    /**
     * Execute input statement
     * @param node Input statement AST node
     */
    void execute(const ast::InputStatement& node);

private:
    IO* io;  // Non-owning pointer

    // Input command handlers
    void sendText(const std::string& text);
    void sendKey(const std::string& key);
    void mouseClick(const std::string& button);
    void mouseMove(int x, int y, bool relative);
};

} // namespace havel
