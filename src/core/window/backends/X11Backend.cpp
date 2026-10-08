#include "core/window/backends/X11Backend.hpp"
#include "utils/Logger.hpp"
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return std::nullopt;
  ::Window root = DisplayManager::GetRootWindow();
  if (!root) return std::nullopt;
  wID activeWindowId = getActiveWindow();
  if (!activeWindowId) return std::nullopt;
  return ActiveWindowContext{display, root, activeWindowId};
}

wID X11Backend::getActiveWindow() {
  if (cacheValid() && activeCache_.id != 0) {
    return activeCache_.id;
  }

  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  // EWMH _NET_ACTIVE_WINDOW is the authoritative client id. On reparenting WMs
  // (Cinnamon, Muffin, KWin with frames) the client is nested under a frame
  // whose parent is the root; climbing to the frame would throw away the real
  // client id the user expects. Only climb for values obtained from fallbacks
  // (input focus / client list / enumerate), which may legitimately be an
  // inner window that needs a stable top-level ancestor.
  bool fromEwmh = false;

  if (XGetWindowProperty(display, DefaultRootWindow(display), activeWindowAtom,
                          0, 1, x11::XFalse, XA_WINDOW, &actualType,
                          &actualFormat, &nitems, &bytesAfter,
                          &prop) == x11::XSuccess) {
    if (prop) {
      activeWindow = *reinterpret_cast<Window *>(prop);
      fromEwmh = true;
      XFree(prop);
    }
  }

  if (activeWindow == 0 || activeWindow == DefaultRootWindow(display)) {
    Window focusedWindow = 0;
    int revertTo = 0;
    if (XGetInputFocus(display, &focusedWindow, &revertTo) != 0) {
      if (focusedWindow != 0 && focusedWindow != 1 && focusedWindow != DefaultRootWindow(display)) {
        activeWindow = focusedWindow;
      }
    }
  }

  if (activeWindow == 0 || activeWindow == DefaultRootWindow(display)) {
    Atom stackingAtom = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", x11::XTrue);
    if (stackingAtom == x11::XNone) {
      stackingAtom = XInternAtom(display, "_NET_CLIENT_LIST", x11::XTrue);
    }
    if (stackingAtom != x11::XNone) {
      Atom actualType;
      int actualFormat;
      unsigned long nitems, bytesAfter;
      unsigned char *prop = nullptr;
      if (XGetWindowProperty(display, DefaultRootWindow(display), stackingAtom,
                              0, 1024, x11::XFalse, XA_WINDOW, &actualType,
                              &actualFormat, &nitems, &bytesAfter,
                              &prop) == x11::XSuccess && prop) {
        if (nitems > 0) {
          Window *wins = reinterpret_cast<Window *>(prop);
          activeWindow = wins[nitems - 1];
        }
        XFree(prop);
      }
    }
  }

  if (activeWindow == 0 || activeWindow == DefaultRootWindow(display)) {
    auto allWins = getAllWindows();
    if (!allWins.empty()) {
      activeWindow = allWins.back().id;
    }
  }

  if (!fromEwmh && activeWindow != 0 && activeWindow != DefaultRootWindow(display)) {
    Window current = activeWindow;
    Window root = DefaultRootWindow(display);
    while (current != 0 && current != root) {
      Window rootReturn, parentReturn;
      Window *childrenReturn = nullptr;
      unsigned int nChildren = 0;
      if (XQueryTree(display, current, &rootReturn, &parentReturn, &childrenReturn, &nChildren) == 0) {
        break;
      }
      if (childrenReturn) XFree(childrenReturn);
      if (parentReturn == 0 || parentReturn == root) {
        activeWindow = current;
        break;
      }
      current = parentReturn;
    }
  }

  if (activeWindow != activeCache_.id) {
    activeCache_.id = activeWindow;
    activeCache_.title.clear();
    activeCache_.className.clear();
  }
  activeCache_.lastUpdate = std::chrono::steady_clock::now();
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
  if (cacheValid() && !activeCache_.title.empty()) {
    return activeCache_.title;
  }
  wID active_win = getActiveWindow();
  if (active_win == 0) return "";
  activeCache_.title = getWindowTitle(active_win);
  return activeCache_.title;
}

