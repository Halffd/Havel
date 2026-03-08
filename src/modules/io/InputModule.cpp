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

            case ast::InputCommand::MouseMove:
                mouseMove(cmd.mouseX, cmd.mouseY, false);
                break;

            case ast::InputCommand::MouseMoveRelative:
                mouseMove(cmd.mouseX, cmd.mouseY, true);
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

void InputModule::mouseMove(int x, int y, bool relative) {
    if (!io) {
        warn("InputModule: IO not available for mouseMove");
        return;
    }

    if (relative) {
        io->MouseMove(x, y);
    } else {
        io->MouseMoveTo(x, y);
    }
}

} // namespace havel
