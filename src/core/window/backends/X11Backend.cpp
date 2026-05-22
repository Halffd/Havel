#include "core/window/backends/X11Backend.hpp"
#include "utils/Logger.hpp"
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace havel {

X11Backend::X11Backend() {
  wmName = detector.GetWMName();
  wmType = detector.Detect();
}

X11Backend::~X11Backend() { shutdown(); }

DisplayServer X11Backend::getDisplayServer() const {
  return DisplayServer::X11;
}

WindowManagerDetector::WMType X11Backend::getWMType() const {
  return wmType;
}

std::string X11Backend::getWMName() const { return wmName; }
bool X11Backend::isWMSupported() const { return wmSupported; }

bool X11Backend::initialize() {
  wmSupported = true;
  return InitializeX11();
}

void X11Backend::shutdown() {}

bool X11Backend::InitializeX11() {
  DisplayManager::Initialize();
  return DisplayManager::GetDisplay() != nullptr;
}

std::string X11Backend::ReadProcFile(const std::string &path) const {
  std::ifstream file(path);
  if (!file.is_open()) return "";
  std::string content;
  std::getline(file, content);
  return content;
}

std::optional<X11Backend::ActiveWindowContext> X11Backend::GetActiveWindowContext() {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return std::nullopt;
  ::Window root = DisplayManager::GetRootWindow();
  if (!root) return std::nullopt;
  wID activeWindowId = getActiveWindow();
  if (!activeWindowId) return std::nullopt;
  return ActiveWindowContext{display, root, activeWindowId};
}

wID X11Backend::getActiveWindow() {
  Display *display = DisplayManager::GetDisplay();
  if (!display) {
    if (!InitializeX11()) return 0;
    display = DisplayManager::GetDisplay();
  }

  Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", x11::XFalse);
  if (activeWindowAtom == x11::XNone) return 0;

  Atom actualType;
  int actualFormat;
  unsigned long nitems, bytesAfter;
  unsigned char *prop = nullptr;
  Window activeWindow = 0;

  if (XGetWindowProperty(display, DefaultRootWindow(display), activeWindowAtom,
                          0, 1, x11::XFalse, XA_WINDOW, &actualType,
                          &actualFormat, &nitems, &bytesAfter,
                          &prop) == x11::XSuccess) {
    if (prop) {
      activeWindow = *reinterpret_cast<Window *>(prop);
      XFree(prop);
    }
  }
  return activeWindow;
}

pID X11Backend::getActiveWindowPID() {
  wID active_win = getActiveWindow();
  if (active_win == 0) return 0;
  return getWindowPID(active_win);
}

std::string X11Backend::getActiveWindowProcess() {
  pID pid = getActiveWindowPID();
  return (pid == 0) ? "" : ReadProcFile("/proc/" + std::to_string(pid) + "/comm");
}

std::string X11Backend::getActiveWindowTitle() {
  wID active_win = getActiveWindow();
  if (active_win == 0) return "";
  return getWindowTitle(active_win);
}

std::string X11Backend::getActiveWindowClass() {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return "";

  ::Window focusedWindow;
  int revertTo;
  if (XGetInputFocus(display, &focusedWindow, &revertTo) == 0) return "";
  if (focusedWindow == x11::XNone) return "";

  XClassHint classHint;
  if (XGetClassHint(display, focusedWindow, &classHint) == 0) return "";

  std::string className(classHint.res_class);
  XFree(classHint.res_name);
  XFree(classHint.res_class);
  return className;
}

std::string X11Backend::getWindowTitle(wID id) {
  if (id == 0) return "";
  Display *display = DisplayManager::GetDisplay();
  if (!display) return "";
  char *windowName = nullptr;
  if (XFetchName(display, id, &windowName) && windowName) {
    std::string title(windowName);
    XFree(windowName);
    return title;
  }
  return "";
}

pID X11Backend::getWindowPID(wID id) {
  if (id == 0) return 0;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return 0;

  Atom pidAtom = XInternAtom(display, "_NET_WM_PID", x11::XTrue);
  if (pidAtom == x11::XNone) return 0;

  Atom actualType;
  int actualFormat;
  unsigned long nItems, bytesAfter;
  unsigned char *propPID = nullptr;
  pID pid = 0;

  if (XGetWindowProperty(display, id, pidAtom, 0, 1, x11::XFalse,
                          XA_CARDINAL, &actualType, &actualFormat, &nItems,
                          &bytesAfter, &propPID) == x11::XSuccess) {
    if (nItems > 0) pid = *reinterpret_cast<pID *>(propPID);
    if (propPID) XFree(propPID);
  }
  return pid;
}

std::string X11Backend::getWindowClass(wID id) {
  if (id == 0) return "";
  Display *display = DisplayManager::GetDisplay();
  if (!display) return "";

  XClassHint classHint;
  if (XGetClassHint(display, id, &classHint) == 0) return "";

  std::string windowClass;
  if (classHint.res_class) {
    windowClass = classHint.res_class;
    XFree(classHint.res_class);
  }
  if (classHint.res_name) {
    if (!windowClass.empty()) windowClass += ":";
    windowClass += classHint.res_name;
    XFree(classHint.res_name);
  }
  return windowClass;
}

Rect X11Backend::getWindowPosition(wID id) {
  if (!id) return {};
  Display *display = DisplayManager::GetDisplay();
  if (!display) return {};

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(display, id, &attrs)) return {};

  int screenX, screenY;
  ::Window child;
  if (!XTranslateCoordinates(display, id, DefaultRootWindow(display),
                              0, 0, &screenX, &screenY, &child)) {
    return {attrs.x, attrs.y, attrs.width, attrs.height};
  }
  return {screenX, screenY, attrs.width, attrs.height};
}