std::string X11Backend::getActiveWindowClass() {
  if (cacheValid() && !activeCache_.className.empty()) {
    return activeCache_.className;
  }
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return "";

  ::Window focusedWindow;
  int revertTo;
  if (XGetInputFocus(display, &focusedWindow, &revertTo) == 0) return "";
  if (focusedWindow == x11::XNone) return "";

  XClassHint classHint;
  if (XGetClassHint(display, focusedWindow, &classHint) == 0) return "";

  std::string className = classHint.res_class ? classHint.res_class : "";
  if (classHint.res_name) XFree(classHint.res_name);
  if (classHint.res_class) XFree(classHint.res_class);

  if (!className.empty()) {
    activeCache_.className = className;
  }
  activeCache_.lastUpdate = std::chrono::steady_clock::now();
  return className;
}

std::string X11Backend::getWindowTitle(wID id) {
  if (id == 0) return "";

  {
    auto it = windowInfoCache_.find(id);
    if (it != windowInfoCache_.end() &&
        std::chrono::steady_clock::now() - it->second.lastUpdate < WINDOW_CACHE_TTL) {
      return it->second.title;
    }
  }

  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return "";

  // Try _NET_WM_NAME first (UTF-8, modern standard)
  Atom netWmNameAtom = XInternAtom(display, "_NET_WM_NAME", x11::XFalse);
  Atom utf8StringAtom = XInternAtom(display, "UTF8_STRING", x11::XFalse);
  std::string title;
  if (netWmNameAtom != x11::XNone) {
    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;
    if (XGetWindowProperty(display, id, netWmNameAtom, 0, 1024, x11::XFalse,
                            utf8StringAtom, &actualType, &actualFormat, &nitems,
                            &bytesAfter, &prop) == x11::XSuccess) {
      if (prop && nitems > 0) {
        title = std::string(reinterpret_cast<char *>(prop));
        XFree(prop);
      } else {
        if (prop) XFree(prop);
      }
    }
  }

  if (title.empty()) {
    char *windowName = nullptr;
    if (XFetchName(display, id, &windowName) && windowName) {
      title = std::string(windowName);
      XFree(windowName);
    }
  }

  // Do not clobber an already-resolved className: getWindowClass reads
  // this cache and would otherwise see an empty class for WINDOW_CACHE_TTL
  // (getWindowInfo calls getWindowTitle then getWindowClass back-to-back).
  auto &entry = windowInfoCache_[id];
  entry.title = title;
  entry.lastUpdate = std::chrono::steady_clock::now();
  return title;
}

pID X11Backend::getWindowPID(wID id) {
  if (id == 0) return 0;
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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

  {
    auto it = windowInfoCache_.find(id);
    if (it != windowInfoCache_.end() && !it->second.className.empty() &&
        std::chrono::steady_clock::now() - it->second.lastUpdate < WINDOW_CACHE_TTL) {
      return it->second.className;
    }
  }

  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return "";

  // First try XGetClassHint (standard ICCCM way)
  XClassHint classHint;
  std::string windowClass;
  if (XGetClassHint(display, id, &classHint) != 0) {
    if (classHint.res_class) {
      windowClass = classHint.res_class;
      XFree(classHint.res_class);
    }
    if (classHint.res_name) {
      XFree(classHint.res_name);
    }
  }

  // Fallback: read raw WM_CLASS property if XGetClassHint failed or returned empty
  if (windowClass.empty()) {
    Atom wmClassAtom = XInternAtom(display, "WM_CLASS", x11::XTrue);
    if (wmClassAtom != x11::XNone) {
      Atom actualType;
      int actualFormat;
      unsigned long nItems, bytesAfter;
      unsigned char *prop = nullptr;
      if (XGetWindowProperty(display, id, wmClassAtom, 0, 1024, x11::XFalse,
                              XA_STRING, &actualType, &actualFormat, &nItems,
                              &bytesAfter, &prop) == x11::XSuccess) {
        if (prop && nItems > 0) {
          // WM_CLASS contains two NULL-terminated strings: res_name, res_class
          const char *data = reinterpret_cast<const char *>(prop);
          const char *res_name = data;
          const char *res_class = data + strlen(data) + 1;
          if (res_class < data + nItems && *res_class) {
            windowClass = res_class;
          } else if (*res_name) {
            windowClass = res_name;
          }
        }
        if (prop) XFree(prop);
      }
    }
  }

  auto &entry = windowInfoCache_[id];
  entry.className = windowClass;
  entry.lastUpdate = std::chrono::steady_clock::now();
  return windowClass;
}

