#ifndef AUTORUNNER_H
#define AUTORUNNER_H

#include <string>
#include <thread>

namespace havel {
    class IO; // Forward declaration
    class HotkeyManager; // Forward declaration

    class AutoRunner {
    private:
        bool running;
        std::thread runnerThread;
        IO& io;
        std::string direction;

    public:
        explicit AutoRunner(IO& ioRef);
        ~AutoRunner();

        void start(const std::string& dir = "w");
        void stop();
        void toggle(const std::string& dir = "w");
        [[nodiscard]] bool isRunning() const;
    };
}

#endif // AUTORUNNER_H