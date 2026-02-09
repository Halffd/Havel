#include "EventMonitor.hpp"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <iostream>

namespace havel {

// ModeManager Implementation
void ModeManager::SetMode(const std::string &mode) {
  std::lock_guard<std::mutex> lock(modeMutex);
  std::string oldMode = currentMode;
  currentMode = mode;

  if (modeChangeCallback) {
    modeChangeCallback(oldMode, mode);
  }
}

std::string ModeManager::GetMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return currentMode;
}

void ModeManager::SetModeChangeCallback(ModeCallback callback) {
  std::lock_guard<std::mutex> lock(modeMutex);
  modeChangeCallback = std::move(callback);
}

// KeyEventListener Implementation
void KeyEventListener::SetKeyDownCallback(KeyCallback callback) {
  std::lock_guard<std::mutex> lock(callbackMutex);
  keyDownCallback = std::move(callback);
}

void KeyEventListener::SetKeyUpCallback(KeyCallback callback) {
  std::lock_guard<std::mutex> lock(callbackMutex);
  keyUpCallback = std::move(callback);
}

void KeyEventListener::StartListening() {
  if (listening.load()) {
    return;
  }

  listening = true;
  listenerThread = std::make_unique<std::thread>([this]() {
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
      std::cerr << "Error: Cannot open X display" << std::endl;
      return;
    }

    // Use proper X11 Window type
    ::Window root = DefaultRootWindow(display);
    XSelectInput(display, root, KeyPressMask | KeyReleaseMask);

    XEvent event;
    while (listening.load()) {
      XNextEvent(display, &event);

      if (event.type == x11::XKeyPress) {
        KeyCode keyCode = event.xkey.keycode;
        // Use modern XKB functions instead of deprecated XKeycodeToKeysym
        KeySym keysym = XkbKeycodeToKeysym(display, keyCode, 0, 0);
        if (keysym == NoSymbol) {
          keysym = XKeycodeToKeysym(display, keyCode, 0); // Fallback
        }
        std::string keyName = XKeysymToString(keysym);

        std::lock_guard<std::mutex> lock(callbackMutex);
        if (keyDownCallback) {
          keyDownCallback(keyCode, true, keyName);
        }
      } else if (event.type == x11::XKeyRelease) {
        KeyCode keyCode = event.xkey.keycode;
        // Use modern XKB functions instead of deprecated XKeycodeToKeysym
        KeySym keysym = XkbKeycodeToKeysym(display, keyCode, 0, 0);
        if (keysym == NoSymbol) {
          keysym = XKeycodeToKeysym(display, keyCode, 0); // Fallback
        }
        std::string keyName = XKeysymToString(keysym);

        std::lock_guard<std::mutex> lock(callbackMutex);
        if (keyUpCallback) {
          keyUpCallback(keyCode, false, keyName);
        }
      }
    }

    XCloseDisplay(display);
  });
}

void KeyEventListener::StopListening() {
  listening = false;
  if (listenerThread && listenerThread->joinable()) {
    listenerThread->join();
  }
}

// UpdateLoopManager Implementation
int UpdateLoopManager::StartUpdateLoop(UpdateCallback callback,
                                       int intervalMs) {
  std::lock_guard<std::mutex> lock(loopMutex);
  int loopId = nextLoopId++;

  loopFlags[loopId] = true;
  updateLoops[loopId] =
      std::make_unique<std::thread>([callback, intervalMs, loopId, this]() {
        while (this->loopFlags[loopId].load()) {
          auto start = std::chrono::steady_clock::now();

          if (this->updateFunction) {
            this->updateFunction();
          } else if (callback) {
            callback();
          }

          auto end = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start);

          if (elapsed.count() < intervalMs) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(intervalMs - elapsed.count()));
          }
        }

        // Clean up
        std::lock_guard<std::mutex> innerLock(loopMutex);
        this->updateLoops.erase(loopId);
        this->loopFlags.erase(loopId);
      });

  return loopId;
}

void UpdateLoopManager::StopUpdateLoop(int loopId) {
  std::lock_guard<std::mutex> lock(loopMutex);
  auto it = loopFlags.find(loopId);
  if (it != loopFlags.end()) {
    it->second = false;
  }
}

void UpdateLoopManager::SetUpdateFunction(UpdateCallback callback) {
  std::lock_guard<std::mutex> lock(loopMutex);
  updateFunction = std::move(callback);
}

// EventMonitor Implementation
EventMonitor::EventMonitor(std::chrono::milliseconds pollInterval)
    : windowMonitor(pollInterval) {}

EventMonitor::~EventMonitor() { Stop(); }

void EventMonitor::SetMode(const std::string &mode) {
  modeManager.SetMode(mode);
}

std::string EventMonitor::GetMode() const { return modeManager.GetMode(); }

void EventMonitor::OnModeChange(
    std::function<void(const std::string &oldMode, const std::string &newMode)>
        callback) {
  modeManager.SetModeChangeCallback(std::move(callback));
}

void EventMonitor::OnKeyDown(
    std::function<void(int keyCode, const std::string &keyName)> callback) {
  // Convert to expected signature
  keyListener.SetKeyDownCallback(
      [callback](int keyCode, bool isDown, const std::string &keyName) {
        if (isDown) {
          callback(keyCode, keyName);
        }
      });
}

void EventMonitor::OnKeyUp(
    std::function<void(int keyCode, const std::string &keyName)> callback) {
  // Convert to expected signature
  keyListener.SetKeyUpCallback(
      [callback](int keyCode, bool isDown, const std::string &keyName) {
        if (!isDown) {
          callback(keyCode, keyName);
        }
      });
}

void EventMonitor::StartKeyListening() { keyListener.StartListening(); }

void EventMonitor::StopKeyListening() { keyListener.StopListening(); }

int EventMonitor::StartUpdateLoop(std::function<void()> callback,
                                  int intervalMs) {
  return updateManager.StartUpdateLoop(std::move(callback), intervalMs);
}

void EventMonitor::StopUpdateLoop(int loopId) {
  updateManager.StopUpdateLoop(loopId);
}

void EventMonitor::Start() { windowMonitor.Start(); }

void EventMonitor::Stop() {
  windowMonitor.Stop();
  StopKeyListening();
}

bool EventMonitor::IsRunning() const { return windowMonitor.IsRunning(); }

} // namespace havel