bool X11Backend::isWindowActive(wID id) { return getActiveWindow() == id; }

bool X11Backend::isWindowExists(wID id) {
  if (!id) return false;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return false;
  XWindowAttributes attr;
  return XGetWindowAttributes(display, id, &attr) != 0;
}

bool X11Backend::isWindowFullscreen(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;

  Atom fsAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", false);
  Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", false);
  Atom actualType;
  int actualFormat;
  unsigned long nItems, bytesAfter;
  unsigned char *prop = nullptr;

  if (XGetWindowProperty(display, id, stateAtom, 0, 1024, false,
                          AnyPropertyType, &actualType, &actualFormat, &nItems,
                          &bytesAfter, &prop) != x11::XSuccess || !prop)
    return false;

  bool isFullscreen = false;
  Atom *states = (Atom *)prop;
  for (unsigned long i = 0; i < nItems; ++i) {
    if (states[i] == fsAtom) { isFullscreen = true; break; }
  }
  XFree(prop);
  return isFullscreen;
}

wID X11Backend::findWindowByPID(pID pid) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { if (!InitializeX11()) return 0; display = DisplayManager::GetDisplay(); }
  ::Window root = DisplayManager::GetRootWindow();
  ::Window rootReturn, parentReturn;
  ::Window *childrenReturn;
  unsigned int nChildren;
  if (!XQueryTree(display, root, &rootReturn, &parentReturn, &childrenReturn, &nChildren)) return 0;

  wID result = 0;
  Atom pidAtom = XInternAtom(display, "_NET_WM_PID", x11::XTrue);
  if (pidAtom == x11::XNone) { XFree(childrenReturn); return 0; }

  for (unsigned int i = 0; i < nChildren && result == 0; i++) {
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char *propPID = nullptr;
    if (XGetWindowProperty(display, childrenReturn[i], pidAtom, 0, 1, x11::XFalse,
                            XA_CARDINAL, &actualType, &actualFormat, &nItems,
                            &bytesAfter, &propPID) == x11::XSuccess) {
      if (nItems > 0 && *reinterpret_cast<pID *>(propPID) == pid) result = childrenReturn[i];
      if (propPID) XFree(propPID);
    }
  }
  XFree(childrenReturn);
  return result;
}

wID X11Backend::findWindowByProcessName(const std::string &processName) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { if (!InitializeX11()) return 0; display = DisplayManager::GetDisplay(); }
  ::Window root = DisplayManager::GetRootWindow();
  ::Window rootReturn, parentReturn;
  ::Window *childrenReturn;
  unsigned int nChildren;
  if (!XQueryTree(display, root, &rootReturn, &parentReturn, &childrenReturn, &nChildren)) return 0;

  wID result = 0;
  Atom pidAtom = XInternAtom(display, "_NET_WM_PID", x11::XTrue);
  if (pidAtom == x11::XNone) { XFree(childrenReturn); return 0; }

  for (unsigned int i = 0; i < nChildren && result == 0; i++) {
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char *propPID = nullptr;
    if (XGetWindowProperty(display, childrenReturn[i], pidAtom, 0, 1, x11::XFalse,
                            XA_CARDINAL, &actualType, &actualFormat, &nItems,
                            &bytesAfter, &propPID) == x11::XSuccess) {
      if (nItems > 0) {
        pID pid = *reinterpret_cast<pID *>(propPID);
        std::string name = getProcessName(pid);
        if (!name.empty() && name.find(processName) != std::string::npos) result = childrenReturn[i];
      }
      if (propPID) XFree(propPID);
    }
  }
  XFree(childrenReturn);
  return result;
}

