#include "SignalWatcher.hpp"
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace havel::util {

void SignalWatcher::logSignal(int sig) {
    const char* signame = "Unknown";
    switch (sig) {
        case SIGINT:  signame = "SIGINT"; break;
        case SIGTERM: signame = "SIGTERM"; break;
        case SIGHUP:  signame = "SIGHUP"; break;
        case SIGQUIT: signame = "SIGQUIT"; break;
    }
    std::cerr << "[SignalWatcher] Received signal: " << signame << " (" << sig << ")\n";
}

SignalWatcher::~SignalWatcher() {
    stop();
}

void SignalWatcher::start() {
    if (watcherThread.joinable()) {
        throw std::runtime_error("SignalWatcher already running");
    }

    watcherThread = std::thread([this]() {
        // Block all signals in this thread initially
        blockAllSignals();
        
        // Only wait for the signals we care about
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGHUP);
        sigaddset(&set, SIGQUIT);
        
        int sig;
        while (!shouldExit.load(std::memory_order_relaxed)) {
            // This blocks until one of our signals arrives
            int result = sigwait(&set, &sig);
            if (result == 0) {
                logSignal(sig);
                if (sig == SIGINT || sig == SIGTERM) {
                    shouldExit.store(true, std::memory_order_relaxed);
                    break;
                }
            } else if (errno != EINTR) {
                std::error_code ec(errno, std::system_category());
                std::cerr << "[SignalWatcher] sigwait failed: " << ec.message() << "\n";
                break;
            }
        }
    });
}

void SignalWatcher::stop() {
    if (watcherThread.joinable()) {
        // Send ourselves a signal to wake up sigwait
        pthread_kill(watcherThread.native_handle(), SIGTERM);
        watcherThread.join();
    }
}

void blockAllSignals() {
    sigset_t set;
    sigfillset(&set);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to block signals");
    }
}

void blockSignals(const std::initializer_list<int>& signals) {
    sigset_t set;
    sigemptyset(&set);
    for (int sig : signals) {
        sigaddset(&set, sig);
    }
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
        throw std::system_error(errno, std::system_category(), "Failed to block signals");
    }
}

} // namespace Havel::Util
