/*
 * Clipboard.hpp
 *
 * Core clipboard operations - NO history, NO monitoring.
 * Just get/set/clear with minimal overhead.
 *
 * Uses Qt internally but doesn't leak Qt types.
 */
#pragma once

#include "ClipboardInfo.hpp"
#include "IClipboardBackend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace havel::host {

/**
 * Clipboard - Minimal clipboard access
 *
 * No history, no monitoring, no callbacks.
 * Just the basics with zero overhead.
 */
class Clipboard {
public:
  // Clipboard backend methods
  enum class Method {
    AUTO,     // Automatically select best available method
    QT,       // Qt clipboard (default on Qt-based systems)
    X11,      // X11 clipboard (XGetSelectionOwner/XSetSelectionOwner)
    WAYLAND,  // Wayland clipboard (wl_data_device_manager)
    EXTERNAL, // External command (xclip, wl-copy, etc.)
    WINDOWS,  // Windows clipboard API
    MACOS     // macOS NSPasteboard
  };

  Clipboard();
  ~Clipboard();

  // Backend strategy
  void setBackend(std::unique_ptr<IClipboardBackend> backend);
  IClipboardBackend* backend() const;
  bool hasBackend() const { return backend_ != nullptr; }

  // =========================================================================
  // Method selection
  // =========================================================================

  /// Set clipboard method
  void setMethod(Method method);

  /// Get current clipboard method
  Method getMethod() const { return method_; }

  /// Detect best available method for current platform
  static Method detectBestMethod();

  // =========================================================================
  // Core operations - minimal overhead
  // =========================================================================

  /// Get clipboard text
  std::string getText() const;

  /// Set clipboard text
  bool setText(const std::string &text);

  /// Clear clipboard
  bool clear();

  /// Check if clipboard has text
  bool hasText() const;

  // Aliases for convenience
  std::string get() const { return getText(); }
  std::string in() const { return getText(); }
  ClipboardInfo out() const { return getInfo(); }

  // Get full clipboard information
  ClipboardInfo getInfo() const;

  // =========================================================================
  // Image support
  // =========================================================================

  /// Get clipboard image as base64-encoded PNG
  std::string getImage() const;

  /// Set clipboard image from base64-encoded PNG
  bool setImage(const std::string &base64Png);

  /// Check if clipboard has image
  bool hasImage() const;

  // =========================================================================
  // File support
  // =========================================================================

  /// Get clipboard files as list of paths
  std::vector<std::string> getFiles() const;

  /// Set clipboard files (list of paths)
  bool setFiles(const std::vector<std::string> &paths);

  /// Check if clipboard has files
  bool hasFiles() const;

private:
  std::unique_ptr<IClipboardBackend> backend_;
  void *clipboard_ = nullptr;
  Method method_ = Method::AUTO;

  // Platform-specific implementations
#ifdef HAVE_QT_EXTENSION
  std::string getTextQt() const;
  std::string getImageQt() const;
  std::vector<std::string> getFilesQt() const;
  bool setTextQt(const std::string &text);
  bool setImageQt(const std::string &base64Png);
  bool setFilesQt(const std::vector<std::string> &paths);
#endif
  std::string getTextX11() const;
  std::string getTextWayland() const;
  std::string getTextExternal() const;
  std::string getTextWindows() const;
  std::string getTextMacOS() const;

  bool setTextX11(const std::string &text);
  bool setTextWayland(const std::string &text);
  bool setTextExternal(const std::string &text);
  bool setTextWindows(const std::string &text);
  bool setTextMacOS(const std::string &text);

  // Timeout helper functions
  std::string runWithTimeout(const std::string& cmd, int timeoutMs = 2000) const;
  bool setTextWithTimeout(const std::string& text, const std::string& cmd, int timeoutMs) const;
};

} // namespace havel::host