wID X11Backend::findWindowByClass(const std::string &className) {
  return X11Backend::findWindowByTitle(className);
}

wID X11Backend::findWindowByTitle(const std::string &title) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { if (!InitializeX11()) return 0; display = DisplayManager::GetDisplay(); }

  ::Window rootWindow = DefaultRootWindow(display);
  ::Window parent;
  ::Window *children;
  unsigned int numChildren;
  if (!XQueryTree(display, rootWindow, &rootWindow, &parent, &children, &numChildren)) return 0;

  Atom nameAtom = XInternAtom(display, "_NET_WM_NAME", x11::XFalse);
  Atom utf8Atom = XInternAtom(display, "UTF8_STRING", x11::XFalse);
  wID result = 0;

  if (children) {
    for (unsigned int i = 0; i < numChildren && result == 0; i++) {
      XTextProperty windowName;
      if (XGetWMName(display, children[i], &windowName) && windowName.value) {
        std::string wTitle(reinterpret_cast<char *>(windowName.value));
        XFree(windowName.value);
        if (wTitle.find(title) != std::string::npos) { result = children[i]; break; }
      }
      if (nameAtom != x11::XNone && utf8Atom != x11::XNone) {
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = nullptr;
        if (XGetWindowProperty(display, children[i], nameAtom, 0, 1024, x11::XFalse,
                                utf8Atom, &actualType, &actualFormat, &nitems,
                                &bytesAfter, &prop) == x11::XSuccess && prop) {
          std::string wTitle(reinterpret_cast<char *>(prop));
          if (wTitle.find(title) != std::string::npos) result = children[i];
          XFree(prop);
        }
      }
    }
    XFree(children);
  }
  return result;
}

wID X11Backend::newWindow(const std::string &name, std::vector<int> *dimensions, bool hide) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { if (!InitializeX11()) return 0; display = DisplayManager::GetDisplay(); }

  int screen = DefaultScreen(display);
  ::Window root = RootWindow(display, screen);
  int x = 0, y = 0, width = 800, height = 600;
  if (dimensions && dimensions->size() == 4) {
    x = (*dimensions)[0]; y = (*dimensions)[1];
    width = (*dimensions)[2]; height = (*dimensions)[3];
  }

  ::Window newWin = XCreateSimpleWindow(display, root, x, y, width, height, 1,
                                         BlackPixel(display, screen), WhitePixel(display, screen));
  XStoreName(display, newWin, name.c_str());
  if (!hide) XMapWindow(display, newWin);
  XFlush(display);
  return reinterpret_cast<wID>(newWin);
}

bool X11Backend::moveWindow(wID id, int x, int y) { return moveResizeWindow(id, x, y, -1, -1); }
bool X11Backend::resizeWindow(wID id, int width, int height) { return moveResizeWindow(id, -1, -1, width, height); }