Rect X11Backend::getWindowPosition(wID id) {
  if (!id) return {};
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return false;
  XWindowAttributes attr;
  return XGetWindowAttributes(display, id, &attr) != 0;
}

bool X11Backend::isWindowFullscreen(wID id) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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

wID X11Backend::getWindowParent(wID id) {
  if (id == 0) return 0;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return 0;

  ::Window rootReturn, parentReturn = 0;
  ::Window *childrenReturn = nullptr;
  unsigned int nChildren = 0;
  if (XQueryTree(display, id, &rootReturn, &parentReturn, &childrenReturn,
                 &nChildren)) {
    if (childrenReturn) XFree(childrenReturn);
    return static_cast<wID>(parentReturn);
  }
  return 0;
}

std::vector<wID> X11Backend::getWindowChildren(wID id) {
  std::vector<wID> result;
  if (id == 0) return result;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return result;

  ::Window rootReturn, parentReturn;
  ::Window *childrenReturn = nullptr;
  unsigned int nChildren = 0;
  if (XQueryTree(display, id, &rootReturn, &parentReturn, &childrenReturn,
                 &nChildren)) {
    result.reserve(nChildren);
    for (unsigned int i = 0; i < nChildren; ++i) {
      result.push_back(static_cast<wID>(childrenReturn[i]));
    }
  }
  if (childrenReturn) XFree(childrenReturn);
  return result;
}

std::vector<std::pair<std::string, std::string>>
X11Backend::getWindowProperties(wID id) {
  std::vector<std::pair<std::string, std::string>> result;
  if (id == 0) return result;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return result;

  int nProps = 0;
  Atom *propAtoms = XListProperties(display, id, &nProps);
  if (!propAtoms) return result;
  result.reserve(static_cast<size_t>(nProps));

  const Atom stringAtom = XA_STRING;
  const Atom utf8Atom = XInternAtom(display, "UTF8_STRING", x11::XFalse);
  const Atom compoundAtom = XInternAtom(display, "COMPOUND_TEXT", x11::XFalse);

  for (int p = 0; p < nProps; ++p) {
    Atom actualType = x11::XNone;
    int actualFormat = 0;
    unsigned long nitems = 0, bytesAfter = 0;
    unsigned char *prop = nullptr;
    if (XGetWindowProperty(display, id, propAtoms[p], 0, 4096, x11::XFalse,
                           AnyPropertyType, &actualType, &actualFormat,
                           &nitems, &bytesAfter,
                           &prop) != x11::XSuccess ||
        !prop) {
      continue;
    }

    char *name = XGetAtomName(display, propAtoms[p]);
    if (!name) {
      XFree(prop);
      continue;
    }

    std::string value;
    if (actualType == stringAtom || actualType == utf8Atom ||
        actualType == compoundAtom) {
      // Text property: bytes are the payload.
      value.assign(reinterpret_cast<char *>(prop),
                   reinterpret_cast<char *>(prop) + nitems);
    } else if (actualType == XA_CARDINAL || actualType == XA_INTEGER) {
      // Numeric property: render every item (atoms such as _NET_WM_STATE
      // hold arrays of cardinals).
      if (actualFormat == 32) {
        const unsigned long *items =
            reinterpret_cast<const unsigned long *>(prop);
        for (unsigned long i = 0; i < nitems; ++i) {
          if (!value.empty()) value += " ";
          value += std::to_string(items[i]);
        }
      } else if (actualFormat == 16) {
        const unsigned short *items =
            reinterpret_cast<const unsigned short *>(prop);
        for (unsigned long i = 0; i < nitems; ++i) {
          if (!value.empty()) value += " ";
          value += std::to_string(items[i]);
        }
      } else if (actualFormat == 8) {
        const unsigned char *items = prop;
        for (unsigned long i = 0; i < nitems; ++i) {
          if (!value.empty()) value += " ";
          value += std::to_string(static_cast<unsigned>(items[i]));
        }
      }
    } else if (actualType == XA_ATOM) {
      // Atom array: render each referenced atom's name.
      const Atom *items = reinterpret_cast<const Atom *>(prop);
      for (unsigned long i = 0; i < nitems; ++i) {
        char *refName = XGetAtomName(display, items[i]);
        if (refName) {
          if (!value.empty()) value += " ";
          value += refName;
          XFree(refName);
        }
      }
    } else {
      // Binary/unknown type: report the type name so the key still shows up.
      char *typeName = XGetAtomName(display, actualType);
      value = typeName ? std::string("<") + typeName + std::string(">")
                       : std::string("<binary>");
      if (typeName) XFree(typeName);
    }

    result.emplace_back(name, value);
    XFree(name);
    XFree(prop);
  }
  XFree(propAtoms);
  return result;
}

