#pragma once
#include "WindowManagerDetector.hpp"
#include "WindowQuery.hpp"
#include "Rect.hpp"
#include "core/DisplayManager.hpp"
#include "types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace havel {

class WindowBackend {
public:
  virtual ~WindowBackend() = default;

  virtual DisplayServer getDisplayServer() const = 0;
  virtual WindowManagerDetector::WMType getWMType() const = 0;
  virtual std::string getWMName() const = 0;
  virtual bool isWMSupported() const = 0;

  virtual wID getActiveWindow() = 0;
  virtual pID getActiveWindowPID() = 0;
  virtual std::string getActiveWindowProcess() = 0;
  virtual std::string getActiveWindowTitle() = 0;
  virtual std::string getActiveWindowClass() = 0;

  virtual pID getWindowPID(wID id) = 0;
  virtual std::string getWindowTitle(wID id) = 0;
  virtual std::string getWindowClass(wID id) = 0;
  virtual Rect getWindowPosition(wID id) = 0;
  virtual bool isWindowActive(wID id) = 0;
  virtual bool isWindowExists(wID id) = 0;
  virtual bool isWindowFullscreen(wID id) = 0;

  virtual wID findWindowByPID(pID pid) = 0;
  virtual wID findWindowByProcessName(const std::string &processName) = 0;
  virtual wID findWindowByClass(const std::string &className) = 0;
  virtual wID findWindowByTitle(const std::string &title) = 0;

  virtual wID newWindow(const std::string &name,
                        std::vector<int> *dimensions = nullptr,
                        bool hide = false) = 0;

  virtual bool moveWindow(wID id, int x, int y) = 0;
  virtual bool resizeWindow(wID id, int width, int height) = 0;
  virtual bool moveResizeWindow(wID id, int x, int y, int width,
                                int height) = 0;
  virtual bool closeWindow(wID id) = 0;
  virtual bool focusWindow(wID id) = 0;
  virtual bool minimizeWindow(wID id) = 0;
  virtual bool maximizeWindow(wID id) = 0;
  virtual bool restoreWindow(wID id) = 0;
  virtual bool hideWindow(wID id) = 0;
  virtual bool showWindow(wID id) = 0;
  virtual bool setWindowOpacity(wID id, float opacity) = 0;
  virtual bool setWindowAlwaysOnTop(wID id, bool onTop) = 0;
  virtual bool toggleWindowFullscreen(wID id) = 0;
  virtual bool centerWindow(wID id) = 0;
  virtual bool snapWindow(wID id, int position, int padding = 0) = 0;
  virtual bool setWindowFloating(wID id, bool floating) = 0;

  virtual int getCurrentWorkspace() = 0;
  virtual std::vector<WorkspaceInfo> getWorkspaces() = 0;
  virtual bool switchToWorkspace(int workspace) = 0;
  virtual bool moveWindowToWorkspace(wID id, int workspace) = 0;

  virtual bool moveWindowToMonitor(wID id, int monitor) = 0;

  virtual void startAltTab() = 0;
  virtual void continueAltTab() = 0;
  virtual void finishAltTab() = 0;

  virtual std::vector<WindowInfo> getAllWindows() = 0;
  virtual WindowInfo getWindowInfo(wID id) = 0;
  virtual WindowInfo getActiveWindowInfo() = 0;

  virtual std::string getProcessName(pid_t pid) = 0;
  virtual std::string getProcessCmdline(pid_t pid) = 0;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;
};

} // namespace havel
