#include "AutoRunner.h"
#include "../utils/Utils.hpp"
#include "../window/WindowManager.hpp"
#include "../core/IO.hpp"
#include "../core/HotkeyManager.hpp"
#include <chrono>

namespace havel {

AutoRunner::AutoRunner(IO& ioRef) : running(false), io(ioRef) {}

AutoRunner::~AutoRunner() {
    if (running) {
        stop();
    }
}

void AutoRunner::start(const std::string& dir) {
    if (running) return;

    direction = dir;
    running = true;

    // Press and hold the key
    io.Send(direction);

    // Start monitoring thread
    runnerThread = std::thread([this]() {
        while (running) {
            // Keep checking if window changed, etc.
            if (!HotkeyManager::isGamingWindow()) {
                stop();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    runnerThread.detach();
}

void AutoRunner::stop() {
    if (!running) return;

    running = false;

    // Release the key
    io.Send(direction + " up");
}

void AutoRunner::toggle(const std::string& dir) {
    if (running) {
        stop();
    } else {
        start(dir);
    }
}

bool AutoRunner::isRunning() const {
    return running;
}

} // namespace havel