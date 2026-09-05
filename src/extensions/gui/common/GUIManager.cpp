#include "GUIManager.hpp"
#include "qt.hpp"
#include "utils/Logger.hpp"
#include "host/ui/UIManager.hpp"
#include "host/ui/UIBackend.hpp"
#include "host/ui/QtBackend.hpp"
#include <QApplication>
#include <QCursor>
#include <QMetaObject>

namespace havel {

GUIManager::GUIManager(WindowManager &windowMgr)
    : QObject(nullptr), windowManager(windowMgr) {
  debug("GUIManager created (delegating to UIBackend)");
}

GUIManager::~GUIManager() {
  // Clean up any open custom windows
  for (auto &[id, widget] : customWindows) {
    if (widget) {
      widget->close();
      delete widget;
    }
  }
  customWindows.clear();
}

void GUIManager::initialize() {
  debug("GUIManager initialized (using UIBackend)");
}

void GUIManager::reload() {
  debug("GUIManager reload() called");
}

host::UIBackend *GUIManager::getBackend() {
  auto &uiManager = host::UIManager::instance();
  return uiManager.backend();
}

// === MENU FUNCTIONS ===

std::string GUIManager::showMenu(const std::string &title,
                                 const std::vector<std::string> &options,
                                 bool multiSelect) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showMenu(title, options, multiSelect);
}

std::string GUIManager::showContextMenu(const std::vector<std::string> &options) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showContextMenu(options);
}

// === INPUT DIALOGS ===

std::string GUIManager::showInputDialog(const std::string &title,
                                        const std::string &prompt,
                                        const std::string &defaultValue) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showInputDialog(title, prompt, defaultValue);
}

std::string GUIManager::showPasswordDialog(const std::string &title,
                                           const std::string &prompt) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showPasswordDialog(title, prompt);
}

double GUIManager::showNumberDialog(const std::string &title,
                                    const std::string &prompt,
                                    double defaultValue, double min, double max,
                                    double step) {
  auto *backend = getBackend();
  if (!backend) return defaultValue;
  return backend->showNumberDialog(title, prompt, defaultValue, min, max, step);
}

// === CUSTOM WINDOWS ===
// These remain in GUIManager as they manage Qt widgets directly
uint64_t GUIManager::createWindow(const std::string &title, const std::string &content,
                                  int width, int height) {
  // This creates a Qt widget directly - kept for backward compatibility
  auto *backend = getBackend();
  if (!backend) return 0;

  // Use the unified UI backend to create a window
  auto window = backend->window(title);
  if (!window) return 0;

  // Add content to window
  auto textElem = backend->text(content);
  if (textElem) {
    window->add(textElem);
  }

  backend->realize(window);
  backend->show(window);

  // Store for tracking
  uint64_t id = nextWindowId++;
  customWindows[id] = nullptr; // We don't track the Qt widget directly here
  return id;
}

void GUIManager::closeWindow(uint64_t windowId) {
  // Custom windows are managed by the UI backend now
  (void)windowId;
}

void GUIManager::updateWindowContent(uint64_t windowId, const std::string &content) {
  // Custom windows are managed by the UI backend now
  (void)windowId;
  (void)content;
}

// === NOTIFICATION FUNCTIONS ===

void GUIManager::showNotification(const std::string &title, const std::string &message,
                                  const std::string &icon, int durationMs) {
  auto *backend = getBackend();
  if (!backend) return;
  backend->showNotification(title, message, icon, durationMs);
}

void GUIManager::showNotificationImpl(const QString &title, const QString &message,
                            const QString &icon, int durationMs) {
  // This is called via QMetaObject::invokeMethod for thread-safe notification
  auto *backend = getBackend();
  if (!backend) return;
  backend->showNotification(title.toStdString(), message.toStdString(), icon.toStdString(), durationMs);
}

// === WINDOW TRANSPARENCY ===

bool GUIManager::setActiveWindowTransparency(double opacity) {
  auto *backend = getBackend();
  if (!backend) return false;
  return backend->setActiveWindowTransparency(opacity);
}

bool GUIManager::setWindowTransparency(uint64_t windowId, double opacity) {
  auto *backend = getBackend();
  if (!backend) return false;
  return backend->setWindowTransparencyById(windowId, opacity);
}

bool GUIManager::setWindowTransparencyByTitle(const std::string &title, double opacity) {
  auto *backend = getBackend();
  if (!backend) return false;
  return backend->setWindowTransparencyByTitle(title, opacity);
}

// === DIALOG FUNCTIONS ===

bool GUIManager::showConfirmDialog(const std::string &title, const std::string &message) {
  auto *backend = getBackend();
  if (!backend) return false;
  return backend->showConfirmDialog(title, message);
}

std::string GUIManager::showFileDialog(const std::string &title,
                                       const std::string &startDir,
                                       const std::string &filter, bool save) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showFileDialog(title, startDir, filter, save);
}

std::string GUIManager::showDirectoryDialog(const std::string &title,
                                            const std::string &startDir) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showDirectoryDialog(title, startDir);
}

// === COLOR PICKER ===

std::string GUIManager::showColorPicker(const std::string &title,
                                        const std::string &defaultColor) {
  auto *backend = getBackend();
  if (!backend) return "";
  return backend->showColorPicker(title, defaultColor);
}

QWidget *GUIManager::getQWidgetForWindow(uint64_t windowId) {
  // Not used anymore - UI backend manages widgets
  (void)windowId;
  return nullptr;
}

} // namespace havel