bool X11Backend::moveResizeWindow(wID id, int x, int y, int width, int height) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(display, id, &attrs)) return false;

  int finalX = (x == -1) ? attrs.x : x;
  int finalY = (y == -1) ? attrs.y : y;
  int finalW = (width == -1) ? attrs.width : (width > 0 ? width : 1);
  int finalH = (height == -1) ? attrs.height : (height > 0 ? height : 1);

  Atom moveresize = XInternAtom(display, "_NET_MOVERESIZE_WINDOW", x11::XTrue);
  if (moveresize != x11::XNone) {
    XEvent ev = {};
    ev.xclient.type = x11::XClientMessage;
    ev.xclient.window = id;
    ev.xclient.message_type = moveresize;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);
    ev.xclient.data.l[1] = finalX;
    ev.xclient.data.l[2] = finalY;
    ev.xclient.data.l[3] = finalW;
    ev.xclient.data.l[4] = finalH;
    if (XSendEvent(display, DefaultRootWindow(display), x11::XFalse,
                    SubstructureRedirectMask | SubstructureNotifyMask, &ev)) {
      XFlush(display); std::this_thread::sleep_for(std::chrono::milliseconds(100));
      int actualX, actualY; ::Window child;
      XTranslateCoordinates(display, id, DefaultRootWindow(display), 0, 0, &actualX, &actualY, &child);
      if (abs(actualX - finalX) < 50 && abs(actualY - finalY) < 50) return true;
    }
  }

  XMoveResizeWindow(display, id, finalX, finalY, finalW, finalH);
  XFlush(display);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int actualX, actualY; ::Window child;
  XTranslateCoordinates(display, id, DefaultRootWindow(display), 0, 0, &actualX, &actualY, &child);
  if (abs(actualX - finalX) < 50 && abs(actualY - finalY) < 50) return true;

  XWindowChanges changes;
  changes.x = finalX; changes.y = finalY;
  changes.width = finalW; changes.height = finalH;
  changes.stack_mode = Above;
  XConfigureWindow(display, id, CWX | CWY | CWWidth | CWHeight | CWStackMode, &changes);
  XEvent configureEvent = {};
  configureEvent.xconfigure.type = x11::XConfigureNotify;
  configureEvent.xconfigure.event = id;
  configureEvent.xconfigure.window = id;
  configureEvent.xconfigure.x = finalX;
  configureEvent.xconfigure.y = finalY;
  configureEvent.xconfigure.width = finalW;
  configureEvent.xconfigure.height = finalH;
  configureEvent.xconfigure.border_width = attrs.border_width;
  configureEvent.xconfigure.above = x11::XNone;
  configureEvent.xconfigure.override_redirect = attrs.override_redirect;
  XSendEvent(display, id, x11::XFalse, StructureNotifyMask, &configureEvent);
  XFlush(display);
  return true;
}

bool X11Backend::closeWindow(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return false;
  XEvent event;
  event.type = x11::XClientMessage;
  event.xclient.window = id;
  event.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", false);
  event.xclient.format = 32;
  event.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", false);
  event.xclient.data.l[1] = CurrentTime;
  XSendEvent(display, id, false, NoEventMask, &event);
  XFlush(display);
  return true;
}

bool X11Backend::focusWindow(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return false;
  Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", x11::XTrue);
  if (activeAtom != x11::XNone) {
    XEvent event = {};
    event.xclient.type = x11::XClientMessage;
    event.xclient.window = id;
    event.xclient.message_type = activeAtom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, DefaultRootWindow(display), x11::XFalse,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display); return true;
  }
  XSetInputFocus(display, id, RevertToPointerRoot, CurrentTime);
  XFlush(display);
  return true;
}

bool X11Backend::minimizeWindow(wID id) {
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XIconifyWindow(ctx->display, ctx->activeWindowId, DefaultScreen(ctx->display));
  XFlush(ctx->display);
  return true;
}