std::vector<uint8_t> X11Backend::getWindowIcon(wID id, int &width,
                                                int &height) {
  width = height = 0;
  std::vector<uint8_t> rgba;
  if (id == 0) return rgba;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return rgba;

  Atom iconAtom = XInternAtom(display, "_NET_WM_ICON", x11::XTrue);
  if (iconAtom == x11::XNone) return rgba;

  Atom actualType = x11::XNone;
  int actualFormat = 0;
  unsigned long nitems = 0, bytesAfter = 0;
  unsigned char *prop = nullptr;
  if (XGetWindowProperty(display, id, iconAtom, 0, 64 * 1024, x11::XFalse,
                         XA_CARDINAL, &actualType, &actualFormat, &nitems,
                         &bytesAfter, &prop) != x11::XSuccess ||
      !prop || actualFormat != 32) {
    if (prop) XFree(prop);
    return rgba;
  }

  // _NET_WM_ICON packs one or more icons as [width, height, ARGB*w*h]
  // cardinals back to back. Keep the largest (the first is not guaranteed
  // to be the biggest).
  const unsigned long *cardinals =
      reinterpret_cast<const unsigned long *>(prop);
  size_t offset = 0;
  const unsigned long *best = nullptr;
  unsigned long bestW = 0, bestH = 0;
  while (offset + 2 <= nitems) {
    unsigned long w = cardinals[offset];
    unsigned long h = cardinals[offset + 1];
    if (w == 0 || h == 0 || offset + 2 + w * h > nitems) {
      break;
    }
    if (w * h > bestW * bestH) {
      best = cardinals + offset;
      bestW = w;
      bestH = h;
    }
    offset += 2 + w * h;
  }

  if (best && bestW > 0 && bestH > 0 &&
      bestW <= 512 && bestH <= 512) {
    rgba.reserve(static_cast<size_t>(bestW * bestH) * 4);
    for (unsigned long i = 0; i < bestW * bestH; ++i) {
      // Pixels are 0xAARRGGBB cardinals; ZPixmap byte order is little-endian
      // on every platform Havel targets, so bytes arrive as B,G,R,A.
      unsigned long px = best[2 + i];
      rgba.push_back(static_cast<uint8_t>((px >> 16) & 0xFF)); // R
      rgba.push_back(static_cast<uint8_t>((px >> 8) & 0xFF));  // G
      rgba.push_back(static_cast<uint8_t>(px & 0xFF));         // B
      rgba.push_back(static_cast<uint8_t>((px >> 24) & 0xFF)); // A
    }
    width = static_cast<int>(bestW);
    height = static_cast<int>(bestH);
  }

  XFree(prop);
  return rgba;
}

std::vector<uint8_t> X11Backend::captureWindow(wID id, int &width,
                                               int &height) {
  width = height = 0;
  std::vector<uint8_t> rgba;
  if (id == 0) return rgba;
  Display *display = DisplayManager::GetDisplay();
  if (!display) return rgba;

  ::Window rootReturn;
  ::Window windowReturn = static_cast<::Window>(id);
  int xReturn, yReturn;
  unsigned int wReturn, hReturn, borderReturn, depthReturn;
  if (!XGetGeometry(display, windowReturn, &rootReturn, &xReturn, &yReturn,
                    &wReturn, &hReturn, &borderReturn, &depthReturn) ||
      wReturn == 0 || hReturn == 0) {
    return rgba;
  }
  // Guard absurd sizes (guard against capture budgets measured in GBs).
  if (wReturn > 16384 || hReturn > 16384) return rgba;

  XImage *image = XGetImage(display, windowReturn, 0, 0,
                            static_cast<unsigned int>(wReturn),
                            static_cast<unsigned int>(hReturn), AllPlanes,
                            ZPixmap);
  if (!image || !image->data) {
    if (image) XDestroyImage(image);
    return rgba;
  }

  rgba.reserve(static_cast<size_t>(wReturn) * hReturn * 4);
  const int bpp = image->bits_per_pixel;
  const bool hasAlpha = depthReturn == 32;
  for (unsigned int y = 0; y < hReturn; ++y) {
    const uint8_t *row =
        reinterpret_cast<const uint8_t *>(image->data) +
        static_cast<size_t>(y) * image->bytes_per_line;
    for (unsigned int x = 0; x < wReturn; ++x) {
      // ZPixmap little-endian: 24bpp = B,G,R,x; 32bpp = B,G,R,A.
      const uint8_t b = row[static_cast<size_t>(x) * (bpp / 8) + 0];
      const uint8_t g = row[static_cast<size_t>(x) * (bpp / 8) + 1];
      const uint8_t r = row[static_cast<size_t>(x) * (bpp / 8) + 2];
      const uint8_t a =
          hasAlpha ? row[static_cast<size_t>(x) * (bpp / 8) + 3]
                   : static_cast<uint8_t>(255);
      rgba.push_back(r);
      rgba.push_back(g);
      rgba.push_back(b);
      rgba.push_back(a);
    }
  }
  XDestroyImage(image);
  width = static_cast<int>(wReturn);
  height = static_cast<int>(hReturn);
  return rgba;
}

