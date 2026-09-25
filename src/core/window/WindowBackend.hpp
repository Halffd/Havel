#pragma once
#include "WindowManagerDetector.hpp"
#include "WindowQuery.hpp"
#include "Rect.hpp"
#include "core/display/DisplayManager.hpp"
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
  virtual bool raiseWindow(wID) { return false; }
  virtual bool lowerWindow(wID) { return false; }

  // EWMH _NET_WM_STATE manipulations. Default: unsupported (false / "no").
  virtual bool setWindowSticky(wID, bool) { return false; }
  virtual bool isWindowSticky(wID) { return false; }
  virtual bool setWindowShaded(wID, bool) { return false; }
  virtual bool isWindowShaded(wID) { return false; }
  virtual bool setWindowSkipTaskbar(wID, bool) { return false; }
  virtual bool isWindowSkipTaskbar(wID) { return false; }
  virtual bool setWindowSkipPager(wID, bool) { return false; }
  virtual bool isWindowSkipPager(wID) { return false; }
  virtual bool setWindowDecorated(wID, bool) { return false; }
  virtual bool isWindowDecorated(wID) { return true; }
  virtual bool getWindowOpacity(wID, double &outOpacity) { outOpacity = 1.0; return false; }
  virtual bool getWindowFrameExtents(wID, int &l, int &r, int &t, int &b) {
    l = r = t = b = 0; return false;
  }
  virtual std::string getWindowType(wID) { return "normal"; }
  virtual bool setWindowOnAllDesktops(wID) { return false; }
  virtual int getWindowDesktop(wID) { return -1; }
  virtual bool terminateWindow(wID) { return false; }
  virtual bool killWindowClient(wID) { return false; }
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

  // ... plus the EWMH / Motif block declared above near the top ...

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
