/*
 * Clipboard.cpp
 *
 * Core clipboard implementation - minimal overhead.
 */
#include "Clipboard.hpp"
#include "ClipboardBackendFactory.hpp"

#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <future>
#include <mutex>

namespace havel::host {

Clipboard::Clipboard() {
  // Auto-detect best method
  method_ = detectBestMethod();
}

void Clipboard::ensureBackend() const {
  if (backend_) {
    return;
  }
  // The registered backend is the Qt clipboard. Method::X11/WAYLAND/EXTERNAL/
  // WINDOWS/MACOS pin a non-registry path, which is what the old inline Qt
  // branch respected.
  if (method_ != Method::AUTO && method_ != Method::QT) {
    return;
  }
  if (!hasClipboardBackendFactory()) {
    return;
  }
  backend_ = createRegisteredClipboardBackend();
}

// Run external command with timeout (in milliseconds)
std::string Clipboard::runWithTimeout(const std::string& cmd, int timeoutMs) const {
#if defined(_WIN32)
  auto promise = std::make_shared<std::promise<std::string>>();
  auto future = promise->get_future();

  std::thread([promise, cmd]() {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      promise->set_value("");
      return;
    }
    char buffer[4096];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') {
      result.pop_back();
    }
    promise->set_value(result);
  }).detach();

  if (future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
    return future.get();
  }
  return "";
#else
  // The promise is shared with the detached thread: the thread may still be
  // working after this function gave up on the future, and a set_value on a
  // promise whose object was already destroyed is undefined.
  auto promise = std::make_shared<std::promise<std::string>>();
  auto future = promise->get_future();

  std::thread([promise, cmd, timeoutMs]() {
    int fds[2];
    if (pipe(fds) != 0) {
      promise->set_value("");
      return;
    }
    const pid_t pid = fork();
    if (pid < 0) {
      close(fds[0]);
      close(fds[1]);
      promise->set_value("");
      return;
    }
    if (pid == 0) {
      close(fds[0]);
      dup2(fds[1], STDOUT_FILENO);
      close(fds[1]);
      execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }
    close(fds[1]);

    // Read through poll() with a deadline instead of blocking in fgets, and
    // never park in pclose/waitpid on a live child: a thread blocked under
    // libc's FILE lock (fgets) or inside pclose (which holds the lock while
    // waiting for the child) deadlocks _IO_flush_all at process exit. Both
    // were observed with `xclip -o` hanging on the X selection.
    const int fd = fds[0];
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    char buffer[4096];
    std::string result;
    bool timedOut = false;
    while (true) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) {
        timedOut = true;
        break;
      }
      pollfd pfd{fd, POLLIN, 0};
      const int ready = poll(&pfd, 1, static_cast<int>(remaining));
      if (ready < 0) {
        if (errno == EINTR) continue;
        timedOut = true;
        break;
      }
      if (ready == 0) {
        timedOut = true;
        break;
      }
      const ssize_t n = read(fd, buffer, sizeof(buffer));
      if (n <= 0) {
        break; // EOF or error
      }
      result.append(buffer, static_cast<size_t>(n));
    }
    close(fd);

    // Kill the child if it outlived the read, then reap with a short grace
    // period. Nothing here blocks indefinitely.
    if (timedOut) {
      kill(pid, SIGKILL);
    }
    int status = 0;
    bool reaped = false;
    const auto reapDeadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < reapDeadline) {
      if (waitpid(pid, &status, WNOHANG) == pid) {
        reaped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!reaped) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // a SIGKILLed child reaps promptly
    }

    if (!timedOut && !result.empty() && result.back() == '\n') {
      result.pop_back();
    }
    promise->set_value(timedOut ? std::string() : result);
  }).detach();

  if (future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
    return future.get();
  }
  return "";
#endif
}

bool Clipboard::setTextWithTimeout(const std::string& text, const std::string& cmd, int timeoutMs) const {
#if defined(_WIN32)
  // Shared promise, same reason as runWithTimeout.
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  std::thread([promise, text, cmd]() {
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
      promise->set_value(false);
      return;
    }
    fputs(text.c_str(), pipe);
    int ret = pclose(pipe);
    promise->set_value(ret == 0);
  }).detach();

  if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
    return future.get();
  }
  return false;