wID X11Backend::newWindow(const std::string &name, std::vector<int> *dimensions, bool hide) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
    // gravity 10 (StaticGravity) | flags x|y|w|h (0x0F << 8)
    ev.xclient.data.l[0] = 10 | ((1 | 2 | 4 | 8) << 8);
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display || id == 0) return false;
  auto monitor = DisplayManager::GetPrimaryMonitor();
  XWindowAttributes attrs;
  if (!XGetWindowAttributes(display, id, &attrs)) return false;
  return moveWindow(id, monitor.x + (monitor.width - attrs.width) / 2,
                    monitor.y + (monitor.height - attrs.height) / 2);
}

bool X11Backend::snapWindow(wID id, int position, int padding) {
  Display *display = DisplayManager::GetDisplay();
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
  if (!display) return windows;

  Window root = DisplayManager::GetRootWindow();

  // Enumerate EWMH _NET_CLIENT_LIST: the authoritative set of managed client
  // windows. Unlike raw root children, this includes reparented clients (e.g.
  // under a Cinnamon/Muffin frame) and excludes frames/decoration windows, so
  // the returned ids are the real clients the user acts on and stay consistent
  // with getActiveWindow().
  std::vector<Window> clientList;
  Atom clientsAtom = XInternAtom(display, "_NET_CLIENT_LIST", x11::XTrue);
  if (clientsAtom != x11::XNone) {
    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;
    if (XGetWindowProperty(display, root, clientsAtom, 0, 4096, x11::XFalse,
                           XA_WINDOW, &actualType, &actualFormat, &nitems,
                           &bytesAfter, &prop) == x11::XSuccess && prop) {
      Window *wins = reinterpret_cast<Window *>(prop);
      for (unsigned long i = 0; i < nitems; i++) clientList.push_back(wins[i]);
      XFree(prop);
    }
  }

  if (clientList.empty()) {
    Window rootReturn, parentReturn;
    Window *childrenReturn = nullptr;
    unsigned int nChildren = 0;
    if (XQueryTree(display, root, &rootReturn, &parentReturn, &childrenReturn,
                   &nChildren)) {
      for (unsigned int i = 0; i < nChildren; i++)
        clientList.push_back(childrenReturn[i]);
      if (childrenReturn) XFree(childrenReturn);
    }
  }

  for (Window w : clientList) {
    WindowInfo info;
    info.id = w;
    info.title = getWindowTitle(w);
    XClassHint classHint;
    if (XGetClassHint(display, w, &classHint)) {
      info.windowClass = classHint.res_class ? classHint.res_class : "";
      if (classHint.res_name) XFree(classHint.res_name);
      if (classHint.res_class) XFree(classHint.res_class);
    }
    XWindowAttributes attrs;
    if (XGetWindowAttributes(display, w, &attrs)) {
      info.x = attrs.x; info.y = attrs.y;
      info.width = attrs.width; info.height = attrs.height;
    }
    info.pid = getWindowPID(w);
    info.exe = info.pid ? getProcessName(info.pid) : "";
    info.cmdline = info.pid ? getProcessCmdline(info.pid) : "";
    info.valid = true;
    windows.push_back(info);
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
  if (!display) { havel::error("[X11Backend] GetDisplay returned null"); }
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

// ============================================================================
// EWMH / _NET_WM_STATE helpers (static internal)
// ============================================================================

static bool X11InternAtomCache(Display *d, const char *name, Atom &out) {
  if (out) return true;
  out = XInternAtom(d, name, x11::XFalse);
  return out != x11::XNone;
}

static Atom X11NetWmStateAtom() { static Atom a = 0; return a; }

static bool X11SendNetWmState(Display *d, wID win, Atom stateAtom, Atom atom1,
                              Atom atom2, int action) {
  if (!d || !win) return false;
  Atom sa = XInternAtom(d, "_NET_WM_STATE", x11::XFalse);
  if (sa == x11::XNone) return false;
  XClientMessageEvent ev = {};
  ev.type = x11::XClientMessage;
  ev.window = static_cast<Window>(win);
  ev.message_type = sa;
  ev.format = 32;
  ev.data.l[0] = action;
  ev.data.l[1] = static_cast<long>(atom1);
  ev.data.l[2] = static_cast<long>(atom2);
  ev.data.l[3] = 1; // source: application
  ev.data.l[4] = 0;
  bool ok = XSendEvent(d, DefaultRootWindow(d), x11::XFalse,
                       SubstructureRedirectMask | SubstructureNotifyMask,
                       reinterpret_cast<XEvent *>(&ev)) != 0;
  if (ok) XFlush(d);
  return ok;
}

static bool X11HasState(Display *d, wID win, Atom target) {
  if (!d || !win) return false;
  Atom stateAtom = XInternAtom(d, "_NET_WM_STATE", x11::XTrue);
  if (stateAtom == x11::XNone) return false;
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(win), stateAtom, 0, 256,
                         x11::XFalse, XA_ATOM, &actual, &fmt, &n, &ba, &prop)
      != x11::XSuccess || !prop) return false;
  bool found = false;
  if (n) {
    Atom *atoms = reinterpret_cast<Atom *>(prop);
    for (unsigned long i = 0; i < n; ++i) {
      if (atoms[i] == target) { found = true; break; }
    }
  }
  if (prop) XFree(prop);
  return found;
}

