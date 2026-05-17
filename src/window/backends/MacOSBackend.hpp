#pragma once
#include "window/WindowBackend.hpp"

namespace havel {

class MacOSBackend : public WindowBackend {
public:
  MacOSBackend();
  ~MacOSBackend() override;

  DisplayServer getDisplayServer() const override;
  WindowManagerDetector::WMType getWMType() const override;
  std::string getWMName() const override;
  bool isWMSupported() const override;

  wID getActiveWindow() override;
  pID getActiveWindowPID() override;
  std::string getActiveWindowProcess() override;
  std::string getActiveWindowTitle() override;
  std::string getActiveWindowClass() override;

  pID getWindowPID(wID id) override;
  std::string getWindowTitle(wID id) override;
  std::string getWindowClass(wID id) override;
  Rect getWindowPosition(wID id) override;
  bool isWindowActive(wID id) override;
  bool isWindowExists(wID id) override;
  bool isWindowFullscreen(wID id) override;

  wID findWindowByPID(pID pid) override;
  wID findWindowByProcessName(const std::string &processName) override;
  wID findWindowByClass(const std::string &className) override;
  wID findWindowByTitle(const std::string &title) override;

  wID newWindow(const std::string &name, std::vector<int> *dimensions,
                bool hide) override;

  bool moveWindow(wID id, int x, int y) override;
  bool resizeWindow(wID id, int width, int height) override;
  bool moveResizeWindow(wID id, int x, int y, int width, int height) override;
  bool closeWindow(wID id) override;
  bool focusWindow(wID id) override;
  bool minimizeWindow(wID id) override;
  bool maximizeWindow(wID id) override;
  bool restoreWindow(wID id) override;
  bool hideWindow(wID id) override;
  bool showWindow(wID id) override;
  bool setWindowOpacity(wID id, float opacity) override;
  bool setWindowAlwaysOnTop(wID id, bool onTop) override;
  bool toggleWindowFullscreen(wID id) override;
  bool centerWindow(wID id) override;
  bool snapWindow(wID id, int position, int padding) override;
  bool setWindowFloating(wID id, bool floating) override;

  int getCurrentWorkspace() override;
  std::vector<WorkspaceInfo> getWorkspaces() override;
  bool switchToWorkspace(int workspace) override;
  bool moveWindowToWorkspace(wID id, int workspace) override;

  bool moveWindowToMonitor(wID id, int monitor) override;

  void startAltTab() override;
  void continueAltTab() override;
  void finishAltTab() override;

  std::vector<WindowInfo> getAllWindows() override;
  WindowInfo getWindowInfo(wID id) override;
  WindowInfo getActiveWindowInfo() override;

  std::string getProcessName(pid_t pid) override;
  std::string getProcessCmdline(pid_t pid) override;

  bool initialize() override;
  void shutdown() override;
};

} // namespace havel
