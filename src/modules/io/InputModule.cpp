/*
 * InputModule.cpp
 *
 * Input handling module for Havel language.
 * Separates input execution logic from evaluator.
 */
#include "InputModule.hpp"
#include "core/IO.hpp"
#include "utils/Logger.hpp"

namespace havel {

InputModule::InputModule(IO* io) : io(io) {}

void InputModule::execute(const ast::InputStatement& node) {
    for (const auto& cmd : node.commands) {
        switch (cmd.type) {
            case ast::InputCommand::SendText:
                sendText(cmd.text);
                break;

            case ast::InputCommand::SendKey:
                sendKey(cmd.key);
                break;

            case ast::InputCommand::MouseClick:
                mouseClick(cmd.text);
                break;

            // Note: MouseMove, MouseRelative, MouseWheel, MouseClickAt require
            // expression evaluation which is handled by the evaluator.
            // This module handles simple text/key/click commands.
            case ast::InputCommand::MouseMove:
            case ast::InputCommand::MouseRelative:
            case ast::InputCommand::MouseWheel:
            case ast::InputCommand::MouseClickAt:
                // These require expression evaluation - handled by evaluator
                break;

            case ast::InputCommand::Sleep:
                // Sleep handled by evaluator
                break;
        }
    }
}

void InputModule::sendText(const std::string& text) {
    if (io) {
        io->Send(text.c_str());
    } else {
        warn("InputModule: IO not available for sendText");
    }
}

void InputModule::sendKey(const std::string& key) {
    if (io) {
        io->Send(("{ " + key + " }").c_str());
    } else {
        warn("InputModule: IO not available for sendKey");
    }
}

void InputModule::mouseClick(const std::string& button) {
    if (!io) {
        warn("InputModule: IO not available for mouseClick");
        return;
    }

    if (button == "left" || button == "lmb") {
        io->MouseClick(1);
    } else if (button == "right" || button == "rmb") {
        io->MouseClick(2);
    } else if (button == "middle" || button == "mmb") {
        io->MouseClick(3);
    } else {
        warn("InputModule: Unknown mouse button '{}'", button);
    }
}

} // namespace havel