#else
  // Shared promise, same reason as runWithTimeout. Same poll/kill/reap
  // structure: a thread parked on a live child deadlocks process exit.
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  std::thread([promise, text, cmd, timeoutMs]() {
    int fds[2];
    if (pipe(fds) != 0) {
      promise->set_value(false);
      return;
    }
    const pid_t pid = fork();
    if (pid < 0) {
      close(fds[0]);
      close(fds[1]);
      promise->set_value(false);
      return;
    }
    if (pid == 0) {
      close(fds[0]);
      dup2(fds[1], STDIN_FILENO);
      close(fds[1]);
      execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }
    close(fds[1]);

    const int fd = fds[0];
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    size_t written = 0;
    while (written < text.size()) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) {
        timedOut = true;
        break;
      }
      pollfd pfd{fd, POLLOUT, 0};
      const int ready = poll(&pfd, 1, static_cast<int>(remaining));
      if (ready < 0) {
        if (errno == EINTR) continue;
        timedOut = true;
        break;
      }
      if (ready == 0) {
        timedOut = true;
        break;
      }
      const ssize_t n = write(fd, text.data() + written, text.size() - written);
      if (n <= 0) {
        timedOut = true;
        break;
      }
      written += static_cast<size_t>(n);
    }
    // Closing the write end is what tells a stdin-reading command to finish;
    // the read end is unused here but must not leak.
    close(fd);

    if (timedOut) {
      kill(pid, SIGKILL);
    }
    int status = 0;
    bool reaped = false;
    const auto reapDeadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < reapDeadline) {
      if (waitpid(pid, &status, WNOHANG) == pid) {
        reaped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!reaped) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // a SIGKILLed child reaps promptly
    }

    promise->set_value(!timedOut);
  }).detach();

  if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
    return future.get();
  }
  return false;
#endif
}

Clipboard::Method Clipboard::detectBestMethod() {
  // Check environment variables for display server
  const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  const char *display = std::getenv("DISPLAY");

  if (waylandDisplay) {
    // Wayland session
    return Method::WAYLAND;
  } else if (display) {
    // X11 session
    return Method::X11;
  }

  // No Qt check here: the Qt backend arrives through the registry, and
  // AUTO tries it first via ensureBackend, so Qt is still preferred when the
  // Qt bridge is installed. Without a QApplication the Qt backend reports
  // empty and AUTO falls through, exactly as the old inline check did.

  // Fallback to external commands
  return Method::EXTERNAL;
}

Clipboard::~Clipboard() = default;

void Clipboard::setBackend(std::unique_ptr<IClipboardBackend> backend) {
    backend_ = std::move(backend);
}

IClipboardBackend* Clipboard::backend() const {
    return backend_.get();
}

void Clipboard::setMethod(Method method) {
  // The registered backend (the Qt one) handles Method::QT; there is no
  // Qt-specific pointer to refresh here any more.
  method_ = method;
}

std::string Clipboard::getText() const {
    // 1. Try backend first (explicit, or lazily created from the registry)
    ensureBackend();
    if (backend_) {
        std::string result = backend_->getText();
        if (!result.empty()) return result;
    }

    // 2. Wayland native
    if (method_ == Method::WAYLAND || method_ == Method::AUTO) {
        std::string result = getTextWayland();
        if (!result.empty()) return result;
    }
    
    // 3. X11 native
    if (method_ == Method::X11 || method_ == Method::AUTO) {
        std::string result = getTextX11();
        if (!result.empty()) return result;
    }
    
    // 4. External commands (copyq, xclip, wl-copy)
    if (method_ == Method::EXTERNAL || method_ == Method::AUTO) {
        std::string result = getTextExternal();
        if (!result.empty()) return result;
    }
    
    // Platform-specific fallback
    return getTextExternal();
}

bool Clipboard::setText(const std::string &text) {
    // 1. Try backend first (explicit, or lazily created from the registry)
    ensureBackend();
    if (backend_) {
        if (backend_->setText(text)) return true;
    }

    // 2. Wayland native
    if (method_ == Method::WAYLAND || method_ == Method::AUTO) {
        if (setTextWayland(text)) return true;
    }
    
    // 3. X11 native
    if (method_ == Method::X11 || method_ == Method::AUTO) {
        if (setTextX11(text)) return true;
    }
    
    // 5. External commands (copyq, xclip, wl-copy)
    if (method_ == Method::EXTERNAL || method_ == Method::AUTO) {
        if (setTextExternal(text)) return true;
    }
    
    // Platform-specific fallback
    return setTextExternal(text);
}