// ============================================================================
// EWMH _NET_WM_STATE toggles on actual windows
// ============================================================================

bool X11Backend::setWindowSticky(wID id, bool sticky) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_STICKY", x11::XFalse);
  return X11SendNetWmState(d, id, atom, 0, 0, sticky ? 1 : 0);
}
bool X11Backend::isWindowSticky(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_STICKY", x11::XTrue);
  if (atom == x11::XNone) return false;
  return X11HasState(d, id, atom);
}
bool X11Backend::setWindowShaded(wID id, bool shaded) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SHADED", x11::XFalse);
  return X11SendNetWmState(d, id, atom, 0, 0, shaded ? 1 : 0);
}
bool X11Backend::isWindowShaded(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SHADED", x11::XTrue);
  if (atom == x11::XNone) return false;
  return X11HasState(d, id, atom);
}
bool X11Backend::setWindowSkipTaskbar(wID id, bool skip) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", x11::XFalse);
  return X11SendNetWmState(d, id, atom, 0, 0, skip ? 1 : 0);
}
bool X11Backend::isWindowSkipTaskbar(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", x11::XTrue);
  if (atom == x11::XNone) return false;
  return X11HasState(d, id, atom);
}
bool X11Backend::setWindowSkipPager(wID id, bool skip) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", x11::XFalse);
  return X11SendNetWmState(d, id, atom, 0, 0, skip ? 1 : 0);
}
bool X11Backend::isWindowSkipPager(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom atom = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", x11::XTrue);
  if (atom == x11::XNone) return false;
  return X11HasState(d, id, atom);
}

// ============================================================================
// Decorations (Motif hints) — borderless
// ============================================================================

struct MotifHints { unsigned long flags, functions, decorations, input_mode, status; };

bool X11Backend::setWindowDecorated(wID id, bool decorated) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XFalse);
  if (a == x11::XNone) return false;
  MotifHints hints = {2UL, 0UL, decorated ? 1UL : 0UL, 0UL, 0UL};
  int ok = XChangeProperty(d, static_cast<Window>(id), a, a, 32,
                           PropModeReplace, reinterpret_cast<unsigned char *>(&hints), 5);
  if (ok) XFlush(d);
  return ok != 0;
}

