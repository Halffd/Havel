#ifndef AUTORUNNER_H
#define AUTORUNNER_H
#include "../utils/Utils.hpp"
#include "../window/WindowManager.hpp"
#include "../core/IO.hpp"
#include "../core/HotkeyManager.hpp"
#include <thread>
// Auto-runner class
namespace havel {
class AutoRunner {
private:
    bool running = false;
    std::thread runnerThread;
    IO& io;
    std::string direction;

public:
    AutoRunner(IO& ioRef) : io(ioRef) {}

    void start(const std::string& dir = "w") {
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

    void stop() {
        if (!running) return;

        running = false;

        // Release the key
        io.Send(direction + " up");

        // Clean up thread if needed
    }

    void toggle(const std::string& dir = "w") {
        if (running) {
            stop();
        } else {
            start(dir);
        }
    }

    bool isRunning() const {
        return running;
    }

    ~AutoRunner() {
        if (running) {
            stop();
        }
    }
};
} // namespace H
#endif //AUTORUNNER_H
