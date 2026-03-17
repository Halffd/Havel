#pragma once

#include <csignal>

namespace havel {

class EventListener;

class SignalHandler {
public:
  explicit SignalHandler(EventListener *listener);
  ~SignalHandler();

  void InstallAsyncHandlers();
  bool SetupSignalfd();
  void ProcessSignal();
  void Shutdown();

  int GetSignalFd() const { return signalFd; }
  static int GetSignalFlag();

private:
  static SignalHandler *instance;
  static void SignalCleanupHandler(int sig);

  EventListener *listener = nullptr;
  int signalFd = -1;
  sigset_t signalMask{};
};

} // namespace havel