bool X11Backend::isWindowDecorated(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return true; // default: has decorations
  Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XTrue);
  if (a == x11::XNone) return true;
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), a, 0, 5, x11::XFalse, a,
                         &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return true;
  if (n < 3) { if (prop) XFree(prop); return true; }
  long deco = reinterpret_cast<long *>(prop)[2];
  XFree(prop);
  return deco != 0;
}

// ============================================================================
// Window property readers
// ============================================================================

bool X11Backend::getWindowOpacity(wID id, double &outOpacity) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_NET_WM_WINDOW_OPACITY", x11::XTrue);
  if (a == x11::XNone) return false;
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), a, 0, 1, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return false;
  if (n >= 1) {
    outOpacity = static_cast<double>(*reinterpret_cast<unsigned long *>(prop)) / 4294967295.0;
    XFree(prop);
    return true;
  }
  if (prop) XFree(prop);
  return false;
}

bool X11Backend::getWindowFrameExtents(wID id, int &l, int &r, int &t, int &b) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_NET_FRAME_EXTENTS", x11::XTrue);
  if (a == x11::XNone) return false;
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), a, 0, 4, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return false;
  if (n >= 4) {
    long *v = reinterpret_cast<long *>(prop);
    l = v[0]; r = v[1]; t = v[2]; b = v[3];
    XFree(prop);
    return true;
  }
  if (prop) XFree(prop);
  return false;
}

std::string X11Backend::getWindowType(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return "normal";
  Atom a = XInternAtom(d, "_NET_WM_WINDOW_TYPE", x11::XTrue);
  if (a == x11::XNone) return "normal";
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), a, 0, 1, x11::XFalse,
                         XA_ATOM, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop || n == 0)
    return "normal";
  Atom typeAtom = reinterpret_cast<Atom *>(prop)[0];
  char *name = XGetAtomName(d, typeAtom);
  std::string out = name ? name : "normal";
  if (name) XFree(name);
  XFree(prop);
  // Strip "_NET_WM_WINDOW_TYPE_" prefix and lowercase for the spec
  const std::string prefix = "_NET_WM_WINDOW_TYPE_";
  if (out.rfind(prefix, 0) == 0) out = out.substr(prefix.size());
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

// ============================================================================
// Workspace / desktop
// ============================================================================

bool X11Backend::setWindowOnAllDesktops(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_NET_WM_DESKTOP", x11::XFalse);
  if (a == x11::XNone) return false;
  XClientMessageEvent ev = {};
  ev.type = x11::XClientMessage;
  ev.window = static_cast<Window>(id);
  ev.message_type = a;
  ev.format = 32;
  ev.data.l[0] = 0xFFFFFFFFL; // all desktops
  ev.data.l[1] = CurrentTime;
  int ok = XSendEvent(d, DefaultRootWindow(d), x11::XFalse,
                      SubstructureRedirectMask | SubstructureNotifyMask,
                      reinterpret_cast<XEvent *>(&ev));
  if (ok) XFlush(d);
  return ok != 0;
}

int X11Backend::getWindowDesktop(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return -1;
  Atom a = XInternAtom(d, "_NET_WM_DESKTOP", x11::XTrue);
  if (a == x11::XNone) return -1;
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), a, 0, 1, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return -1;
  int out = -1;
  if (n >= 1) out = static_cast<int>(*reinterpret_cast<unsigned long *>(prop));
  if (prop) XFree(prop);
  return out;
}

// ============================================================================
// Lifecycle helpers
// ============================================================================

bool X11Backend::terminateWindow(wID id) {
  auto pid = getWindowPID(id);
  if (pid <= 0) return false;
  return ::kill(pid, SIGTERM) == 0;
}
bool X11Backend::killWindowClient(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  XKillClient(d, static_cast<Window>(id));
  XFlush(d);
  return true;
}

// ============================================================================
// Stacking
// ============================================================================

bool X11Backend::raiseWindow(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d || !id) return false;
  XRaiseWindow(d, static_cast<Window>(id));
  XFlush(d);
  return true;
}
bool X11Backend::lowerWindow(wID id) {
  Display *d = DisplayManager::GetDisplay();
  if (!d || !id) return false;
  XLowerWindow(d, static_cast<Window>(id));
  XFlush(d);
  return true;
}

} // namespace havel
