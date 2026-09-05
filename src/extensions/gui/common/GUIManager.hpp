#pragma once
#include "qt.hpp"

#include "core/window/WindowManager.hpp"
#include "host/ui/UIManager.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace havel {

/**
 * @brief GUIManager provides high-level GUI functionality for dialogs, menus,
 * and window effects
 *
 * Now delegates to the unified UIBackend system (QtBackend) instead of
 * directly using Qt. This resolves the duplication with host/ui/UIBackend.
 */
class GUIManager : public QObject {
  Q_OBJECT
public:
  GUIManager(WindowManager &windowMgr);
  ~GUIManager();

  // Initialize GUI (call after QApplication created)
  void initialize();
  
  // Reload GUI components
  void reload();

  // === MENU FUNCTIONS ===
  std::string showMenu(const std::string &title,
                       const std::vector<std::string> &options,
                       bool multiSelect = false);

  std::string showContextMenu(const std::vector<std::string> &options);

  // === INPUT DIALOGS ===
  std::string showInputDialog(const std::string &title,
                              const std::string &prompt = "",
                              const std::string &defaultValue = "");

  std::string showPasswordDialog(const std::string &title,
                                 const std::string &prompt = "");

  double showNumberDialog(const std::string &title,
                          const std::string &prompt = "",
                          double defaultValue = 0.0, double min = -1000000.0,
                          double max = 1000000.0, double step = 1.0);

  // === CUSTOM WINDOWS ===
  uint64_t createWindow(const std::string &title, const std::string &content,
                        int width = 400, int height = 300);

  void closeWindow(uint64_t windowId);

  void updateWindowContent(uint64_t windowId, const std::string &content);

  // === NOTIFICATION FUNCTIONS ===
  void showNotification(const std::string &title, const std::string &message,
                        const std::string &icon = "info", int durationMs = 0);

  // === WINDOW TRANSPARENCY ===
  bool setActiveWindowTransparency(double opacity);

  bool setWindowTransparency(uint64_t windowId, double opacity);

  bool setWindowTransparencyByTitle(const std::string &title, double opacity);

  // === DIALOG FUNCTIONS ===
  bool showConfirmDialog(const std::string &title, const std::string &message);

  std::string showFileDialog(const std::string &title,
                             const std::string &startDir = "",
                             const std::string &filter = "", bool save = false);

  std::string showDirectoryDialog(const std::string &title,
                                  const std::string &startDir = "");

  // === COLOR PICKER ===
  std::string showColorPicker(const std::string &title,
                              const std::string &defaultColor = "#ffffff");

private:
  WindowManager &windowManager;

  // Track custom windows (for createWindow/closeWindow/updateWindowContent)
  std::unordered_map<uint64_t, QWidget *> customWindows;
  uint64_t nextWindowId = 1;

  // Helper functions
  QWidget *getQWidgetForWindow(uint64_t windowId);
  host::UIBackend *getBackend();

  // Thread-safe notification implementation
  Q_INVOKABLE void showNotificationImpl(const QString &title, const QString &message,
                            const QString &icon, int durationMs);
};

} // namespace havel
