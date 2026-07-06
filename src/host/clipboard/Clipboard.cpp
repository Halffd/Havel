/*
 * Clipboard.cpp
 *
 * Core clipboard implementation - minimal overhead.
 */
#include "Clipboard.hpp"

#ifdef HAVE_QT_EXTENSION
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QList>
#include <QMimeData>
#include <QUrl>
#endif
#include <cstdlib>
#include <chrono>
#include <thread>
#include <future>
#include <mutex>

namespace havel::host {

Clipboard::Clipboard() {
  // Auto-detect best method
  method_ = detectBestMethod();
}

// Run external command with timeout (in milliseconds)
std::string Clipboard::runWithTimeout(const std::string& cmd, int timeoutMs) const {
  std::promise<std::string> promise;
  auto future = promise.get_future();
  
  std::thread([this, &promise, cmd]() {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      promise.set_value("");
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
    promise.set_value(result);
  }).detach();
  
  if (future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
    return future.get();
  }
  return "";
}

bool Clipboard::setTextWithTimeout(const std::string& text, const std::string& cmd, int timeoutMs) const {
  std::promise<bool> promise;
  auto future = promise.get_future();
  
  std::thread([this, &promise, text, cmd]() {
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
      promise.set_value(false);
      return;
    }
    fputs(text.c_str(), pipe);
    int ret = pclose(pipe);
    promise.set_value(ret == 0);
  }).detach();
  
  if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
    return future.get();
  }
  return false;
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

  // Default to Qt if available
  if (QGuiApplication::instance()) {
    return Method::QT;
  }

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
  method_ = method;
  // Re-initialize clipboard with new method
  if (method_ == Method::QT) {
    if (QGuiApplication::instance()) {
      clipboard_ = QGuiApplication::clipboard();
    }
  } else {
    // For other methods, we don't need a cached pointer
    clipboard_ = nullptr;
  }
}

std::string Clipboard::getText() const {
    // 1. Try backend first
    if (backend_) {
        std::string result = backend_->getText();
        if (!result.empty()) return result;
    }
    
    // 1. Qt (if built and available)
    if (method_ == Method::QT || method_ == Method::AUTO) {
        std::string result = getTextQt();
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
    // 1. Try backend first
    if (backend_) {
        if (backend_->setText(text)) return true;
    }
    
    // 1. Qt (if built and available)
    if (method_ == Method::QT || method_ == Method::AUTO) {
        if (setTextQt(text)) return true;
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

std::string Clipboard::getTextQt() const {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return "";
  }
  return cb->text().toStdString();
}

bool Clipboard::setTextQt(const std::string &text) {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return false;
  }
  cb->setText(QString::fromStdString(text));
  return true;
}

// ============================================================================
// X11 Implementation (using external commands)
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
    if (backend_) return backend_->getImage();
    // Platform-specific image clipboard support
    switch (method_) {
  case Method::QT:
    return getImageQt();
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
    if (backend_) return backend_->setImage(base64Png);
    switch (method_) {
  case Method::QT:
    return setImageQt(base64Png);
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

bool Clipboard::hasImage() const { return !getImage().empty(); }

// Qt Image Implementation
std::string Clipboard::getImageQt() const {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return "";
  }

  QImage image = cb->image();
  if (image.isNull()) {
    return "";
  }

  QByteArray byteArray;
  QBuffer buffer(&byteArray);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return byteArray.toBase64().toStdString();
}

bool Clipboard::setImageQt(const std::string &base64Png) {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return false;
  }

  QByteArray byteArray = QByteArray::fromBase64(QString::fromStdString(base64Png).toUtf8());
  QImage image;
  image.loadFromData(byteArray, "PNG");
  if (image.isNull()) {
    return false;
  }
  cb->setImage(image);
  return true;
}


} // namespace havel::host