bool X11Backend::maximizeWindow(wID id) {
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XEvent event;
  event.type = x11::XClientMessage;
  event.xclient.window = ctx->activeWindowId;
  event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", false);
  event.xclient.format = 32;
  event.xclient.data.l[0] = 1;
  event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_MAXIMIZED_VERT", false);
  event.xclient.data.l[2] = XInternAtom(ctx->display, "_NET_WM_STATE_MAXIMIZED_HORZ", false);
  event.xclient.data.l[3] = 0; event.xclient.data.l[4] = 0;
  XSendEvent(ctx->display, ctx->root, false, SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XFlush(ctx->display);
  return true;
}

bool X11Backend::restoreWindow(wID id) {
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XEvent event;
  event.type = x11::XClientMessage;
  event.xclient.window = ctx->activeWindowId;
  event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", false);
  event.xclient.format = 32;
  event.xclient.data.l[0] = 0;
  event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_MAXIMIZED_VERT", false);
  event.xclient.data.l[2] = XInternAtom(ctx->display, "_NET_WM_STATE_MAXIMIZED_HORZ", false);
  event.xclient.data.l[3] = 0; event.xclient.data.l[4] = 0;
  XSendEvent(ctx->display, ctx->root, false, SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XFlush(ctx->display);
  return true;
}

bool X11Backend::hideWindow(wID id) {
  if (!id) return false;
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XUnmapWindow(ctx->display, id);
  XFlush(ctx->display);
  return true;
}

bool X11Backend::showWindow(wID id) {
  if (!id) return false;
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XMapWindow(ctx->display, id);
  XFlush(ctx->display);
  return true;
}

bool X11Backend::setWindowOpacity(wID id, float opacity) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;
  unsigned long opacity_long = static_cast<unsigned long>(opacity * 4294967295.0f);
  Atom opacityAtom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", x11::XFalse);
  if (opacityAtom == x11::XNone) return false;
  XChangeProperty(display, id, opacityAtom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&opacity_long, 1);
  XFlush(display);
  return true;
}

bool X11Backend::setWindowAlwaysOnTop(wID id, bool onTop) {
  auto ctx = GetActiveWindowContext();
  if (!ctx) return false;
  XEvent event;
  event.type = x11::XClientMessage;
  event.xclient.window = ctx->activeWindowId;
  event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", false);
  event.xclient.format = 32;
  event.xclient.data.l[0] = onTop ? 1 : 0;
  event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_ABOVE", false);
  event.xclient.data.l[2] = 0; event.xclient.data.l[3] = 0; event.xclient.data.l[4] = 0;
  XSendEvent(ctx->display, ctx->root, false, SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XFlush(ctx->display);
  return true;
}

bool X11Backend::toggleWindowFullscreen(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;
  Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", x11::XFalse);
  Atom fsAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", x11::XFalse);
  if (stateAtom == x11::XNone || fsAtom == x11::XNone) return false;
  XEvent ev{};
  ev.xclient.type = x11::XClientMessage;
  ev.xclient.window = id;
  ev.xclient.message_type = stateAtom;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 0; ev.xclient.data.l[1] = fsAtom;
  ev.xclient.data.l[2] = 0; ev.xclient.data.l[3] = 1; ev.xclient.data.l[4] = 0;
  XSendEvent(display, DefaultRootWindow(display), false,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(display);
  return true;
}

bool X11Backend::centerWindow(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;
  auto monitor = DisplayManager::GetPrimaryMonitor();
  XWindowAttributes attrs;
  if (!XGetWindowAttributes(display, id, &attrs)) return false;
  return moveWindow(id, monitor.x + (monitor.width - attrs.width) / 2,
                    monitor.y + (monitor.height - attrs.height) / 2);
}

bool X11Backend::snapWindow(wID id, int position, int padding) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return false;
  ::Window win = id ? id : getActiveWindow();
  if (!win) return false;

  ::Window root = DisplayManager::GetRootWindow();
  XWindowAttributes root_attrs;
  XGetWindowAttributes(display, root, &root_attrs);
  const int screenW = root_attrs.width - padding * 2;
  const int screenH = root_attrs.height - padding * 2;

  switch (position) {
    case 1: XMoveResizeWindow(display, win, padding, padding, screenW / 2, screenH); break;
    case 2: XMoveResizeWindow(display, win, screenW / 2 + padding, padding, screenW / 2, screenH); break;
    default: return false;
  }
  XFlush(display);
  return true;
}

bool X11Backend::setWindowFloating(wID, bool) { return false; }

int X11Backend::getCurrentWorkspace() {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return 1;
  ::Window root = DisplayManager::GetRootWindow();
  Atom desktopAtom = XInternAtom(display, "_NET_CURRENT_DESKTOP", x11::XFalse);
  if (desktopAtom == x11::XNone) return 1;
  Atom actualType; int actualFormat;
  unsigned long nitems, bytesAfter;
  unsigned char *data = nullptr;
  if (XGetWindowProperty(display, root, desktopAtom, 0, 1, x11::XFalse, XA_CARDINAL,
                          &actualType, &actualFormat, &nitems, &bytesAfter,
                          &data) == x11::XSuccess) {
    int desktop = data ? *reinterpret_cast<int *>(data) : 1;
    if (data) XFree(data);
    return desktop + 1;
  }
  return 1;
}

std::vector<WorkspaceInfo> X11Backend::getWorkspaces() {
  std::vector<WorkspaceInfo> workspaces;
  for (int i = 1; i <= 4; i++) {
    WorkspaceInfo ws;
    ws.id = i; ws.name = "Workspace " + std::to_string(i);
    ws.visible = (i == 1);
    workspaces.push_back(ws);
  }
  return workspaces;
}

bool X11Backend::switchToWorkspace(int workspace) { ManageVirtualDesktops(workspace); return true; }
bool X11Backend::moveWindowToWorkspace(wID, int workspace) { ManageVirtualDesktops(workspace); return true; }

void X11Backend::ManageVirtualDesktops(int action) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) return;
  ::Window root = DisplayManager::GetRootWindow();
  Atom desktopAtom = XInternAtom(display, "_NET_CURRENT_DESKTOP", x11::XFalse);
  Atom desktopCountAtom = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", x11::XFalse);

  unsigned long nitems, bytes;
  unsigned char *data = NULL;
  int format; Atom type;

  XGetWindowProperty(display, root, desktopAtom, 0, 1, x11::XFalse, XA_CARDINAL,
                     &type, &format, &nitems, &bytes, &data);
  int current = data ? *(int *)data : 0;
  if (data) XFree(data);

  XGetWindowProperty(display, root, desktopCountAtom, 0, 1, x11::XFalse, XA_CARDINAL,
                     &type, &format, &nitems, &bytes, &data);
  int total = data ? *(int *)data : 1;
  if (data) XFree(data);

  int next = current;
  switch (action) {
    case 1: next = (current + 1) % total; break;
    case 2: next = (current - 1 + total) % total; break;
  }

  XEvent event;
  event.xclient.type = x11::XClientMessage;
  event.xclient.message_type = desktopAtom;
  event.xclient.format = 32;
  event.xclient.data.l[0] = next;
  event.xclient.data.l[1] = CurrentTime;
  XSendEvent(display, root, x11::XFalse, SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XFlush(display);
}

bool X11Backend::moveWindowToMonitor(wID id, int monitor) {
  Display *display = DisplayManager::GetDisplay();
  if (!display || id == 0) return false;

  XWindowAttributes winAttr;
  if (!XGetWindowAttributes(display, id, &winAttr)) return false;
  int winX, winY; ::Window child;
  XTranslateCoordinates(display, id, DisplayManager::GetRootWindow(), 0, 0, &winX, &winY, &child);

  auto monitors = DisplayManager::GetMonitors();
  if (monitors.size() < 2) return false;
  if (monitor < 0 || static_cast<size_t>(monitor) >= monitors.size()) return false;

  auto &target = monitors[monitor];
  return moveWindow(id, target.x + (winX - monitors[0].x), target.y + (winY - monitors[0].y));
}

void X11Backend::startAltTab() {}
void X11Backend::continueAltTab() {}
void X11Backend::finishAltTab() {}

std::vector<WindowInfo> X11Backend::getAllWindows() {
  std::vector<WindowInfo> windows;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return windows;

  Window root = DisplayManager::GetRootWindow();
  Window rootReturn, parentReturn;
  Window *childrenReturn;
  unsigned int nChildren;

  if (XQueryTree(display, root, &rootReturn, &parentReturn, &childrenReturn, &nChildren)) {
    for (unsigned int i = 0; i < nChildren; i++) {
      WindowInfo info;
      info.id = childrenReturn[i];
      char *windowName = nullptr;
      if (XFetchName(display, childrenReturn[i], &windowName)) {
        info.title = windowName ? windowName : "";
        XFree(windowName);
      }
      XClassHint classHint;
      if (XGetClassHint(display, childrenReturn[i], &classHint)) {
        info.windowClass = classHint.res_class ? classHint.res_class : "";
        if (classHint.res_name) XFree(classHint.res_name);
        if (classHint.res_class) XFree(classHint.res_class);
      }
      XWindowAttributes attrs;
      if (XGetWindowAttributes(display, childrenReturn[i], &attrs)) {
        info.x = attrs.x; info.y = attrs.y;
        info.width = attrs.width; info.height = attrs.height;
      }
      info.valid = true;
      windows.push_back(info);
    }
    XFree(childrenReturn);
  }
  return windows;
}

WindowInfo X11Backend::getWindowInfo(wID id) {
  WindowInfo info;
  if (id == 0) return info;
  info.id = id; info.pid = getWindowPID(id);
  info.exe = getProcessName(info.pid);
  info.cmdline = getProcessCmdline(info.pid);
  info.title = getWindowTitle(id);
  info.windowClass = getWindowClass(id);
  info.valid = true;
  Display *display = DisplayManager::GetDisplay();
  if (display) {
    XWindowAttributes attrs;
    if (XGetWindowAttributes(display, id, &attrs)) {
      info.x = attrs.x; info.y = attrs.y;
      info.width = attrs.width; info.height = attrs.height;
    }
  }
  return info;
}

WindowInfo X11Backend::getActiveWindowInfo() {
  return getWindowInfo(getActiveWindow());
}

std::string X11Backend::getProcessName(pid_t pid) {
  return ReadProcFile("/proc/" + std::to_string(pid) + "/comm");
}

std::string X11Backend::getProcessCmdline(pid_t pid) {
  std::string result = ReadProcFile("/proc/" + std::to_string(pid) + "/cmdline");
  if (!result.empty()) { for (char &c : result) { if (c == '\0') c = ' '; } }
  return result;
}

} // namespace havel
