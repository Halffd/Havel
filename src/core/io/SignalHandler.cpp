#include "SignalHandler.hpp"
#include "EventListener.hpp"
#include "utils/Logger.hpp"
#include <cerrno>
#include <csignal>
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

void SignalHandler::SignalCleanupHandler(int sig) {
  if (instance && instance->listener) {
    instance->listener->ForceUngrabAllDevices();
  }
  _exit(sig);
}

void SignalHandler::InstallAsyncHandlers() {
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SignalCleanupHandler;

  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGQUIT, &sa, nullptr);

  debug(
      "Traditional signal handlers installed for SIGINT, SIGTERM, SIGABRT, "
      "SIGSEGV, SIGQUIT");
}

bool SignalHandler::SetupSignalfd() {
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGTERM);
  sigaddset(&signalMask, SIGINT);
  sigaddset(&signalMask, SIGHUP);
  sigaddset(&signalMask, SIGQUIT);

  if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) != 0) {
    error("Failed to block signals for signalfd");
    return false;
  }

  signalFd = signalfd(-1, &signalMask, SFD_CLOEXEC);
  if (signalFd == -1) {
    error("Failed to create signalfd: {}", strerror(errno));
    return false;
  }

  debug("Signal handling: signalfd created (traditional handlers are primary defense)");
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
