/*
 * Clipboard.cpp
 *
 * Core clipboard implementation - minimal overhead.
 */
#include "Clipboard.hpp"
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QList>
#include <QMimeData>
#include <QUrl>
#include <cstdlib>

namespace havel::host {

Clipboard::Clipboard() {
  // Auto-detect best method
  method_ = detectBestMethod();
}

Clipboard::~Clipboard() {}

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

std::string Clipboard::getText() const {
  switch (method_) {
  case Method::QT:
    return getTextQt();
  case Method::X11:
    return getTextX11();
  case Method::WAYLAND:
    return getTextWayland();
  case Method::EXTERNAL:
    return getTextExternal();
  case Method::WINDOWS:
    return getTextWindows();
  case Method::MACOS:
    return getTextMacOS();
  case Method::AUTO:
  default:
    // Auto-detect and retry
    const_cast<Clipboard *>(this)->method_ = detectBestMethod();
    return getText();
  }
}

bool Clipboard::setText(const std::string &text) {
  switch (method_) {
  case Method::QT:
    return setTextQt(text);
  case Method::X11:
    return setTextX11(text);
  case Method::WAYLAND:
    return setTextWayland(text);
  case Method::EXTERNAL:
    return setTextExternal(text);
  case Method::WINDOWS:
    return setTextWindows(text);
  case Method::MACOS:
    return setTextMacOS(text);
  case Method::AUTO:
  default:
    // Auto-detect and retry
    method_ = detectBestMethod();
    return setText(text);
  }
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
  // Use xclip or xsel if available
  FILE *pipe = popen(
      "xclip -selection clipboard -o 2>/dev/null || xsel -b 2>/dev/null", "r");
  if (!pipe) {
    return "";
  }

  char buffer[4096];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  pclose(pipe);

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

bool Clipboard::setTextX11(const std::string &text) {
  // Use xclip or xsel if available
  FILE *pipe = popen(
      "xclip -selection clipboard 2>/dev/null || xsel -b 2>/dev/null", "w");
  if (!pipe) {
    return false;
  }

  fputs(text.c_str(), pipe);
  pclose(pipe);
  return true;
}

// ============================================================================
// Wayland Implementation (using external commands)
// ============================================================================

std::string Clipboard::getTextWayland() const {
  // Use wl-paste if available
  FILE *pipe = popen("wl-paste 2>/dev/null", "r");
  if (!pipe) {
    return "";
  }

  char buffer[4096];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  pclose(pipe);

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

bool Clipboard::setTextWayland(const std::string &text) {
  // Use wl-copy if available
  FILE *pipe = popen("wl-copy 2>/dev/null", "w");
  if (!pipe) {
    return false;
  }

  fputs(text.c_str(), pipe);
  pclose(pipe);
  return true;
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
  // macOS pbcopy/pbpaste commands
  FILE *pipe = popen("pbpaste 2>/dev/null", "r");
  if (!pipe) {
    return "";
  }

  char buffer[4096];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  pclose(pipe);

  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

bool Clipboard::setTextMacOS(const std::string &text) {
  // macOS pbcopy command
  FILE *pipe = popen("pbcopy 2>/dev/null", "w");
  if (!pipe) {
    return false;
  }

  fputs(text.c_str(), pipe);
  pclose(pipe);
  return true;
}

// ============================================================================
// Image Support
// ============================================================================

std::string Clipboard::getImage() const {
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
  return QByteArray::fromRawData(byteArray.constData(), byteArray.size())
      .toBase64()
      .toStdString();
}

bool Clipboard::setImageQt(const std::string &base64Png) {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return false;
  }

  QByteArray byteArray =
      QByteArray::fromBase64(QByteArray::fromStdString(base64Png));
  QImage image;
  image.loadFromData(byteArray, "PNG");
  if (image.isNull()) {
    return false;
  }

  cb->setImage(image);
  return true;
}

// ============================================================================
// File Support
// ============================================================================

std::vector<std::string> Clipboard::getFiles() const {
  // Platform-specific file clipboard support
  switch (method_) {
  case Method::QT:
    return getFilesQt();
  case Method::X11:
  case Method::WAYLAND:
  case Method::EXTERNAL:
  case Method::WINDOWS:
  case Method::MACOS:
  case Method::AUTO:
  default:
    // Not yet implemented for these methods
    return {};
  }
}

bool Clipboard::setFiles(const std::vector<std::string> &paths) {
  switch (method_) {
  case Method::QT:
    return setFilesQt(paths);
  case Method::X11:
  case Method::WAYLAND:
  case Method::EXTERNAL:
  case Method::WINDOWS:
  case Method::MACOS:
  case Method::AUTO:
  default:
    // Not yet implemented for these methods
    (void)paths;
    return false;
  }
}

bool Clipboard::hasFiles() const { return !getFiles().empty(); }

// ============================================================================
// ClipboardInfo Support
// ============================================================================

ClipboardInfo Clipboard::getInfo() const {
  ClipboardInfo info;

  // Try to get text first
  std::string text = getText();
  if (!text.empty()) {
    info.type = ClipboardInfo::Type::TEXT;
    info.content = text;
    info.size = text.size();
    info.mimeType = "text/plain";
    return info;
  }

  // Try to get image
  std::string image = getImage();
  if (!image.empty()) {
    info.type = ClipboardInfo::Type::IMAGE;
    info.content = image;
    info.size = image.size();
    info.mimeType = "image/png";
    return info;
  }

  // Try to get files
  std::vector<std::string> files = getFiles();
  if (!files.empty()) {
    info.type = ClipboardInfo::Type::FILES;
    info.files = files;
    info.size = 0; // Size would require stat calls
    info.mimeType = "text/uri-list";
    return info;
  }

  // Empty clipboard
  info.type = ClipboardInfo::Type::EMPTY;
  return info;
}

// Qt File Implementation
std::vector<std::string> Clipboard::getFilesQt() const {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return {};
  }

  const QMimeData *mimeData = cb->mimeData();
  if (!mimeData || !mimeData->hasUrls()) {
    return {};
  }

  std::vector<std::string> files;
  for (const QUrl &url : mimeData->urls()) {
    if (url.isLocalFile()) {
      files.push_back(url.toLocalFile().toStdString());
    }
  }
  return files;
}

bool Clipboard::setFilesQt(const std::vector<std::string> &paths) {
  auto *cb = static_cast<QClipboard *>(clipboard_);
  if (!cb && QGuiApplication::instance()) {
    cb = QGuiApplication::clipboard();
  }
  if (!cb) {
    return false;
  }

  QList<QUrl> urls;
  for (const auto &path : paths) {
    urls.append(QUrl::fromLocalFile(QString::fromStdString(path)));
  }

  QMimeData *mimeData = new QMimeData();
  mimeData->setUrls(urls);
  cb->setMimeData(mimeData);
  return true;
}

} // namespace havel::host