bool Clipboard::clear() { return setText(""); }

bool Clipboard::hasText() const { return !getText().empty(); }

// ============================================================================
// Qt Implementation
// ============================================================================
// The Qt implementation lives in src/extensions/qt/QtClipboardBackend.hpp and
// arrives here through ClipboardBackendFactory.hpp; ensureBackend() creates it
// lazily, so there is nothing Qt-specific left in this file.

// ============================================================================

std::string Clipboard::getTextX11() const {
  // Use xclip or xsel if available with timeout
  return runWithTimeout("xclip -selection clipboard -o 2>/dev/null || xsel -b 2>/dev/null", 2000);
}

bool Clipboard::setTextX11(const std::string &text) {
  // Use xclip or xsel if available with timeout
  return setTextWithTimeout(text, "xclip -selection clipboard 2>/dev/null || xsel -b 2>/dev/null", 2000);
}

// ============================================================================
// Wayland Implementation (using external commands)
// ============================================================================

std::string Clipboard::getTextWayland() const {
  // Use wl-paste if available with timeout
  return runWithTimeout("wl-paste 2>/dev/null", 2000);
}

bool Clipboard::setTextWayland(const std::string &text) {
  // Use wl-copy if available with timeout
  return setTextWithTimeout(text, "wl-copy 2>/dev/null", 2000);
}

// ============================================================================
// External Commands Implementation (cross-platform fallback)
// ============================================================================

std::string Clipboard::getTextExternal() const {
  // Try platform-specific external commands
#if defined(_WIN32)
  return getTextWindows();
#elif defined(__APPLE__)
  return getTextMacOS();
#else
  // Linux/Unix - try Wayland first, then X11
  std::string result = getTextWayland();
  if (!result.empty()) {
    return result;
  }
  return getTextX11();
#endif
}

bool Clipboard::setTextExternal(const std::string &text) {
  // Try platform-specific external commands
#if defined(_WIN32)
  return setTextWindows(text);
#elif defined(__APPLE__)
  return setTextMacOS(text);
#else
  // Linux/Unix - try Wayland first, then X11
  if (setTextWayland(text)) {
    return true;
  }
  return setTextX11(text);
#endif
}

// ============================================================================
// Windows Implementation
// ============================================================================

std::string Clipboard::getTextWindows() const {
  // Windows clipboard API would go here
  // For now, return empty string as placeholder
  return "";
}

bool Clipboard::setTextWindows(const std::string &text) {
  // Windows clipboard API would go here
  // For now, return false as placeholder
  (void)text;
  return false;
}

// ============================================================================
// macOS Implementation
// ============================================================================

std::string Clipboard::getTextMacOS() const {
  // macOS pbcopy/pbpaste commands with timeout
  return runWithTimeout("pbpaste 2>/dev/null", 2000);
}

bool Clipboard::setTextMacOS(const std::string &text) {
  // macOS pbcopy command with timeout
  return setTextWithTimeout(text, "pbcopy 2>/dev/null", 2000);
}

// ============================================================================
// Image Support
// ============================================================================

std::string Clipboard::getImage() const {
    ensureBackend();
    if (backend_) return backend_->getImage();
    // Platform-specific image clipboard support
    switch (method_) {
  case Method::X11:
  case Method::WAYLAND:
  case Method::EXTERNAL:
  case Method::WINDOWS:
  case Method::MACOS:
  case Method::AUTO:
  default:
    // Not yet implemented for these methods
    return "";
  }
}

bool Clipboard::setImage(const std::string &base64Png) {
    ensureBackend();
    if (backend_) return backend_->setImage(base64Png);
    switch (method_) {
  case Method::X11:
  case Method::WAYLAND:
  case Method::EXTERNAL:
  case Method::WINDOWS:
  case Method::MACOS:
  case Method::AUTO:
  default:
    // Not yet implemented for these methods
    (void)base64Png;
    return false;
  }
}

} // namespace havel::host