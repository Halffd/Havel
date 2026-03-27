#include "SignalHandler.hpp"
#include "EventListener.hpp"
#include "utils/Logger.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace havel {

SignalHandler *SignalHandler::instance = nullptr;

SignalHandler::SignalHandler(EventListener *listener) : listener(listener) {
  instance = this;
}

SignalHandler::~SignalHandler() {
  Shutdown();
  if (instance == this) {
    instance = nullptr;
  }
}

// Atomic flag for signal handling
static std::atomic<int> gSignalFlag{0};

void SignalHandler::SignalCleanupHandler(int sig) {
  // Only set flag - don't do heavy operations in signal handler
  gSignalFlag.store(sig, std::memory_order_relaxed);
}

int SignalHandler::GetSignalFlag() {
  return gSignalFlag.load(std::memory_order_relaxed);
}

void SignalHandler::InstallAsyncHandlers() {
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SignalCleanupHandler;

  // Catch all termination and interrupt signals
  sigaction(SIGINT, &sa, nullptr);    // Ctrl+C
  sigaction(SIGTERM, &sa, nullptr);   // Termination
  sigaction(SIGQUIT, &sa, nullptr);   // Quit
  sigaction(SIGABRT, &sa, nullptr);   // Abort
  sigaction(SIGSEGV, &sa, nullptr);   // Segmentation fault
  sigaction(SIGILL, &sa, nullptr);    // Illegal instruction
  sigaction(SIGFPE, &sa, nullptr);    // Floating point exception
  sigaction(SIGBUS, &sa, nullptr);    // Bus error
  sigaction(SIGPIPE, &sa, nullptr);   // Broken pipe
  sigaction(SIGALRM, &sa, nullptr);   // Alarm clock
  sigaction(SIGUSR1, &sa, nullptr);   // User-defined 1
  sigaction(SIGUSR2, &sa, nullptr);   // User-defined 2
  sigaction(SIGCHLD, &sa, nullptr);   // Child stopped/terminated
  sigaction(SIGCONT, &sa, nullptr);   // Continue if stopped
  sigaction(SIGSTOP, &sa, nullptr);   // Stop (cannot be caught, but try)
  sigaction(SIGTSTP, &sa, nullptr);   // Terminal stop
  sigaction(SIGTTIN, &sa, nullptr);   // Background read from terminal
  sigaction(SIGTTOU, &sa, nullptr);   // Background write to terminal
  sigaction(SIGURG, &sa, nullptr);    // Urgent condition on socket
  sigaction(SIGXCPU, &sa, nullptr);   // CPU time limit exceeded
  sigaction(SIGXFSZ, &sa, nullptr);   // File size limit exceeded
  sigaction(SIGVTALRM, &sa, nullptr); // Virtual timer expired
  sigaction(SIGPROF, &sa, nullptr);   // Profiling timer expired
  sigaction(SIGWINCH, &sa, nullptr);  // Window size change
  sigaction(SIGIO, &sa, nullptr);     // I/O now possible
  sigaction(SIGPWR, &sa, nullptr);    // Power failure
  sigaction(SIGSYS, &sa, nullptr);    // Bad system call

  debug("Traditional signal handlers installed for all signals");
}

bool SignalHandler::SetupSignalfd() {
  sigemptyset(&signalMask);
  
  // Block all catchable signals for signalfd
  sigaddset(&signalMask, SIGTERM);
  sigaddset(&signalMask, SIGINT);
  sigaddset(&signalMask, SIGHUP);
  sigaddset(&signalMask, SIGQUIT);
  sigaddset(&signalMask, SIGABRT);
  sigaddset(&signalMask, SIGSEGV);
  sigaddset(&signalMask, SIGILL);
  sigaddset(&signalMask, SIGFPE);
  sigaddset(&signalMask, SIGBUS);
  sigaddset(&signalMask, SIGPIPE);
  sigaddset(&signalMask, SIGALRM);
  sigaddset(&signalMask, SIGUSR1);
  sigaddset(&signalMask, SIGUSR2);
  sigaddset(&signalMask, SIGCHLD);
  sigaddset(&signalMask, SIGCONT);
  sigaddset(&signalMask, SIGTSTP);
  sigaddset(&signalMask, SIGTTIN);
  sigaddset(&signalMask, SIGTTOU);
  sigaddset(&signalMask, SIGURG);
  sigaddset(&signalMask, SIGXCPU);
  sigaddset(&signalMask, SIGXFSZ);
  sigaddset(&signalMask, SIGVTALRM);
  sigaddset(&signalMask, SIGPROF);
  sigaddset(&signalMask, SIGWINCH);
  sigaddset(&signalMask, SIGIO);
  sigaddset(&signalMask, SIGPWR);
  sigaddset(&signalMask, SIGSYS);

  if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) != 0) {
    error("Failed to block signals for signalfd");
    return false;
  }

  signalFd = signalfd(-1, &signalMask, SFD_CLOEXEC);
  if (signalFd == -1) {
    error("Failed to create signalfd: {}", strerror(errno));
    return false;
  }

  debug("Signal handling: signalfd created (traditional handlers are primary "
        "defense)");
  return true;
}

void SignalHandler::ProcessSignal() {
  if (signalFd < 0 || !listener) {
    return;
  }

  struct signalfd_siginfo si;
  ssize_t s = read(signalFd, &si, sizeof(si));
  if (s != sizeof(si)) {
    error("Failed to read from signalfd: {}", strerror(errno));
    return;
  }

  listener->HandleSignal(si.ssi_signo);
}

void SignalHandler::Shutdown() {
  if (signalFd >= 0) {
    close(signalFd);
    signalFd = -1;
  }
}

} // namespace havel
