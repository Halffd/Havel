// UIBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "../BridgeSelection.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {

static Value createWindowObject(
    VM *vm, const HostContext *ctx, uint64_t windowId,
    const std::string &title, const std::string &windowClass,
    const std::string &exe, int pid, const std::string &cmdline);
static void parseWindowSpec(const std::string &selector, std::string &type,
                            std::string &value, bool &substring);
static bool matchWindowSpec(const ::havel::host::WindowInfo &win,
                            const std::string &type,
                            const std::string &rawValue, bool substring);
static uint64_t resolveWindowId(const Value &arg,
                                ::havel::host::WindowService &winService,
                                VM *vm);
static bool motifGetBorderless(wID windowId, bool &borderless);
static bool motifSetBorderless(wID windowId, bool enable);
static Value _mkOrObject(VM *vm, const char *k, long v, const char *k1, long v1,
                         const char *k2, long v2, const char *k3, long v3);

void UIBridge::install(PipelineOptions &options) {
  options.host_functions["window.active"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetActive(args, ctx);
  };
  options.host_functions["window.cmd"] = [ctx = ctx_](const auto &args) {
    return handleWindowCmd(args, ctx);
  };
  options.host_functions["window.find"] = [ctx = ctx_](const auto &args) {
    return handleWindowFind(args, ctx);
  };
  // Compatibility additions mirroring modules/app/window.hv API
  options.host_functions["window.activeId"] = [ctx = ctx_](const auto &args) {
    return handleWindowActiveId(args, ctx);
  };
  options.host_functions["window.exists"] = [ctx = ctx_](const auto &args) {
    return handleWindowExists(args, ctx);
  };
  options.host_functions["window.isActive"] = [ctx = ctx_](const auto &args) {
    ::havel::host::WindowService ws(ctx->windowManager);
    if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
    uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) return Value::makeBool(false);
    return Value::makeBool(wid == ws.getActiveWindow());
  };
  options.host_functions["window.sticky"] = [ctx = ctx_](const auto &args) {
    return handleWindowSticky(args, ctx);
  };
  options.host_functions["window.isSticky"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSticky(args, ctx);
  };
  options.host_functions["window.shade"] = [ctx = ctx_](const auto &args) {
    return handleWindowShade(args, ctx);
  };
  options.host_functions["window.isShaded"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsShaded(args, ctx);
  };
  options.host_functions["window.skipTaskbar"] = [ctx = ctx_](const auto &args) {
    return handleWindowSkipTaskbar(args, ctx);
  };
  options.host_functions["window.isSkipTaskbar"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSkipTaskbar(args, ctx);
  };
  options.host_functions["window.skipPager"] = [ctx = ctx_](const auto &args) {
    return handleWindowSkipPager(args, ctx);
  };
  options.host_functions["window.isSkipPager"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSkipPager(args, ctx);
  };
  options.host_functions["window.alwaysOnTop"] = [ctx = ctx_](const auto &args) {
    return handleWindowAlwaysOnTop(args, ctx);
  };
  options.host_functions["window.isAlwaysOnTop"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsAlwaysOnTop(args, ctx);
  };
  options.host_functions["window.getOpacity"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetOpacity(args, ctx);
  };
  options.host_functions["window.terminate"] = [ctx = ctx_](const auto &args) {
    return handleWindowTerminate(args, ctx);
  };
  options.host_functions["window.stickyToDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowStickyToDesktop(args, ctx);
  };
  options.host_functions["window.getDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetDesktop(args, ctx);
  };
  options.host_functions["window.setOpacity"] = [ctx = ctx_](const auto &args) {
    return handleWindowSetOpacity(args, ctx);
  };
  options.host_functions["window.unmin"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmin(args, ctx);
  };
  options.host_functions["window.unmax"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmax(args, ctx);
  };
  options.host_functions["window.toggleMax"] = [ctx = ctx_](const auto &args) {
    return handleWindowToggleMax(args, ctx);
  };
   options.host_functions["window.borderless"] = [ctx = ctx_](const auto &args) {
     if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
     ::havel::host::WindowService ws(ctx->windowManager);
     uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
     if (wid == 0) return Value::makeBool(false);
     bool enable = true;
     if (args.size() >= 2) {
       if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) enable = v->asBool();
     }
     Display *d = DisplayManager::GetDisplay();
     if (!d) return Value::makeBool(false);
     Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XFalse);
     if (a == x11::XNone) return Value::makeBool(false);
     unsigned long hints[5] = {2UL, 0UL, enable ? 0UL : 1UL, 0UL, 0UL};
     int ok = XChangeProperty(d, static_cast<Window>(wid), a, a, 32, PropModeReplace,
                              reinterpret_cast<unsigned char *>(hints), 5);
     if (ok) XFlush(d);
     return Value::makeBool(ok != 0);
   };
   options.host_functions["window.isBorderless"] = [ctx = ctx_](const auto &args) {
     if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
     ::havel::host::WindowService ws(ctx->windowManager);
     uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
     if (wid == 0) return Value::makeBool(false);
     Display *d = DisplayManager::GetDisplay();
     if (!d) return Value::makeBool(false);
     Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XTrue);
     if (a == x11::XNone) return Value::makeBool(false);
     Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
     if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 5, x11::XFalse, a,
                            &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
       return Value::makeBool(false);
     long deco = (n >= 3) ? reinterpret_cast<long *>(prop)[2] : 1;
     if (prop) XFree(prop);
     return Value::makeBool(deco == 0);
   };
   options.host_functions["window.toggleBorderless"] = [ctx = ctx_](const auto &args) {
     if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
     ::havel::host::WindowService ws(ctx->windowManager);
     uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
     if (wid == 0) return Value::makeBool(false);
     Display *d = DisplayManager::GetDisplay();
     if (!d) return Value::makeBool(false);
     Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XFalse);
     if (a == x11::XNone) return Value::makeBool(false);
     Atom ar = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XTrue);
     bool curBorderless = false;
     if (ar != x11::XNone) {
       Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
       if (XGetWindowProperty(d, static_cast<Window>(wid), ar, 0, 5, x11::XFalse, ar,
                              &actual, &fmt, &n, &ba, &prop) == x11::XSuccess && prop && n>=3) {
         curBorderless = (reinterpret_cast<long *>(prop)[2] == 0);
       }
       if (prop) XFree(prop);
     }
     bool wantOn = !curBorderless;
     unsigned long hints[5] = {2UL, 0UL, wantOn ? 0UL : 1UL, 0UL, 0UL};
     int ok = XChangeProperty(d, static_cast<Window>(wid), a, a, 32, PropModeReplace,
                              reinterpret_cast<unsigned char *>(hints), 5);
     if (ok) XFlush(d);
     return Value::makeBool(ok != 0);
   };
  options.host_functions["window.moveToMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveToMonitor(args, ctx);
  };
  options.host_functions["window.moveMonitor"] = [ctx = ctx_](const auto &args) {
    // module-level alias — moveMonitor(obj, i, follow)
    return handleWindowMoveToMonitor(args, ctx);
  };
  options.host_functions["window.moveMonitorNext"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveToNextMonitor(args, ctx);
  };
  options.host_functions["window.moveMonitorPrev"] = [ctx = ctx_](const auto &args) {
    // spec has moveMonitorPrev; backend lacks a "prev" op — reuse next with wrapped index? just call next for now.
    if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) {
      auto a = ws.getActiveWindowInfo();
      if (!a.valid) return Value::makeBool(false);
      wid = a.id;
    }
    auto mons = DisplayManager::GetMonitors();
    if (mons.empty()) return Value::makeBool(false);
    auto info = ws.getWindowInfo(wid);
    if (!info.valid) return Value::makeBool(false);
    int cur = 0;
    for (int i = 0; i < static_cast<int>(mons.size()); ++i) {
      if (info.x >= mons[i].x && info.x < mons[i].x + mons[i].width &&
          info.y >= mons[i].y && info.y < mons[i].y + mons[i].height) { cur = i; break; }
    }
    int prev = (cur - 1 + static_cast<int>(mons.size())) % static_cast<int>(mons.size());
    return Value::makeBool(ws.moveWindowToMonitor(wid, prev));
  };
  options.host_functions["window.getCurrentMonitor"] = [ctx = ctx_](const auto &args) {
    if (!ctx->windowManager) return Value::makeInt(0);
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = 0;
    if (!args.empty())
      wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) {
      auto a = ws.getActiveWindowInfo();
      if (!a.valid) return Value::makeInt(0);
      wid = a.id;
    }
    auto info = ws.getWindowInfo(wid);
    if (!info.valid) return Value::makeInt(0);
    auto mons = DisplayManager::GetMonitors();
    int idx = 0;
    for (const auto &m : mons) {
      if (info.x >= m.x && info.x < m.x + m.width &&
          info.y >= m.y && info.y < m.y + m.height) return Value::makeInt(idx);
      ++idx;
    }
    return Value::makeInt(0);
  };
   options.host_functions["window.getMonitors"] = [ctx = ctx_](const auto &args) {
     if (!ctx->vm) return Value::makeNull();
     auto *vm = static_cast<VM *>(ctx->vm);
     compiler::VMApi api(*vm);
     auto mons = DisplayManager::GetMonitors();
     auto arr = api.makeArray();
     int idx = 0;
     for (const auto &m : mons) {
       auto obj = api.makeObject();
       api.setField(obj, "index", Value::makeInt(idx));
       api.setField(obj, "name", api.makeString(m.name));
       api.setField(obj, "x", Value::makeInt(m.x));
       api.setField(obj, "y", Value::makeInt(m.y));
       api.setField(obj, "width", Value::makeInt(m.width));
       api.setField(obj, "height", Value::makeInt(m.height));
       api.setField(obj, "primary", Value::makeBool(m.isPrimary));
       api.push(arr, obj);
       ++idx;
     }
     return arr;
   };
  options.host_functions["window.frameExtents"] = [ctx = ctx_](const auto &args) {
    if (!ctx->windowManager || args.empty()) return Value::makeNull();
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) return Value::makeNull();
    Display *d = DisplayManager::GetDisplay();
    if (!d) return Value::makeNull();
    Atom a = XInternAtom(d, "_NET_FRAME_EXTENTS", x11::XTrue);
    if (a == x11::XNone) return Value::makeNull();
    Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
    if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 4, x11::XFalse,
                           XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop) return Value::makeNull();
    long l=0,t=0,r=0,b=0;
    if (n>=4){ l=reinterpret_cast<long*>(prop)[0]; r=reinterpret_cast<long*>(prop)[1]; t=reinterpret_cast<long*>(prop)[2]; b=reinterpret_cast<long*>(prop)[3]; }
    if (prop) XFree(prop);
    auto *vm = static_cast<VM *>(ctx->vm);
    auto obj = vm->createHostObject();
    vm->setHostObjectField(obj, "left", Value::makeInt(l));
    vm->setHostObjectField(obj, "right", Value::makeInt(r));
    vm->setHostObjectField(obj, "top", Value::makeInt(t));
    vm->setHostObjectField(obj, "bottom", Value::makeInt(b));
    return Value::makeObjectId(obj.id);
  };
  options.host_functions["window.findByTitle"] =
      [ctx = ctx_](const auto &args) { return handleWindowFindByTitle(args, ctx); };
  options.host_functions["window.findByClass"] =
      [ctx = ctx_](const auto &args) { return handleWindowFindByClass(args, ctx); };
  options.host_functions["window.findByPid"] =
      [ctx = ctx_](const auto &args) { return handleWindowFindByPid(args, ctx); };
  options.host_functions["window.findAllBySpec"] =
      [ctx = ctx_](const auto &args) { return handleWindowFindAllBySpec(args, ctx); };
  options.host_functions["window.moveToDesktop"] =
      [ctx = ctx_](const auto &args) { return handleWindowMoveToDesktop(args, ctx); };
  options.host_functions["window.setOpacity"] =
      [ctx = ctx_](const auto &args) { return handleWindowSetOpacity(args, ctx); };
  options.host_functions["window.groupNames"] =
      [ctx = ctx_](const auto &args) { return handleWindowGroupNames(args, ctx); };
  options.host_functions["window.close"] = [ctx = ctx_](const auto &args) {
    return handleWindowClose(args, ctx);
  };
  options.host_functions["window.resize"] = [ctx = ctx_](const auto &args) {
    return handleWindowResize(args, ctx);
  };
  options.host_functions["window.move"] = [ctx = ctx_](const auto &args) {
    return handleWindowMove(args, ctx);
  };
  options.host_functions["window.moveRel"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveRel(args, ctx);
  };
  options.host_functions["window.moveToMonitor"] =
      [ctx = ctx_](const auto &args) {
        return handleWindowMoveToMonitor(args, ctx);
      };
  options.host_functions["window.moveToNextMonitor"] =
      [ctx = ctx_](const auto &args) {
        return handleWindowMoveToNextMonitor(args, ctx);
      };
  options.host_functions["window.focus"] = [ctx = ctx_](const auto &args) {
    return handleWindowFocus(args, ctx);
  };
  options.host_functions["window.min"] = [ctx = ctx_](const auto &args) {
    return handleWindowMinimize(args, ctx);
  };
  options.host_functions["window.max"] = [ctx = ctx_](const auto &args) {
    return handleWindowMaximize(args, ctx);
  };
  options.host_functions["window.hide"] = [ctx = ctx_](const auto &args) {
    return handleWindowHide(args, ctx);
  };
  options.host_functions["window.show"] = [ctx = ctx_](const auto &args) {
    return handleWindowShow(args, ctx);
  };
  // Window query functions
  options.host_functions["window.any"] = [ctx = ctx_](const auto &args) {
    return handleWindowAny(args, ctx);
  };
  options.host_functions["window.count"] = [ctx = ctx_](const auto &args) {
    return handleWindowCount(args, ctx);
  };
  options.host_functions["window.filter"] = [ctx = ctx_](const auto &args) {
    return handleWindowFilter(args, ctx);
  };
  // Active window namespace functions
  options.host_functions["active.get"] = [ctx = ctx_](const auto &args) {
    return handleActiveGet(args, ctx);
  };
  options.host_functions["active.title"] = [ctx = ctx_](const auto &args) {
    return handleActiveTitle(args, ctx);
  };
  options.host_functions["active.class"] = [ctx = ctx_](const auto &args) {
    return handleActiveClass(args, ctx);
  };
  options.host_functions["active.exe"] = [ctx = ctx_](const auto &args) {
    return handleActiveExe(args, ctx);
  };
  options.host_functions["active.pid"] = [ctx = ctx_](const auto &args) {
    return handleActivePid(args, ctx);
  };
  options.host_functions["active.close"] = [ctx = ctx_](const auto &args) {
    return handleActiveClose(args, ctx);
  };
  options.host_functions["active.min"] = [ctx = ctx_](const auto &args) {
    return handleActiveMin(args, ctx);
  };
  options.host_functions["active.max"] = [ctx = ctx_](const auto &args) {
    return handleActiveMax(args, ctx);
  };
  options.host_functions["active.hide"] = [ctx = ctx_](const auto &args) {
    return handleActiveHide(args, ctx);
  };
  options.host_functions["active.show"] = [ctx = ctx_](const auto &args) {
    return handleActiveShow(args, ctx);
  };
  options.host_functions["active.move"] = [ctx = ctx_](const auto &args) {
    return handleActiveMove(args, ctx);
  };
  options.host_functions["active.resize"] = [ctx = ctx_](const auto &args) {
    return handleActiveResize(args, ctx);
  };
  // Window object prototype methods (shared, not per-instance)
  options.host_functions["window._close"] = [ctx = ctx_](const auto &args) {
    return handleWindowCloseObj(args, ctx);
  };
  options.host_functions["window._hide"] = [ctx = ctx_](const auto &args) {
    return handleWindowHideObj(args, ctx);
  };
  options.host_functions["window._show"] = [ctx = ctx_](const auto &args) {
    return handleWindowShowObj(args, ctx);
  };
  options.host_functions["window._focus"] = [ctx = ctx_](const auto &args) {
    return handleWindowFocusObj(args, ctx);
  };
  options.host_functions["window._min"] = [ctx = ctx_](const auto &args) {
    return handleWindowMinObj(args, ctx);
  };
  options.host_functions["window._max"] = [ctx = ctx_](const auto &args) {
    return handleWindowMaxObj(args, ctx);
  };
  options.host_functions["window._resize"] = [ctx = ctx_](const auto &args) {
    return handleWindowResizeObj(args, ctx);
  };
  options.host_functions["window._move"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveObj(args, ctx);
  };
  // Additional window operations
  options.host_functions["window.restore"] = [ctx = ctx_](const auto &args) {
    return handleWindowRestore(args, ctx);
  };
  options.host_functions["window.snap"] = [ctx = ctx_](const auto &args) {
    return handleWindowSnap(args, ctx);
  };
  options.host_functions["window.center"] = [ctx = ctx_](const auto &args) {
    return handleWindowCenter(args, ctx);
  };
  options.host_functions["window.fullscreen"] = [ctx = ctx_](
                                                   const auto &args) {
    return handleWindowFullscreen(args, ctx);
  };
  options.host_functions["window.moveResize"] = [ctx = ctx_](
                                                    const auto &args) {
    return handleWindowMoveResize(args, ctx);
  };
  options.host_functions["window.setAlwaysOnTop"] = [ctx = ctx_](
                                                        const auto &args) {
    return handleWindowSetAlwaysOnTop(args, ctx);
  };
  options.host_functions["window.pos"] = [ctx = ctx_](const auto &args) {
    return handleWindowPos(args, ctx);
  };
  options.host_functions["window.list"] = [ctx = ctx_](const auto &args) {
    return handleWindowList(args, ctx);
  };
  options.host_functions["window.all"] = [ctx = ctx_](const auto &args) {
    return handleWindowList(args, ctx);
  };
  options.host_functions["window.pidWindow"] = [ctx = ctx_](const auto &args) {
    return handleWindowFindByPid(args, ctx);
  };
  options.host_functions["window.currentDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowCurrentDesktop(args, ctx);
  };
  options.host_functions["window.desktopCount"] = [ctx = ctx_](const auto &args) {
    return handleWindowDesktopCount(args, ctx);
  };
  options.host_functions["window.desktopName"] = [ctx = ctx_](const auto &args) {
    return handleWindowDesktopName(args, ctx);
  };
  options.host_functions["window.viewport"] = [ctx = ctx_](const auto &args) {
    return handleWindowViewport(args, ctx);
  };
  options.host_functions["window.switchDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowSwitchDesktop(args, ctx);
  };
  options.host_functions["window.title"] = [ctx = ctx_](const auto &args) {
    return handleWindowTitle(args, ctx);
  };
  options.host_functions["window.class"] = [ctx = ctx_](const auto &args) {
    return handleWindowClass(args, ctx);
  };
  options.host_functions["window.exe"] = [ctx = ctx_](const auto &args) {
    return handleWindowExe(args, ctx);
  };
  options.host_functions["window.pid"] = [ctx = ctx_](const auto &args) {
    return handleWindowPid(args, ctx);
  };
  options.host_functions["window.id"] = [ctx = ctx_](const auto &args) {
    return handleWindowId(args, ctx);
  };
  options.host_functions["window.area"] = [ctx = ctx_](const auto &args) {
    return handleWindowArea(args, ctx);
  };
  options.host_functions["window.each"] = [ctx = ctx_](const auto &args) {
    return handleWindowEach(args, ctx);
  };
options.host_functions["window.sort"] = [ctx = ctx_](const auto &args) {
    return handleWindowSort(args, ctx);
};
options.host_functions["window.map"] = [ctx = ctx_](const auto &args) {
    return handleWindowMap(args, ctx);
};
options.host_functions["window.unmap"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmap(args, ctx);
};
options.host_functions["window.pin"] = [ctx = ctx_](const auto &args) {
    return handleWindowPin(args, ctx);
};
options.host_functions["window.wait"] = [ctx = ctx_](const auto &args) {
    return handleWindowWait(args, ctx);
};
// New object-method variants
  options.host_functions["window._restore"] = [ctx = ctx_](const auto &args) {
    return handleWindowRestoreObj(args, ctx);
  };
  options.host_functions["window._snap"] = [ctx = ctx_](const auto &args) {
    return handleWindowSnapObj(args, ctx);
  };
  options.host_functions["window._center"] = [ctx = ctx_](const auto &args) {
    return handleWindowCenterObj(args, ctx);
  };
  options.host_functions["window._fullscreen"] = [ctx = ctx_](
                                                    const auto &args) {
    return handleWindowFullscreenObj(args, ctx);
  };
  options.host_functions["window._moveResize"] = [ctx = ctx_](
                                                    const auto &args) {
    return handleWindowMoveResizeObj(args, ctx);
  };
  options.host_functions["window._setAlwaysOnTop"] = [ctx = ctx_](
                                                        const auto &args) {
    return handleWindowSetAlwaysOnTopObj(args, ctx);
  };
  options.host_functions["window._pos"] = [ctx = ctx_](const auto &args) {
    return handleWindowPosObj(args, ctx);
  };
  options.host_functions["window._size"] = [ctx = ctx_](const auto &args) {
    return handleWindowSizeObj(args, ctx);
  };
  options.host_functions["window._setSize"] = [ctx = ctx_](const auto &args) {
    return handleWindowSetSizeObj(args, ctx);
  };
  options.host_functions["window._isMaximized"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsMaximized(args, ctx);
  };
  options.host_functions["window._isMinimized"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsMinimized(args, ctx);
  };
  options.host_functions["window._isFullscreen"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsFullscreen(args, ctx);
  };
  options.host_functions["window._unmax"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmax(args, ctx);
  };
  options.host_functions["window._toggleMax"] = [ctx = ctx_](const auto &args) {
    return handleWindowToggleMax(args, ctx);
  };
  options.host_functions["window._unmin"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmin(args, ctx);
  };
  options.host_functions["window._sticky"] = [ctx = ctx_](const auto &args) {
    return handleWindowSticky(args, ctx);
  };
  options.host_functions["window._isSticky"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSticky(args, ctx);
  };
  options.host_functions["window._shade"] = [ctx = ctx_](const auto &args) {
    return handleWindowShade(args, ctx);
  };
  options.host_functions["window._isShaded"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsShaded(args, ctx);
  };
  options.host_functions["window._skipTaskbar"] = [ctx = ctx_](const auto &args) {
    return handleWindowSkipTaskbar(args, ctx);
  };
  options.host_functions["window._isSkipTaskbar"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSkipTaskbar(args, ctx);
  };
  options.host_functions["window._skipPager"] = [ctx = ctx_](const auto &args) {
    return handleWindowSkipPager(args, ctx);
  };
  options.host_functions["window._isSkipPager"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsSkipPager(args, ctx);
  };
  options.host_functions["window._alwaysOnTopState"] = [ctx = ctx_](const auto &args) {
    return handleWindowAlwaysOnTop(args, ctx);
  };
  options.host_functions["window._isAlwaysOnTopState"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsAlwaysOnTop(args, ctx);
  };
  options.host_functions["window._raise"] = [ctx = ctx_](const auto &args) {
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = 0;
    if (!args.empty())
      wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) {
      auto a = ws.getActiveWindowInfo();
      if (a.valid) wid = a.id;
    }
    Display *d = DisplayManager::GetDisplay();
    if (!d || wid == 0) return Value::makeBool(false);
    XRaiseWindow(d, static_cast<Window>(wid));
    XFlush(d);
    // No receiver injection through makeFunctionRef dispatches; just return bool.
    // Chainability in Havel comes from class methods on window.hv Window class,
    // and the C++ side only needs to perform the action.
    return Value::makeBool(true);
  };
  options.host_functions["window._lower"] = [ctx = ctx_](const auto &args) {
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = 0;
    if (!args.empty())
      wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) {
      auto a = ws.getActiveWindowInfo();
      if (a.valid) wid = a.id;
    }
    Display *d = DisplayManager::GetDisplay();
    if (!d || wid == 0) return Value::makeBool(false);
    XLowerWindow(d, static_cast<Window>(wid));
    XFlush(d);
    return Value::makeBool(true);
  };
  options.host_functions["window._setPos"] = [ctx = ctx_](const auto &args) {
    // setPos(x, y, speed, winId, relative) — same signature as move
    return handleWindowMove(args, ctx);
  };
  options.host_functions["window._geometry"] = [ctx = ctx_](const auto &args) {
    if (!ctx->windowManager || !ctx->vm) return Value::makeNull();
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = 0;
    if (!args.empty())
      wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) {
      auto a = ws.getActiveWindowInfo();
      if (!a.valid) return Value::makeNull();
      wid = a.id;
    }
    auto info = ws.getWindowInfo(wid);
    if (!info.valid) return Value::makeNull();
    auto *vm = static_cast<VM *>(ctx->vm);
    auto obj = vm->createHostObject();
    vm->setHostObjectField(obj, "x", Value::makeInt(info.x));
    vm->setHostObjectField(obj, "y", Value::makeInt(info.y));
    vm->setHostObjectField(obj, "width", Value::makeInt(info.width));
    vm->setHostObjectField(obj, "height", Value::makeInt(info.height));
    return Value::makeObjectId(obj.id);
  };
  options.host_functions["window._toggleFullscreen"] = [ctx = ctx_](const auto &args) {
    if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
    ::havel::host::WindowService ws(ctx->windowManager);
    uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (wid == 0) return Value::makeBool(false);
    return Value::makeBool(ws.toggleFullscreen(wid));
  };
  options.host_functions["window._setOpacity"] = [ctx = ctx_](const auto &args) {
    return handleWindowSetOpacity(args, ctx);
  };
  options.host_functions["window._borderless"] = [ctx = ctx_](const auto &args) {
    return handleWindowBorderlessObj(args, ctx);
  };
  options.host_functions["window._isBorderless"] = [ctx = ctx_](const auto &args) {
    return handleWindowIsBorderless(args, ctx);
  };
  options.host_functions["window._toggleBorderless"] = [ctx = ctx_](const auto &args) {
    return handleWindowToggleBorderless(args, ctx);
  };
  options.host_functions["window._moveMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveMonitorObj(args, ctx);
  };
  options.host_functions["window._moveMonitorNext"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveMonitorNext(args, ctx);
  };
  options.host_functions["window._moveMonitorPrev"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveMonitorPrev(args, ctx);
  };
  options.host_functions["window._getCurrentMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetCurrentMonitor(args, ctx);
  };
  options.host_functions["window._getMonitors"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetMonitors(args, ctx);
  };
  options.host_functions["window._frameExtents"] = [ctx = ctx_](const auto &args) {
    return handleWindowFrameExtents(args, ctx);
  };
  options.host_functions["window._type"] = [ctx = ctx_](const auto &args) {
    return handleWindowType(args, ctx);
  };
  options.host_functions["window._states"] = [ctx = ctx_](const auto &args) {
    return handleWindowStates(args, ctx);
  };
  options.host_functions["window._getOpacity"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetOpacity(args, ctx);
  };
  options.host_functions["window._terminate"] = [ctx = ctx_](const auto &args) {
    return handleWindowTerminate(args, ctx);
  };
  options.host_functions["window._stickyToDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowStickyToDesktop(args, ctx);
  };
  options.host_functions["window._getDesktop"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetDesktop(args, ctx);
  };
  options.host_functions["window._exists"] = [ctx = ctx_](const auto &args) {
    return handleWindowExists(args, ctx);
  };
  options.host_functions["window._title"] = [ctx = ctx_](const auto &args) {
    return handleWindowTitleObj(args, ctx);
  };
  options.host_functions["window._class"] = [ctx = ctx_](const auto &args) {
    return handleWindowClassObj(args, ctx);
  };
  options.host_functions["window._exe"] = [ctx = ctx_](const auto &args) {
    return handleWindowExeObj(args, ctx);
  };
options.host_functions["window._pid"] = [ctx = ctx_](const auto &args) {
    return handleWindowPidObj(args, ctx);
};
options.host_functions["window._map"] = [ctx = ctx_](const auto &args) {
    return handleWindowMapObj(args, ctx);
};
options.host_functions["window._unmap"] = [ctx = ctx_](const auto &args) {
    return handleWindowUnmapObj(args, ctx);
};
options.host_functions["window._pin"] = [ctx = ctx_](const auto &args) {
    return handleWindowPinObj(args, ctx);
};
options.host_functions["window._wait"] = [ctx = ctx_](const auto &args) {
    return handleWindowWaitObj(args, ctx);
};
// Group operations
  options.host_functions["group.add"] = [ctx = ctx_](const auto &args) {
    return handleGroupAdd(args, ctx);
  };
  options.host_functions["group.remove"] = [ctx = ctx_](const auto &args) {
    return handleGroupRemove(args, ctx);
  };
  options.host_functions["group.get"] = [ctx = ctx_](const auto &args) {
    return handleGroupGet(args, ctx);
  };
  options.host_functions["group.list"] = [ctx = ctx_](const auto &args) {
    return handleGroupList(args, ctx);
  };
options.host_functions["group.find"] = [ctx = ctx_](const auto &args) {
    return handleGroupFind(args, ctx);
};
options.host_functions["group.findBy"] = [ctx = ctx_](const auto &args) {
    return handleGroupFindBy(args, ctx);
};
  // The Qt bridge owns every host function that needs a Qt class:
  // clipboard.*, io.getClipboard and gui.notify. The host selects it
  // explicitly at startup (see BridgeSelection), so this call site stays
  // Qt-free and the core never references QClipboard or GUIManager.
  if (auto installQtBridge = ::havel::qtBridgeInstaller())
    installQtBridge(options, ctx_);
    options.host_functions["screenshot.full"] = [ctx = ctx_](const auto &args) {
    return handleScreenshotFull(args, ctx);
  };
  options.host_functions["screenshot.monitor"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleScreenshotMonitor(args, ctx);
  };
}

// Helper: Create window object with data fields
// Methods are shared static functions that take window ID as first argument
static Value createWindowObject(
    VM *vm, const HostContext *ctx, uint64_t windowId,
    const std::string &title = "", const std::string &windowClass = "",
    const std::string &exe = "", int pid = 0, const std::string &cmdline = "") {
  if (!vm || !ctx || !ctx->windowManager) {
    return Value::makeNull();
  }

  VMApi api(*vm);
  auto obj = api.makeObject();
  api.setField(obj, "id", Value::makeInt(static_cast<int64_t>(windowId)));

  // Snapshot properties: win.title, win.class etc.
  api.setField(obj, "title", api.makeString(title));
  api.setField(obj, "class", api.makeString(windowClass));
  api.setField(obj, "exe", api.makeString(exe));
  api.setField(obj, "pid", Value::makeInt(static_cast<int64_t>(pid)));
  api.setField(obj, "cmd", api.makeString(cmdline));
  api.setField(obj, "cmdline", api.makeString(cmdline));

  // Action methods
  api.setField(obj, "close", api.makeFunctionRef("window._close"));
  api.setField(obj, "hide", api.makeFunctionRef("window._hide"));
  api.setField(obj, "show", api.makeFunctionRef("window._show"));
  api.setField(obj, "focus", api.makeFunctionRef("window._focus"));
  api.setField(obj, "min", api.makeFunctionRef("window._min"));
  api.setField(obj, "max", api.makeFunctionRef("window._max"));
  api.setField(obj, "resize", api.makeFunctionRef("window._resize"));
  api.setField(obj, "move", api.makeFunctionRef("window._move"));
  api.setField(obj, "restore", api.makeFunctionRef("window._restore"));
  api.setField(obj, "snap", api.makeFunctionRef("window._snap"));
  api.setField(obj, "center", api.makeFunctionRef("window._center"));
  api.setField(obj, "fullscreen", api.makeFunctionRef("window._fullscreen"));
  api.setField(obj, "moveResize", api.makeFunctionRef("window._moveResize"));
  api.setField(obj, "setAlwaysOnTop", api.makeFunctionRef("window._setAlwaysOnTop"));
  api.setField(obj, "pos", api.makeFunctionRef("window._pos"));
  api.setField(obj, "size", api.makeFunctionRef("window._size"));
  api.setField(obj, "setSize", api.makeFunctionRef("window._setSize"));
  api.setField(obj, "isMaximized", api.makeFunctionRef("window._isMaximized"));
  api.setField(obj, "isMinimized", api.makeFunctionRef("window._isMinimized"));
  api.setField(obj, "isFullscreen", api.makeFunctionRef("window._isFullscreen"));
  api.setField(obj, "unmax", api.makeFunctionRef("window._unmax"));
  api.setField(obj, "toggleMax", api.makeFunctionRef("window._toggleMax"));
  api.setField(obj, "unmin", api.makeFunctionRef("window._unmin"));
  api.setField(obj, "sticky", api.makeFunctionRef("window._sticky"));
  api.setField(obj, "isSticky", api.makeFunctionRef("window._isSticky"));
  api.setField(obj, "shade", api.makeFunctionRef("window._shade"));
  api.setField(obj, "isShaded", api.makeFunctionRef("window._isShaded"));
  api.setField(obj, "skipTaskbar", api.makeFunctionRef("window._skipTaskbar"));
  api.setField(obj, "isSkipTaskbar", api.makeFunctionRef("window._isSkipTaskbar"));
  api.setField(obj, "skipPager", api.makeFunctionRef("window._skipPager"));
  api.setField(obj, "isSkipPager", api.makeFunctionRef("window._isSkipPager"));
  api.setField(obj, "alwaysOnTopState", api.makeFunctionRef("window._alwaysOnTopState"));
  api.setField(obj, "isAlwaysOnTopState", api.makeFunctionRef("window._isAlwaysOnTopState"));
  api.setField(obj, "raise", api.makeFunctionRef("window._raise"));
  api.setField(obj, "lower", api.makeFunctionRef("window._lower"));
  api.setField(obj, "setPos", api.makeFunctionRef("window._setPos"));
  api.setField(obj, "geometry", api.makeFunctionRef("window._geometry"));
  api.setField(obj, "toggleFullscreen", api.makeFunctionRef("window._toggleFullscreen"));
  api.setField(obj, "setOpacity", api.makeFunctionRef("window._setOpacity"));
  api.setField(obj, "borderless", api.makeFunctionRef("window._borderless"));
  api.setField(obj, "isBorderless", api.makeFunctionRef("window._isBorderless"));
  api.setField(obj, "toggleBorderless", api.makeFunctionRef("window._toggleBorderless"));
  api.setField(obj, "moveMonitor", api.makeFunctionRef("window._moveMonitor"));
  api.setField(obj, "moveMonitorNext", api.makeFunctionRef("window._moveMonitorNext"));
  api.setField(obj, "moveMonitorPrev", api.makeFunctionRef("window._moveMonitorPrev"));
  api.setField(obj, "getCurrentMonitor", api.makeFunctionRef("window._getCurrentMonitor"));
  api.setField(obj, "getMonitors", api.makeFunctionRef("window._getMonitors"));
  api.setField(obj, "frameExtents", api.makeFunctionRef("window._frameExtents"));
  api.setField(obj, "type", api.makeFunctionRef("window._type"));
  api.setField(obj, "states", api.makeFunctionRef("window._states"));
  api.setField(obj, "getOpacity", api.makeFunctionRef("window._getOpacity"));
  api.setField(obj, "terminate", api.makeFunctionRef("window._terminate"));
  api.setField(obj, "stickyToDesktop", api.makeFunctionRef("window._stickyToDesktop"));
  api.setField(obj, "getDesktop", api.makeFunctionRef("window._getDesktop"));
  api.setField(obj, "exists", api.makeFunctionRef("window._exists"));

  return Value::makeObjectId(obj.asObjectId());
}


Value
UIBridge::handleWindowGetActive(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }
  // X11 round-trip: fiber-suspending under the A+C model. In a goroutine
  // the query runs on an EventQueue worker (XInitThreads makes Xlib
  // thread-safe process-wide); the window object is built VM-side on
  // resume. Top-level/init calls run inline — cost identical to today.
  auto *wm = ctx->windowManager;
  auto *vm = static_cast<VM *>(ctx->vm);
  compiler::VMApi api(*vm);
  return api.runBlocking(
      [wm]() -> compiler::AsyncCxxResult {
        ::havel::host::WindowService winService(wm);
        auto info = winService.getActiveWindowInfo();
        // C++ payload across the boundary: never Values.
        return std::static_pointer_cast<void>(
            std::make_shared<::havel::WindowInfo>(std::move(info)));
      },
      [vm, ctx](const compiler::AsyncCxxResult &cell) -> Value {
        auto info = std::static_pointer_cast<::havel::WindowInfo>(cell);
        if (!info || !info->valid) {
          return Value::makeNull();
        }
        return createWindowObject(vm, ctx, info->id, info->title,
                                  info->windowClass, info->exe, info->pid,
                                  info->cmdline);
      });
}


Value UIBridge::handleWindowCmd(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeNull();
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeNull();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getWindowInfo(wid);
  if (!info.valid) {
    return Value::makeNull();
  }
  auto ref = static_cast<VM *>(ctx->vm)->createRuntimeString(info.cmdline);
  return Value::makeStringId(ref.id);
}

// Forward declaration - defined below
static uint64_t resolveWindowId(const Value &arg,
                                ::havel::host::WindowService &winService,
                                VM *vm);


// Shared window object methods - take object as first argument, extract id
Value
UIBridge::handleWindowCloseObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.closeWindow(wid));
}


Value
UIBridge::handleWindowHideObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.hideWindow(wid));
}


Value
UIBridge::handleWindowShowObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.showWindow(wid));
}


Value
UIBridge::handleWindowFocusObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.focusWindow(wid));
}


Value
UIBridge::handleWindowMinObj(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.minimizeWindow(wid));
}


Value
UIBridge::handleWindowMaxObj(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.maximizeWindow(wid));
}


Value
UIBridge::handleWindowResizeObj(const std::vector<Value> &args,
                                const HostContext *ctx) {
  // Object method form: obj is args[0]; remaining args follow the module-level
  // resize() signature (w, h [, relative]).
  return handleWindowResize(args, ctx);
}


Value
UIBridge::handleWindowMoveObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  // Object method form: obj is args[0]; remaining args follow the module-level
  // move() signature (x, y [, speed] [, relative]).
  return handleWindowMove(args, ctx);
}

// Spec parsing shared by window.find / window.findAllBySpec. Semantics
// mirror modules/app/window.hv _parseSpec/_matchWindow:
//   "title foo" / "class bar" / "cmd baz" / "exe qux" -> substring match
//   "pid 1234" / "id 5678"                            -> exact match
//   bare string                                       -> title substring
static void parseWindowSpec(const std::string &selector, std::string &type,
                            std::string &value, bool &substring) {
  type = "title";
  value = selector;
  substring = true;
  auto spacePos = selector.find(' ');
  if (spacePos == std::string::npos)
    return;
  std::string prefix = selector.substr(0, spacePos);
  std::string rest = selector.substr(spacePos + 1);
  if (prefix == "title" || prefix == "class" || prefix == "cmd" ||
      prefix == "exe") {
    type = prefix;
    value = rest;
  } else if (prefix == "pid" || prefix == "id") {
    type = prefix;
    value = rest;
    substring = false;
  }
}

static bool matchWindowSpec(const ::havel::host::WindowInfo &win,
                            const std::string &type,
                            const std::string &rawValue, bool substring) {
  if (type == "title" || type == "class" || type == "cmd" || type == "exe") {
    std::string hay = type == "title"   ? win.title
                      : type == "class" ? win.windowClass
                      : type == "cmd"   ? win.cmdline
                                        : win.exe;
    std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
    std::string needle = rawValue;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    return substring ? hay.find(needle) != std::string::npos : hay == needle;
  }
  if (type == "pid") {
    try {
      return win.pid == std::stoi(rawValue);
    } catch (...) {
      return false;
    }
  }
  if (type == "id") {
    try {
      return static_cast<uint64_t>(std::stoull(rawValue)) == win.id;
    } catch (...) {
      return false;
    }
  }
  return false;
}


Value UIBridge::handleWindowFind(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);

  std::string selector;
  if (args[0].isStringId() || args[0].isStringValId())
    selector = vm->resolveStringKey(args[0]);
  else
    return Value::makeNull();

  std::string type, value;
  bool substring;
  parseWindowSpec(selector, type, value, substring);

  ::havel::host::WindowService winService(ctx->windowManager);
  for (const auto &win : winService.getAllWindows()) {
    if (matchWindowSpec(win, type, value, substring)) {
      return createWindowObject(vm, ctx, win.id, win.title, win.windowClass,
                                win.exe, win.pid, win.cmdline);
    }
  }
  return Value::makeNull();
}


Value UIBridge::handleWindowActiveId(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager)
    return Value::makeInt(0);
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid)
    return Value::makeInt(0);
  return Value::makeInt(static_cast<int64_t>(info.id));
}


// Exact-match finders mirroring window.hv findByTitle/findByClass/findByPid
Value UIBridge::handleWindowFindByTitle(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string name;
  if (args[0].isStringId() || args[0].isStringValId())
    name = vm->resolveStringKey(args[0]);
  else
    return Value::makeNull();

  ::havel::host::WindowService winService(ctx->windowManager);
  for (const auto &win : winService.getAllWindows()) {
    if (win.title == name)
      return createWindowObject(vm, ctx, win.id, win.title, win.windowClass,
                                win.exe, win.pid, win.cmdline);
  }
  return Value::makeNull();
}


Value UIBridge::handleWindowFindByClass(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string name;
  if (args[0].isStringId() || args[0].isStringValId())
    name = vm->resolveStringKey(args[0]);
  else
    return Value::makeNull();

  ::havel::host::WindowService winService(ctx->windowManager);
  for (const auto &win : winService.getAllWindows()) {
    if (win.windowClass == name)
      return createWindowObject(vm, ctx, win.id, win.title, win.windowClass,
                                win.exe, win.pid, win.cmdline);
  }
  return Value::makeNull();
}


Value UIBridge::handleWindowFindByPid(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  if (!args[0].isInt() && !args[0].isDouble())
    return Value::makeNull();
  int64_t want = args[0].isInt() ? args[0].asInt()
                                 : static_cast<int64_t>(args[0].asDouble());
  auto *vm = static_cast<VM *>(ctx->vm);

  ::havel::host::WindowService winService(ctx->windowManager);
  for (const auto &win : winService.getAllWindows()) {
    if (win.pid == want)
      return createWindowObject(vm, ctx, win.id, win.title, win.windowClass,
                                win.exe, win.pid, win.cmdline);
  }
  return Value::makeNull();
}


Value UIBridge::handleWindowFindAllBySpec(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);

  std::string selector;
  if (args[0].isStringId() || args[0].isStringValId())
    selector = vm->resolveStringKey(args[0]);
  else
    selector = "title";

  std::string type, value;
  bool substring;
  parseWindowSpec(selector, type, value, substring);

  ::havel::host::WindowService winService(ctx->windowManager);
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &win : winService.getAllWindows()) {
    if (!matchWindowSpec(win, type, value, substring))
      continue;
    auto winObj =
        createWindowObject(vm, ctx, win.id, win.title, win.windowClass,
                           win.exe, win.pid, win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
  }
  return Value::makeArrayId(arr.id);
}


// window.moveToDesktop(target, desktopNum) -> backend workspace move
Value UIBridge::handleWindowMoveToDesktop(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid =
      resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  int64_t desk = 0;
  if (args[1].isInt())
    desk = args[1].asInt();
  else if (args[1].isDouble())
    desk = static_cast<int64_t>(args[1].asDouble());
  else
    return Value::makeBool(false);
  return Value::makeBool(winService.moveWindowToWorkspace(wid, desk));
}


// window.setOpacity(target, value) where value is 0.0..1.0 like window.hv
Value UIBridge::handleWindowSetOpacity(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid =
      resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  double opacity = 1.0;
  if (args[1].isDouble())
    opacity = args[1].asDouble();
  else if (args[1].isInt())
    opacity = static_cast<double>(args[1].asInt());
  else
    return Value::makeBool(false);
  if (opacity < 0.0)
    opacity = 0.0;
  if (opacity > 1.0)
    opacity = 1.0;
  return Value::makeBool(
      ctx->windowManager->getBackend().setWindowOpacity(wid,
                                                        static_cast<float>(opacity)));
}


Value UIBridge::handleWindowGroupNames(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  ::havel::host::WindowService winService(ctx->windowManager);
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &name : winService.getGroupNames()) {
    auto ref = vm->createRuntimeString(name);
    vm->pushHostArrayValue(arr, Value::makeStringId(ref.id));
  }
  return Value::makeArrayId(arr.id);
}

// Helper: resolve window argument (ID, object with id field, or selector string) to window ID
static uint64_t resolveWindowId(const Value &arg,
                                ::havel::host::WindowService &winService,
                                VM *vm = nullptr) {
  uint64_t wid = 0;

  if (arg.isInt()) {
    wid = static_cast<uint64_t>(arg.asInt());
  } else if (arg.isObjectId() && vm) {
    auto obj = ObjectRef{arg.asObjectId(), true};
    auto idVal = vm->getHostObjectField(obj, "id");
    if (idVal.isInt())
      wid = static_cast<uint64_t>(idVal.asInt());
  } else if (arg.isStringId() && vm) {
    std::string selector = vm->toString(arg);
    size_t spacePos = selector.find(' ');
    if (spacePos != std::string::npos) {
      std::string type = selector.substr(0, spacePos);
      std::string value = selector.substr(spacePos + 1);
      auto windows = winService.getAllWindows();
      for (const auto &win : windows) {
        bool match = false;
        if (type == "title")
          match = win.title.find(value) != std::string::npos;
        else if (type == "class")
          match = win.windowClass.find(value) != std::string::npos;
        else if (type == "exe")
          match = win.exe.find(value) != std::string::npos;
        else if (type == "pid") {
          try {
            match = win.pid == std::stoi(value);
          } catch (...) {
          }
        }
        if (match) {
          wid = win.id;
          break;
        }
      }
    }
  }

  (void)winService;
  return wid;
}


Value
UIBridge::handleWindowClose(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.closeWindow(wid));
}


Value
UIBridge::handleWindowResize(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  // Signatures supported:
  //   window.resize(winObj, w, h [, relative])
  //   window.resize(w, h [, winId] [, relative])
  bool sawObject = !args.empty() && args[0].isObjectId();
  int wIdx = sawObject ? 1 : 0;
  int hIdx = wIdx + 1;
  int winIdIdx = hIdx + 1;
  int relIdx = winIdIdx + 1;
  if (sawObject) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    winIdIdx = -1; // already have the target from the object argument
    relIdx = hIdx + 1; // obj.resize(w, h, relative)
  }
  if (wid == 0) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (args.size() < static_cast<size_t>(hIdx + 1))
    return Value::makeBool(false);
  int w = 0, h = 0;
  bool relative = false;
  if (auto *v = (args[wIdx].isInt() ? &args[wIdx] : nullptr))
    w = static_cast<int>(v->asInt());
  else if (auto *v = (args[wIdx].isDouble() ? &args[wIdx] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[hIdx].isInt() ? &args[hIdx] : nullptr))
    h = static_cast<int>(v->asInt());
  else if (auto *v = (args[hIdx].isDouble() ? &args[hIdx] : nullptr))
    h = static_cast<int>(v->asInt());
  if (winIdIdx >= 0 && args.size() > static_cast<size_t>(winIdIdx)) {
    uint64_t cand = resolveWindowId(args[winIdIdx], winService, static_cast<VM *>(ctx->vm));
    if (cand != 0) wid = cand;
  }
  if (args.size() > static_cast<size_t>(relIdx)) {
    if (auto *v = (args[relIdx].isBool() ? &args[relIdx] : nullptr))
      relative = v->asBool();
  }
  if (relative) {
    auto cur = winService.getWindowInfo(wid);
    if (!cur.valid) return Value::makeBool(false);
    w += cur.width;
    h += cur.height;
  }
  return Value(winService.resizeWindow(wid, w, h));
}


Value
UIBridge::handleWindowMoveToMonitor(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  int monitor = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    monitor = static_cast<int>(v->asInt());
  else if (auto *v2 = (args[1].isDouble() ? &args[1] : nullptr))
    monitor = static_cast<int>(v2->asDouble());
  return Value(winService.moveWindowToMonitor(wid, monitor));
}


Value
UIBridge::handleWindowMoveToNextMonitor(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  bool follow = true;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (args.size() >= 2) {
      if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) follow = v->asBool();
    }
  }
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  auto info = ws.getWindowInfo(wid);
  if (!info.valid) return Value::makeBool(false);
  auto mons = DisplayManager::GetMonitors();
  const int n = static_cast<int>(mons.size());
  if (n == 0) return Value::makeBool(false);
  int cur = 0;
  for (int i = 0; i < n; ++i) {
    auto &m = mons[i];
    if (info.x >= m.x && info.x < m.x + m.width &&
        info.y >= m.y && info.y < m.y + m.height) { cur = i; break; }
  }
  int next = (cur + 1) % n;
  bool ok = ws.moveWindowToMonitor(wid, next);
  if (ok && follow) ws.focusWindow(wid);
  return Value::makeBool(ok);
}

// -- EWMH spec helpers (module-level aliases used by snapshot object methods) --


Value UIBridge::handleWindowMoveMonitorNext(const std::vector<Value> &args,
                                             const HostContext *ctx) {
  // object method: winObj.moveMonitorNext(follow = true)
  return handleWindowMoveToNextMonitor(args, ctx);
}

// -- _MOTIF_WM_HINTS helpers for *Borderless handlers --

static bool motifGetBorderless(wID windowId, bool &borderless) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XTrue);
  if (a == x11::XNone) return false;
  Atom actual; int fmt; unsigned long n = 0, ba = 0; unsigned char *prop = nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(windowId), a, 0, 5, x11::XFalse,
                         a, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return false;
  long deco = (n >= 3) ? reinterpret_cast<long *>(prop)[2] : 1;
  XFree(prop);
  borderless = (deco == 0);
  return true;
}

static bool motifSetBorderless(wID windowId, bool enable) {
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom a = XInternAtom(d, "_MOTIF_WM_HINTS", x11::XFalse);
  if (a == x11::XNone) return false;
  unsigned long hints[5] = {2UL, 0UL, enable ? 0UL : 1UL, 0UL, 0UL};
  int ok = XChangeProperty(d, static_cast<Window>(windowId), a, a, 32,
                           PropModeReplace,
                           reinterpret_cast<unsigned char *>(hints), 5);
  if (ok) XFlush(d);
  return ok != 0;
}


Value UIBridge::handleWindowBorderlessObj(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  // object method: winObj.borderless(enable = true)
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool enable = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) enable = v->asBool();
  }
  return Value::makeBool(motifSetBorderless(wid, enable));
}


Value UIBridge::handleWindowIsBorderless(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  // object method: winObj.isBorderless()
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool borderless = false;
  if (!motifGetBorderless(wid, borderless)) return Value::makeBool(false);
  return Value::makeBool(borderless);
}


Value UIBridge::handleWindowToggleBorderless(const std::vector<Value> &args,
                                             const HostContext *ctx) {
  // object method: winObj.toggleBorderless()
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool borderless = false;
  motifGetBorderless(wid, borderless);
  return Value::makeBool(motifSetBorderless(wid, !borderless));
}


Value UIBridge::handleWindowMoveMonitorObj(const std::vector<Value> &args,
                                           const HostContext *ctx) {
  // object method: winObj.moveMonitor(i, follow = true)
  if (args.size() < 2 || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  int monitor = 0;
  bool follow = true;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    monitor = static_cast<int>(v->asInt());
  else if (auto *v2 = (args[1].isDouble() ? &args[1] : nullptr))
    monitor = static_cast<int>(v2->asDouble());
  if (args.size() >= 3) {
    if (auto *v = (args[2].isBool() ? &args[2] : nullptr)) follow = v->asBool();
  }
  bool ok = ws.moveWindowToMonitor(wid, monitor);
  if (ok && follow) ws.focusWindow(wid);
  return Value::makeBool(ok);
}


Value UIBridge::handleWindowMoveMonitorPrev(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  // object method: winObj.moveMonitorPrev(follow = true)
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  bool follow = true;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
    if (args.size() >= 2) {
      if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) follow = v->asBool();
    }
  }
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  auto info = ws.getWindowInfo(wid);
  if (!info.valid) return Value::makeBool(false);
  auto mons = DisplayManager::GetMonitors();
  const int n = static_cast<int>(mons.size());
  if (n == 0) return Value::makeBool(false);
  int cur = 0;
  for (int i = 0; i < n; ++i) {
    auto &m = mons[i];
    if (info.x >= m.x && info.x < m.x + m.width &&
        info.y >= m.y && info.y < m.y + m.height) { cur = i; break; }
  }
  int prev = (cur - 1 + n) % n;
  bool ok = ws.moveWindowToMonitor(wid, prev);
  if (ok && follow) ws.focusWindow(wid);
  return Value::makeBool(ok);
}


Value UIBridge::handleWindowGetCurrentMonitor(const std::vector<Value> &args,
                                              const HostContext *ctx) {
  // object method: winObj.getCurrentMonitor() -> monitor index
  if (!ctx->windowManager) return Value::makeInt(0);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeInt(0);
    wid = a.id;
  }
  auto info = ws.getWindowInfo(wid);
  if (!info.valid) return Value::makeInt(0);
  auto mons = DisplayManager::GetMonitors();
  int idx = 0;
  for (const auto &m : mons) {
    if (info.x >= m.x && info.x < m.x + m.width &&
        info.y >= m.y && info.y < m.y + m.height) return Value::makeInt(idx);
    ++idx;
  }
  return Value::makeInt(0);
}


Value UIBridge::handleWindowGetMonitors(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->vm) return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto mons = DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();
  auto arrG = vm->makeRoot(Value::makeArrayId(arr.id));
  int idx = 0;
  for (const auto &m : mons) {
    compiler::VMApi api(*vm);
    auto obj = api.makeObject();
    api.setField(obj, "index", Value::makeInt(idx));
    api.setField(obj, "name", api.makeString(m.name));
    api.setField(obj, "x", Value::makeInt(m.x));
    api.setField(obj, "y", Value::makeInt(m.y));
    api.setField(obj, "width", Value::makeInt(m.width));
    api.setField(obj, "height", Value::makeInt(m.height));
    api.setField(obj, "primary", Value::makeBool(m.isPrimary));
    vm->pushHostArrayValue(arr, obj);
    ++idx;
  }
  return Value::makeArrayId(arr.id);
}

static Value _mkOrObject(VM *vm, const char *k, long v, const char *k1, long v1,
                         const char *k2, long v2, const char *k3, long v3) {
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, k,  Value::makeInt(v));
  vm->setHostObjectField(obj, k1, Value::makeInt(v1));
  vm->setHostObjectField(obj, k2, Value::makeInt(v2));
  vm->setHostObjectField(obj, k3, Value::makeInt(v3));
  return Value::makeObjectId(obj.id);
}


Value UIBridge::handleWindowFrameExtents(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeNull();
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeNull();
    wid = a.id;
  }
  Display *d = DisplayManager::GetDisplay();
  if (!d) return Value::makeNull();
  Atom a = XInternAtom(d, "_NET_FRAME_EXTENTS", x11::XTrue);
  if (a == x11::XNone) return Value::makeNull();
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 4, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return Value::makeNull();
  long l=0,r=0,t=0,b=0;
  if (n>=4){ l=reinterpret_cast<long*>(prop)[0]; r=reinterpret_cast<long*>(prop)[1];
             t=reinterpret_cast<long*>(prop)[2]; b=reinterpret_cast<long*>(prop)[3]; }
  if (prop) XFree(prop);
  auto *vm = static_cast<VM *>(ctx->vm);
  return _mkOrObject(vm, "left", l, "right", r, "top", t, "bottom", b);
}


Value UIBridge::handleWindowType(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeNull();
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeNull();
    wid = a.id;
  }
  Display *d = DisplayManager::GetDisplay();
  if (!d) return Value::makeNull();
  Atom a = XInternAtom(d, "_NET_WM_WINDOW_TYPE", x11::XTrue);
  if (a == x11::XNone) return Value::makeNull();
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 8, x11::XFalse,
                         XA_ATOM, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop || n == 0)
    return Value::makeNull();
  // first atom is the primary type
  Atom typeAtom = reinterpret_cast<Atom *>(prop)[0];
  char *name = XGetAtomName(d, typeAtom);
  std::string result = name ? name : "";
  if (name) XFree(name);
  XFree(prop);
  if (result.rfind("_NET_WM_WINDOW_TYPE_", 0) == 0)
    result = result.substr(20); // strip prefix for spec cleanliness
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  auto *vm = static_cast<VM *>(ctx->vm);
  compiler::VMApi api(*vm);
  return api.makeString(result);
}


Value UIBridge::handleWindowStates(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) return Value::makeNull();
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeNull();
    wid = a.id;
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  Display *d = DisplayManager::GetDisplay();
  if (!d) return Value::makeNull();
  Atom a = XInternAtom(d, "_NET_WM_STATE", x11::XTrue);
  if (a == x11::XNone) return Value::makeNull();
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 64, x11::XFalse,
                         XA_ATOM, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return Value::makeNull();
  auto arr = vm->createHostArray();
  auto arrG = vm->makeRoot(Value::makeArrayId(arr.id));
  for (unsigned long i = 0; i < n; ++i) {
    Atom at = reinterpret_cast<Atom *>(prop)[i];
    char *nm = XGetAtomName(d, at);
    compiler::VMApi api(*vm); vm->pushHostArrayValue(arr, nm ? api.makeString(nm) : api.makeString(""));
    if (nm) XFree(nm);
  }
  XFree(prop);
  return Value::makeArrayId(arr.id);
}


Value UIBridge::handleWindowMove(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  // Signatures supported:
  //   window.move(winObj, x, y [, speed] [, relative]) — object form
  //   window.move(x, y [, speed] [, winId] [, relative])
  bool sawObject = !args.empty() && args[0].isObjectId();
  // Object form prepends the window object; the remaining args follow the
  // module-level signature (x, y, speed, winId, relative).
  int xIdx = sawObject ? 1 : 0;
  int yIdx = xIdx + 1;
  int speedIdx = yIdx + 1;
  int winIdIdx = speedIdx + 1;
  int relIdx = winIdIdx + 1;
  if (sawObject) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    winIdIdx = -1; // already have the target from the object argument
    relIdx = speedIdx + 1; // obj.move(x, y, speed, relative)
  } else if (args.size() >= 3 && !args[2].isInt() && !args[2].isDouble()) {
    // args[2] is not a number => can't be speed => first arg is winId
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    xIdx = 1; yIdx = 2; speedIdx = 3; winIdIdx = -1; relIdx = 4;
  }
  if (wid == 0) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (args.size() < static_cast<size_t>(yIdx + 1))
    return Value::makeBool(false);
  int x = 0, y = 0;
  double speed = 0.0;
  bool relative = false;
  if (auto *v = (args[xIdx].isInt() ? &args[xIdx] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[yIdx].isInt() ? &args[yIdx] : nullptr))
    y = static_cast<int>(v->asInt());
  if (args.size() > static_cast<size_t>(speedIdx)) {
    if (auto *v = (args[speedIdx].isDouble() ? &args[speedIdx] : nullptr))
      speed = v->asDouble();
    else if (auto *v = (args[speedIdx].isInt() ? &args[speedIdx] : nullptr))
      speed = static_cast<double>(v->asInt());
  }
  if (winIdIdx >= 0 && args.size() > static_cast<size_t>(winIdIdx)) {
    uint64_t cand = resolveWindowId(args[winIdIdx], winService, static_cast<VM *>(ctx->vm));
    if (cand != 0) wid = cand;
  }
  if (args.size() > static_cast<size_t>(relIdx)) {
    if (auto *v = (args[relIdx].isBool() ? &args[relIdx] : nullptr))
      relative = v->asBool();
  }
  (void)speed; // animation speed hint — not implemented in X11 backend yet
  if (relative) {
    auto cur = winService.getWindowInfo(wid);
    if (!cur.valid) return Value::makeBool(false);
    x += cur.x;
    y += cur.y;
  }
  return Value(winService.moveWindow(wid, x, y));
}


Value UIBridge::handleWindowMoveRel(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  // moveRel(dx, dy, dw, dh) — relative move+resize on active window
  // moveRel(winId, dx, dy, dw, dh) — same on specific window
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  int dIdx = 0;
  if (args.size() >= 5) {  // first arg is window ID
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    dIdx = 1;
  } else if (args.size() >= 4) {  // use active window, all 4 are deltas
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  } else {
    return Value::makeBool(false);
  }
  if (wid == 0 || args.size() < static_cast<size_t>(dIdx + 4))
    return Value::makeBool(false);

  auto cur = winService.getWindowInfo(wid);
  if (!cur.valid) return Value::makeBool(false);

  int dx = 0, dy = 0, dw = 0, dh = 0;
  if (auto *v = (args[dIdx].isInt() ? &args[dIdx] : nullptr)) dx = static_cast<int>(v->asInt());
  if (auto *v = (args[dIdx + 1].isInt() ? &args[dIdx + 1] : nullptr)) dy = static_cast<int>(v->asInt());
  if (auto *v = (args[dIdx + 2].isInt() ? &args[dIdx + 2] : nullptr)) dw = static_cast<int>(v->asInt());
  if (auto *v = (args[dIdx + 3].isInt() ? &args[dIdx + 3] : nullptr)) dh = static_cast<int>(v->asInt());

  return Value(winService.moveResizeWindow(wid, cur.x + dx, cur.y + dy,
                                            cur.width + dw, cur.height + dh));
}


Value
UIBridge::handleWindowFocus(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  bool sawObj = false;
  if (!args.empty()) {
    sawObj = args[0].isObjectId();
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  bool ok = winService.focusWindow(wid);
  // Return original object for chainability when called as object method
  return (ok && sawObj) ? args[0] : Value::makeBool(ok);
}


Value
UIBridge::handleWindowMinimize(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.minimizeWindow(wid));
}


Value
UIBridge::handleWindowMaximize(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.maximizeWindow(wid));
}


Value UIBridge::handleWindowHide(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.hideWindow(wid));
}


Value UIBridge::handleWindowShow(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.showWindow(wid));
}


// Window query functions implementation
Value UIBridge::handleWindowAny(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    return Value::makeBool(false);
  }

  // Get selector string: "type value" where type is title/class/exe/pid/cmd
  std::string selector;
  // TODO: string support - for now just return false
  (void)selector;
  return Value::makeBool(false);

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return Value::makeBool(false);
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  ::havel::host::WindowService winService(ctx->windowManager);

  // Use anyWindow with predicate
  bool result = winService.anyWindow([&](const ::havel::host::WindowInfo &win) {
    if (type == "title") {
      return win.title.find(value) != std::string::npos;
    } else if (type == "class") {
      return win.windowClass.find(value) != std::string::npos;
    } else if (type == "exe") {
      return win.exe.find(value) != std::string::npos;
    } else if (type == "pid") {
      try {
        int pid = std::stoi(value);
        return win.pid == pid;
      } catch (...) {
        return false;
      }
    } else if (type == "cmd") {
      return win.cmdline.find(value) != std::string::npos;
    }
    return false;
  });

  return Value::makeBool(result);
}


Value
UIBridge::handleWindowCount(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeInt(static_cast<int64_t>(0));
  }

  ::havel::host::WindowService winService(ctx->windowManager);

  // If no selector provided, count all windows
  if (args.empty()) {
    auto windows = winService.getAllWindows();
    return Value::makeInt(static_cast<int64_t>(windows.size()));
  }

  // Get selector string
  std::string selector;
  // TODO: string support - for now just count all windows
  (void)selector;
  auto windows = winService.getAllWindows();
  return Value::makeInt(static_cast<int64_t>(windows.size()));

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    auto windows = winService.getAllWindows();
    return Value::makeInt(static_cast<int64_t>(windows.size()));
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  // Use countWindows with predicate
  int count = winService.countWindows([&](const ::havel::host::WindowInfo &win) {
    if (type == "title") {
      return win.title.find(value) != std::string::npos;
    } else if (type == "class") {
      return win.windowClass.find(value) != std::string::npos;
    } else if (type == "exe") {
      return win.exe.find(value) != std::string::npos;
    } else if (type == "pid") {
      try {
        int pid = std::stoi(value);
        return win.pid == pid;
      } catch (...) {
        return false;
      }
    } else if (type == "cmd") {
      return win.cmdline.find(value) != std::string::npos;
    }
    return false;
  });

  return Value::makeInt(static_cast<int64_t>(count));
}


Value
UIBridge::handleWindowFilter(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }
  if (args.empty()) {
    return Value::makeNull();
  }

  // Get selector string
  std::string selector;
  // TODO: string support - for now return null
  (void)selector;
  return Value::makeNull();

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return Value::makeNull();
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  ::havel::host::WindowService winService(ctx->windowManager);

  // Use filterWindows with predicate
  auto matchingWindows =
      winService.filterWindows([&](const ::havel::host::WindowInfo &win) {
        if (type == "title") {
          return win.title.find(value) != std::string::npos;
        } else if (type == "class") {
          return win.windowClass.find(value) != std::string::npos;
        } else if (type == "exe") {
          return win.exe.find(value) != std::string::npos;
        } else if (type == "pid") {
          try {
            int pid = std::stoi(value);
            return win.pid == pid;
          } catch (...) {
            return false;
          }
        } else if (type == "cmd") {
          return win.cmdline.find(value) != std::string::npos;
        }
        return false;
      });

  // Create array of window objects
  auto *vm = static_cast<VM *>(ctx->vm);
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &win : matchingWindows) {
    auto winObj =
        createWindowObject(vm, ctx, win.id, win.title, win.windowClass, win.exe,
                           win.pid, win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
  }

  return Value::makeArrayId(arr.id);
}

// ============================================================================
// Additional window operations
// ============================================================================


Value
UIBridge::handleWindowRestore(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.restoreWindow(wid));
}


Value UIBridge::handleWindowSnap(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  int posIdx = 0;
  if (!args.empty() && args[0].isObjectId()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    posIdx = 1;
  } else if (!args.empty()) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
    posIdx = 0;
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
    posIdx = -1;
  }
  if (wid == 0)
    return Value::makeBool(false);
  int position = 0;
  if (posIdx >= 0 && args.size() > static_cast<size_t>(posIdx)) {
    if (auto *v = (args[posIdx].isInt() ? &args[posIdx] : nullptr))
      position = static_cast<int>(v->asInt());
  }
  return Value::makeBool(winService.snapWindow(wid, position));
}


Value
UIBridge::handleWindowCenter(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.centerWindow(wid));
}


Value UIBridge::handleWindowFullscreen(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
  }
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.toggleFullscreen(wid));
}


Value UIBridge::handleWindowMoveResize(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  int xIdx = 0, yIdx = 1, wIdx = 2, hIdx = 3;
  // 5 args = wid + x,y,w,h; 4 args = x,y,w,h on active window
  if (args.size() >= 5 && args[0].isObjectId()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    xIdx = 1; yIdx = 2; wIdx = 3; hIdx = 4;
  } else if (args.size() >= 4) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
    xIdx = 0; yIdx = 1; wIdx = 2; hIdx = 3;
  } else {
    return Value::makeBool(false);
  }
  if (wid == 0 || args.size() < static_cast<size_t>(hIdx + 1))
    return Value::makeBool(false);
  int x = 0, y = 0, w = 0, h = 0;
  if (auto *v = (args[xIdx].isInt() ? &args[xIdx] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[yIdx].isInt() ? &args[yIdx] : nullptr))
    y = static_cast<int>(v->asInt());
  if (auto *v = (args[wIdx].isInt() ? &args[wIdx] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[hIdx].isInt() ? &args[hIdx] : nullptr))
    h = static_cast<int>(v->asInt());
  return Value::makeBool(winService.moveResizeWindow(wid, x, y, w, h));
}


Value
UIBridge::handleWindowSetAlwaysOnTop(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  int valIdx = 0;
  if (!args.empty() && args[0].isObjectId()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
    valIdx = 1;
  } else {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeBool(false);
    wid = info.id;
    valIdx = 0;
  }
  if (wid == 0)
    return Value::makeBool(false);
  bool onTop = true;
  if (args.size() > static_cast<size_t>(valIdx)) {
    if (auto *v = (args[valIdx].isBool() ? &args[valIdx] : nullptr))
      onTop = v->asBool();
    else if (auto *v = (args[valIdx].isInt() ? &args[valIdx] : nullptr))
      onTop = v->asInt() != 0;
  }
  return Value::makeBool(winService.setAlwaysOnTop(wid, onTop));
}


Value UIBridge::handleWindowPos(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  auto *vm = static_cast<VM *>(ctx->vm);
  // Signatures:
  //   window.pos(relative = false, windowId = <active>)   — module-level
  //   win.pos(relative = false)                           — object method (win
  //   injected as args[0])
  bool relative = false;
  uint64_t wid = 0;
  if (!args.empty() && args[0].isObjectId()) {
    wid = resolveWindowId(args[0], winService, vm);
    if (args.size() >= 2 && args[1].isBool()) relative = args[1].asBool();
  } else {
    if (!args.empty() && args[0].isBool()) relative = args[0].asBool();
    if (args.size() >= 2)
      wid = resolveWindowId(args[1], winService, vm);
  }
  if (wid == 0) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeNull();
    wid = info.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  // Relative: geometry-relative-to-parent (info.x/y). Absolute: translate to
  // root-window coordinates via the backend.
  int x = info.x, y = info.y;
  if (!relative) {
    auto abs = winService.getWindowAbsolutePosition(wid);
    if (abs.valid) { x = abs.x; y = abs.y; }
  }
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "x", Value::makeInt(x));
  vm->setHostObjectField(obj, "y", Value::makeInt(y));
  return Value::makeObjectId(obj.id);
}


Value UIBridge::handleWindowList(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  auto windows = winService.getAllWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &win : windows) {
    auto winObj = createWindowObject(vm, ctx, win.id, win.title,
        win.windowClass, win.exe, win.pid,
        win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
  }
  return Value::makeArrayId(arr.id);
}


Value UIBridge::handleWindowTitle(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  }
  if (wid == 0) {
    auto activeInfo = winService.getActiveWindowInfo();
    if (!activeInfo.valid) return Value::makeNull();
    wid = activeInfo.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto ref = vm->createRuntimeString(info.title);
  return Value::makeStringId(ref.id);
}


Value UIBridge::handleWindowClass(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  }
  if (wid == 0) {
    auto activeInfo = winService.getActiveWindowInfo();
    if (!activeInfo.valid) return Value::makeNull();
    wid = activeInfo.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto ref = vm->createRuntimeString(info.windowClass);
  return Value::makeStringId(ref.id);
}


Value UIBridge::handleWindowExe(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  }
  if (wid == 0) {
    auto activeInfo = winService.getActiveWindowInfo();
    if (!activeInfo.valid) return Value::makeNull();
    wid = activeInfo.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto ref = vm->createRuntimeString(info.exe);
  return Value::makeStringId(ref.id);
}


Value UIBridge::handleWindowPid(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeInt(0);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  }
  if (wid == 0) {
    auto activeInfo = winService.getActiveWindowInfo();
    if (!activeInfo.valid) return Value::makeInt(0);
    wid = activeInfo.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeInt(0);
  return Value::makeInt(static_cast<int64_t>(info.pid));
}


Value UIBridge::handleWindowId(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeInt(0);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  return Value::makeInt(static_cast<int64_t>(wid));
}


Value UIBridge::handleWindowArea(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeNull();
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "x", Value::makeInt(info.x));
  vm->setHostObjectField(obj, "y", Value::makeInt(info.y));
  vm->setHostObjectField(obj, "width", Value::makeInt(info.width));
  vm->setHostObjectField(obj, "height", Value::makeInt(info.height));
  return Value::makeObjectId(obj.id);
}


Value UIBridge::handleWindowEach(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
return Value::makeNull();
::havel::host::WindowService winService(ctx->windowManager);
auto windows = winService.getAllWindows();
auto *vm = static_cast<VM *>(ctx->vm);
auto arr = vm->createHostArray();
auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
for (const auto &win : windows) {
    auto winObj = createWindowObject(vm, ctx, win.id, win.title,
        win.windowClass, win.exe, win.pid,
        win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
}
return Value::makeArrayId(arr.id);
}


Value UIBridge::handleWindowSort(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  auto windows = winService.getAllWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string field = "title";
  if (args[0].isStringId()) {
    auto str = vm->toString(args[0]);
    if (!str.empty())
      field = str;
  }
  std::sort(windows.begin(), windows.end(),
            [&field](const ::havel::host::WindowInfo &a,
                      const ::havel::host::WindowInfo &b) {
              if (field == "title")
                return a.title < b.title;
              if (field == "class")
                return a.windowClass < b.windowClass;
              if (field == "exe")
                return a.exe < b.exe;
              if (field == "pid")
                return a.pid < b.pid;
              if (field == "id")
                return a.id < b.id;
              return a.title < b.title;
});
  auto arr = vm->createHostArray();
  auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &win : windows) {
    auto winObj = createWindowObject(vm, ctx, win.id, win.title,
        win.windowClass, win.exe, win.pid,
        win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
  }
  return Value::makeArrayId(arr.id);
}

// ============================================================================
// Object-method variants for new operations
// ============================================================================


Value
UIBridge::handleWindowRestoreObj(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.restoreWindow(wid));
}


Value UIBridge::handleWindowSnapObj(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  int position = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    position = static_cast<int>(v->asInt());
  return Value::makeBool(winService.snapWindow(wid, position));
}


Value
UIBridge::handleWindowCenterObj(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.centerWindow(wid));
}


Value
UIBridge::handleWindowFullscreenObj(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.toggleFullscreen(wid));
}


Value
UIBridge::handleWindowMoveResizeObj(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (args.size() < 5 || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  int x = 0, y = 0, w = 0, h = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    y = static_cast<int>(v->asInt());
  if (auto *v = (args[3].isInt() ? &args[3] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[4].isInt() ? &args[4] : nullptr))
    h = static_cast<int>(v->asInt());
  return Value::makeBool(winService.moveResizeWindow(wid, x, y, w, h));
}


Value
UIBridge::handleWindowSetAlwaysOnTopObj(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  bool onTop = true;
  if (auto *v = (args[1].isBool() ? &args[1] : nullptr))
    onTop = v->asBool();
  else if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    onTop = v->asInt() != 0;
  return Value::makeBool(winService.setAlwaysOnTop(wid, onTop));
}


Value UIBridge::handleWindowPosObj(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  return handleWindowPos(args, ctx);
}


Value UIBridge::handleWindowSizeObj(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty()) {
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  }
  if (wid == 0) {
    auto info = winService.getActiveWindowInfo();
    if (!info.valid) return Value::makeNull();
    wid = info.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "width", Value::makeInt(info.width));
  vm->setHostObjectField(obj, "height", Value::makeInt(info.height));
  vm->setHostObjectField(obj, "clientWidth", Value::makeInt(info.width));
  vm->setHostObjectField(obj, "clientHeight", Value::makeInt(info.height));
  return Value::makeObjectId(obj.id);
}


Value UIBridge::handleWindowSetSizeObj(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  // obj.setSize(w, h) — forward to module-level resize (w, h)
  return handleWindowResize(args, ctx);
}


Value UIBridge::handleWindowIsMaximized(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm)
    return Value::makeNull();
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeNull();
    wid = active.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  bool maxxed = info.maximized;
  vm->setHostObjectField(obj, "horizontal", Value::makeBool(maxxed));
  vm->setHostObjectField(obj, "vertical", Value::makeBool(maxxed));
  vm->setHostObjectField(obj, "any", Value::makeBool(maxxed));
  return Value::makeObjectId(obj.id);
}


Value UIBridge::handleWindowIsMinimized(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeBool(false);
    wid = active.id;
  }
  auto info = winService.getWindowInfo(wid);
  return Value::makeBool(info.valid && info.minimized);
}


Value UIBridge::handleWindowIsFullscreen(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeBool(false);
    wid = active.id;
  }
  auto info = winService.getWindowInfo(wid);
  return Value::makeBool(info.valid && info.fullscreen);
}


Value UIBridge::handleWindowUnmax(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeBool(false);
    wid = active.id;
  }
  return Value(winService.restoreWindow(wid));
}


Value UIBridge::handleWindowToggleMax(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeBool(false);
    wid = active.id;
  }
  auto info = winService.getWindowInfo(wid);
  if (!info.valid) return Value::makeBool(false);
  if (info.maximized)
    return Value(winService.restoreWindow(wid));
  return Value(winService.maximizeWindow(wid));
}


Value UIBridge::handleWindowUnmin(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = winService.getActiveWindowInfo();
    if (!active.valid) return Value::makeBool(false);
    wid = active.id;
  }
  return Value(winService.showWindow(wid));
}

// ---------------------------------------------------------------------------
// Workspace / desktop module-level helpers
// ---------------------------------------------------------------------------


Value UIBridge::handleWindowCurrentDesktop(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) return Value::makeInt(-1);
  return Value::makeInt(ctx->windowManager->getBackend().getCurrentWorkspace());
}


Value UIBridge::handleWindowDesktopCount(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) return Value::makeInt(0);
  auto ws = ctx->windowManager->getBackend().getWorkspaces();
  return Value::makeInt(static_cast<int64_t>(ws.size()));
}


Value UIBridge::handleWindowDesktopName(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) return Value::makeNull();
  int idx = -1;
  if (!args.empty() && args[0].isInt()) idx = static_cast<int>(args[0].asInt());
  auto ws = ctx->windowManager->getBackend().getWorkspaces();
  if (idx < 0 || idx >= static_cast<int>(ws.size())) return Value::makeNull();
  compiler::VMApi api(*static_cast<VM *>(ctx->vm));
  return api.makeString(ws[idx].name);
}


Value UIBridge::handleWindowViewport(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)args;
  if (!ctx->vm) return Value::makeNull();
  compiler::VMApi api(*static_cast<VM *>(ctx->vm));
  Display *d = DisplayManager::GetDisplay();
  if (!d) return Value::makeNull();
  Atom a = XInternAtom(d, "_NET_DESKTOP_VIEWPORT", x11::XTrue);
  if (a == x11::XNone) return Value::makeNull();
  Atom actual; int fmt; unsigned long n=0, ba=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, DefaultRootWindow(d), a, 0, 2, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess || !prop)
    return Value::makeNull();
  long vx=0, vy=0;
  if (n>=2){ vx=reinterpret_cast<long*>(prop)[0]; vy=reinterpret_cast<long*>(prop)[1]; }
  if (prop) XFree(prop);
  auto obj = api.makeObject();
  api.setField(obj, "x", Value::makeInt(vx));
  api.setField(obj, "y", Value::makeInt(vy));
  return obj;
}


Value UIBridge::handleWindowSwitchDesktop(const std::vector<Value> &args,
                                           const HostContext *ctx) {
  if (!ctx->windowManager || args.empty() || !args[0].isInt())
    return Value::makeBool(false);
  return Value::makeBool(
      ctx->windowManager->getBackend().switchToWorkspace(static_cast<int>(args[0].asInt())));
}



Value UIBridge::handleWindowSticky(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  bool on = true;
  // Calls from obj-method style `_w.sticky(on)` => args[0] = bool trigger
  // Calls like `_w.sticky()` (no args) => default true
  if (args.size() >= 1) {
    if (auto *v = (args[0].isBool() ? &args[0] : nullptr)) on = v->asBool();
    else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) on = (v->asInt() != 0);
  }
  // Only positional arg = explicit windowId/object; never passed when called as obj method
  if (args.size() >= 2)
    wid = resolveWindowId(args[1], ws, static_cast<VM *>(ctx->vm));
  else if (!args.empty() && !args[0].isBool())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  bool ok = ws.setSticky(wid, on);
  // Chainable when called with window object as receiver
  if (!args.empty() && args[0].isObjectId()) return args[0];
  return Value::makeBool(ok);

}
Value UIBridge::handleWindowIsSticky(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  return Value::makeBool(ws.isSticky(wid));

}
Value UIBridge::handleWindowShade(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool on = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) on = v->asBool();
    else if (auto *v = (args[1].isInt() ? &args[1] : nullptr)) on = (v->asInt() != 0);
  }
  bool ok = ws.setShaded(wid, on);
  if (args[0].isObjectId()) return args[0];
  return Value::makeBool(ok);

}
Value UIBridge::handleWindowIsShaded(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  return Value::makeBool(ws.isShaded(wid));

}
Value UIBridge::handleWindowSkipTaskbar(const std::vector<Value> &args,
                                           const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool on = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) on = v->asBool();
    else if (auto *v = (args[1].isInt() ? &args[1] : nullptr)) on = (v->asInt() != 0);
  }
  bool ok = ws.setSkipTaskbar(wid, on);
  if (args[0].isObjectId()) return args[0];
  return Value::makeBool(ok);

}
Value UIBridge::handleWindowIsSkipTaskbar(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  return Value::makeBool(ws.isSkipTaskbar(wid));

}
Value UIBridge::handleWindowSkipPager(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool on = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) on = v->asBool();
    else if (auto *v = (args[1].isInt() ? &args[1] : nullptr)) on = (v->asInt() != 0);
  }
  bool ok = ws.setSkipPager(wid, on);
  if (args[0].isObjectId()) return args[0];
  return Value::makeBool(ok);

}
Value UIBridge::handleWindowIsSkipPager(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  return Value::makeBool(ws.isSkipPager(wid));
}


Value UIBridge::handleWindowAlwaysOnTop(const std::vector<Value> &args,
                                          const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  bool on = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr)) on = v->asBool();
    else if (auto *v = (args[1].isInt() ? &args[1] : nullptr)) on = (v->asInt() != 0);
  }
  bool ok = ws.setAlwaysOnTop(wid, on);
  if (args[0].isObjectId()) return args[0];
  return Value::makeBool(ok);

}
Value UIBridge::handleWindowIsAlwaysOnTop(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  if (!ctx->windowManager) return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeBool(false);
    wid = a.id;
  }
  return Value::makeBool(ws.alwaysOnTop(wid));
}


Value UIBridge::handleWindowGetOpacity(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeDouble(1.0);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto a = ws.getActiveWindowInfo();
    if (!a.valid) return Value::makeDouble(1.0);
    wid = a.id;
  }
  double op = 1.0;
  if (!ws.getOpacity(wid, op)) return Value::makeDouble(1.0);
  return Value::makeDouble(op);
}


Value UIBridge::handleWindowTerminate(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx->windowManager || args.empty())
    return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  return Value::makeBool(ws.terminate(wid));
}


Value UIBridge::handleWindowStickyToDesktop(const std::vector<Value> &args,
                                            const HostContext *ctx) {
  if (!ctx->windowManager || args.empty())
    return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  return Value::makeBool(ws.setOnAllDesktops(wid));
}


Value UIBridge::handleWindowGetDesktop(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx->windowManager)
    return Value::makeInt(-1);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = 0;
  if (!args.empty())
    wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) {
    auto active = ws.getActiveWindowInfo();
    if (!active.valid) return Value::makeInt(-1);
    wid = active.id;
  }
  Display *d = DisplayManager::GetDisplay();
  if (!d) return Value::makeInt(-1);
  Atom a = XInternAtom(d, "_NET_WM_DESKTOP", x11::XTrue);
  if (a == x11::XNone) return Value::makeInt(-1);
  Atom actual;
  int fmt;
  unsigned long n = 0, ba = 0;
  unsigned char *prop = nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(wid), a, 0, 1, x11::XFalse,
                         XA_CARDINAL, &actual, &fmt, &n, &ba, &prop) != x11::XSuccess)
    return Value::makeInt(-1);
  long dsk = -1;
  if (prop && n >= 1) dsk = static_cast<long>(*reinterpret_cast<unsigned long *>(prop));
  if (prop) XFree(prop);
  return Value::makeInt(dsk);
}


Value UIBridge::handleWindowExists(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  if (!ctx->windowManager || args.empty())
    return Value::makeBool(false);
  ::havel::host::WindowService ws(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], ws, static_cast<VM *>(ctx->vm));
  if (wid == 0) return Value::makeBool(false);
  return Value::makeBool(ws.getWindowInfo(wid).valid);
}





Value
UIBridge::handleWindowTitleObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  return handleWindowTitle(args, ctx);
}


Value
UIBridge::handleWindowClassObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  return handleWindowClass(args, ctx);
}


Value UIBridge::handleWindowExeObj(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  return handleWindowExe(args, ctx);
}


Value UIBridge::handleWindowPidObj(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  return handleWindowPid(args, ctx);
}


Value UIBridge::handleWindowMap(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.showWindow(wid));
}


Value UIBridge::handleWindowUnmap(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  return Value::makeBool(winService.hideWindow(wid));
}


Value UIBridge::handleWindowPin(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, static_cast<VM *>(ctx->vm));
  if (wid == 0)
    return Value::makeBool(false);
  bool onTop = true;
  if (args.size() >= 2) {
    if (auto *v = (args[1].isBool() ? &args[1] : nullptr))
      onTop = v->asBool();
    else if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
      onTop = v->asInt() != 0;
  } else {
    auto info = winService.getWindowInfo(wid);
    if (info.valid) {
      // no second arg = toggle
      onTop = true; // X11 doesn't expose "is on top" easily, default to true
    }
  }
  return Value::makeBool(winService.setAlwaysOnTop(wid, onTop));
}


Value UIBridge::handleWindowWait(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager)
    return Value::makeBool(false);
  auto *vm = static_cast<VM *>(ctx->vm);
  compiler::VMApi api(*vm);
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService, vm);
  if (wid == 0)
    return Value::makeBool(false);
  std::string state = "show";
  if (args[1].isStringId()) {
    auto s = vm->toString(args[1]);
    if (!s.empty()) state = s;
  }
  int timeoutMs = 5000;
  if (args.size() >= 3 && args[2].isInt())
    timeoutMs = static_cast<int>(args[2].asInt());
  bool waitVisible = (state == "show" || state == "visible" || state == "map");

  // Poll loop with X11 round-trips + sleeps: runs on a worker under the
  // A+C model; goroutines park instead of stalling the VM thread for up
  // to the full timeout.
  auto *wm = ctx->windowManager;
  return api.runBlocking(
      [wm, wid, waitVisible, timeoutMs]() -> compiler::AsyncCxxResult {
        ::havel::host::WindowService svc(wm);
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
          auto info = svc.getWindowInfo(wid);
          if (info.valid) {
            if (waitVisible && !info.minimized) {
              return std::static_pointer_cast<void>(std::make_shared<bool>(true));
            }
            if (!waitVisible && info.minimized) {
              return std::static_pointer_cast<void>(std::make_shared<bool>(true));
            }
          } else {
            if (!waitVisible) {
              return std::static_pointer_cast<void>(std::make_shared<bool>(true));
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return std::static_pointer_cast<void>(std::make_shared<bool>(false));
      },
      [](const compiler::AsyncCxxResult &cell) -> Value {
        auto ok = std::static_pointer_cast<bool>(cell);
        return Value::makeBool(ok && *ok);
      });
}


Value UIBridge::handleWindowMapObj(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  return handleWindowMap(args, ctx);
}


Value UIBridge::handleWindowUnmapObj(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  return handleWindowUnmap(args, ctx);
}


Value UIBridge::handleWindowPinObj(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  return handleWindowPin(args, ctx);
}


Value UIBridge::handleWindowWaitObj(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  return handleWindowWait(args, ctx);
}

// ============================================================================
// Group operations
// ============================================================================

static std::unordered_map<std::string, std::vector<uint64_t>> &getGroupStore() {
  static std::unordered_map<std::string, std::vector<uint64_t>> groups;
  return groups;
}


Value UIBridge::handleGroupAdd(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.size() < 2 || !ctx->vm)
    return Value::makeBool(false);
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string groupName = vm->toString(args[0]);
  uint64_t wid = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else if (args[1].isObjectId()) {
    auto obj = ObjectRef{args[1].asObjectId(), true};
    auto idVal = vm->getHostObjectField(obj, "id");
    if (idVal.isInt())
      wid = static_cast<uint64_t>(idVal.asInt());
  }
  if (wid == 0 || groupName.empty())
    return Value::makeBool(false);
  auto &groups = getGroupStore();
  auto &members = groups[groupName];
  for (auto existing : members) {
    if (existing == wid)
      return Value::makeBool(false);
  }
  members.push_back(wid);
  return Value::makeBool(true);
}


Value UIBridge::handleGroupRemove(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.size() < 2 || !ctx->vm)
    return Value::makeBool(false);
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string groupName = vm->toString(args[0]);
  uint64_t wid = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else if (args[1].isObjectId()) {
    auto obj = ObjectRef{args[1].asObjectId(), true};
    auto idVal = vm->getHostObjectField(obj, "id");
    if (idVal.isInt())
      wid = static_cast<uint64_t>(idVal.asInt());
  }
  if (wid == 0 || groupName.empty())
    return Value::makeBool(false);
  auto &groups = getGroupStore();
  auto it = groups.find(groupName);
  if (it == groups.end())
    return Value::makeBool(false);
  auto &members = it->second;
  auto mid = std::find(members.begin(), members.end(), wid);
  if (mid == members.end())
    return Value::makeBool(false);
  members.erase(mid);
  if (members.empty())
    groups.erase(it);
  return Value::makeBool(true);
}


Value UIBridge::handleGroupGet(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string groupName = vm->toString(args[0]);
  if (groupName.empty())
    return Value::makeNull();
  auto &groups = getGroupStore();
  auto it = groups.find(groupName);
  if (it == groups.end())
    return Value::makeArrayId(vm->createHostArray().id);
  auto arr = vm->createHostArray();
  for (auto wid : it->second) {
    vm->pushHostArrayValue(arr, Value::makeInt(static_cast<int64_t>(wid)));
  }
  return Value::makeArrayId(arr.id);
}


Value UIBridge::handleGroupList(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx->vm)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
auto &groups = getGroupStore();
auto arr = vm->createHostArray();
auto arrGuard = vm->makeRoot(Value::makeArrayId(arr.id));
  for (const auto &[name, members] : groups) {
    auto obj = vm->createHostObject();
    auto objGuard = vm->makeRoot(Value::makeObjectId(obj.id));
    auto nameRef = vm->createRuntimeString(name);
    vm->setHostObjectField(obj, "name", Value::makeStringId(nameRef.id));
    vm->setHostObjectField(obj, "count",
                           Value::makeInt(static_cast<int64_t>(members.size())));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }
  return Value::makeArrayId(arr.id);
}


Value UIBridge::handleGroupFind(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.size() < 2 || !ctx->vm || !ctx->windowManager)
    return Value::makeInt(0);
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string groupName = vm->toString(args[0]);
  std::string selector = vm->toString(args[1]);
  if (groupName.empty())
    return Value::makeInt(0);
  auto &groups = getGroupStore();
  auto it = groups.find(groupName);
  if (it == groups.end())
    return Value::makeInt(0);
  ::havel::host::WindowService winService(ctx->windowManager);
  size_t spacePos = selector.find(' ');
  std::string type = (spacePos != std::string::npos)
                         ? selector.substr(0, spacePos)
                         : "title";
  std::string value = (spacePos != std::string::npos)
                          ? selector.substr(spacePos + 1)
                          : selector;
  for (auto wid : it->second) {
    auto info = winService.getWindowInfo(wid);
    if (!info.valid)
      continue;
    bool match = false;
    if (type == "title")
      match = info.title.find(value) != std::string::npos;
    else if (type == "class")
      match = info.windowClass.find(value) != std::string::npos;
    else if (type == "exe")
      match = info.exe.find(value) != std::string::npos;
    else if (type == "pid") {
      try {
        match = info.pid == std::stoi(value);
      } catch (...) {
      }
    }
    if (match)
      return createWindowObject(vm, ctx, info.id, info.title,
                                info.windowClass, info.exe, info.pid,
                                info.cmdline);
  }
  return Value::makeInt(0);
}


Value UIBridge::handleGroupFindBy(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.size() < 3 || !ctx->vm || !ctx->windowManager)
    return Value::makeInt(0);
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string groupName = vm->toString(args[0]);
  std::string field = vm->toString(args[1]);
  std::string value = vm->toString(args[2]);
  if (groupName.empty() || field.empty())
    return Value::makeInt(0);
  auto &groups = getGroupStore();
  auto it = groups.find(groupName);
  if (it == groups.end())
    return Value::makeInt(0);
  ::havel::host::WindowService winService(ctx->windowManager);
  for (auto wid : it->second) {
    auto info = winService.getWindowInfo(wid);
    if (!info.valid)
      continue;
    bool match = false;
    if (field == "title")
      match = info.title.find(value) != std::string::npos;
    else if (field == "class")
      match = info.windowClass.find(value) != std::string::npos;
    else if (field == "exe")
      match = info.exe.find(value) != std::string::npos;
    else if (field == "pid") {
      try {
        match = info.pid == std::stoi(value);
      } catch (...) {
      }
    } else if (field == "id") {
      try {
        match = static_cast<int64_t>(info.id) == std::stoll(value);
      } catch (...) {
      }
    }
    if (match)
      return createWindowObject(vm, ctx, info.id, info.title,
                                info.windowClass, info.exe, info.pid,
                                info.cmdline);
  }
  return Value::makeInt(0);
}




Value
UIBridge::handleScreenshotFull(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto& service = ::havel::host::ScreenshotService::getInstance();
  auto result = service.captureFullDesktop();
  (void)result;
  return Value::makeNull();
}


Value
UIBridge::handleScreenshotMonitor(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)ctx;
  int monitor = 0;
  if (!args.empty()) {
    if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
      monitor = static_cast<int>(v->asInt());
  }
  auto& service = ::havel::host::ScreenshotService::getInstance();
  auto result = service.captureMonitor(monitor);
  (void)result;
  return Value::makeNull();
}

// handleGUINotify lives in src/host/module/bridges/qt/QtGuiBridge.cpp: the
// concrete GUIManager it calls is a QObject, so the body cannot sit in this
// Qt-free translation unit.

// ============================================================================
// InputBridge Implementation
// ============================================================================

// Active window namespace implementations
Value UIBridge::handleActiveGet(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  return handleWindowGetActive(args, ctx);
}


Value
UIBridge::handleActiveTitle(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeNull();
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid || !ctx->vm) {
    return Value::makeNull();
  }
  auto ref = static_cast<VM *>(ctx->vm)->createRuntimeString(info.title);
  return Value::makeStringId(ref.id);
}


Value
UIBridge::handleActiveClass(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeNull();
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid || !ctx->vm) {
    return Value::makeNull();
  }
  auto ref = static_cast<VM *>(ctx->vm)->createRuntimeString(info.windowClass);
  return Value::makeStringId(ref.id);
}


Value UIBridge::handleActiveExe(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeNull();
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid || !ctx->vm) {
    return Value::makeNull();
  }
  auto ref = static_cast<VM *>(ctx->vm)->createRuntimeString(info.exe);
  return Value::makeStringId(ref.id);
}


Value UIBridge::handleActivePid(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeInt(static_cast<int64_t>(0));
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeInt(static_cast<int64_t>(0));
  }
  return Value::makeInt(static_cast<int64_t>(info.pid));
}


Value
UIBridge::handleActiveClose(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.closeWindow(info.id);
  return Value::makeBool(true);
}


Value UIBridge::handleActiveMin(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.minimizeWindow(info.id);
  return Value::makeBool(true);
}


Value UIBridge::handleActiveMax(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.maximizeWindow(info.id);
  return Value::makeBool(true);
}


Value UIBridge::handleActiveHide(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.hideWindow(info.id);
  return Value::makeBool(true);
}


Value UIBridge::handleActiveShow(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.showWindow(info.id);
  return Value::makeBool(true);
}


Value UIBridge::handleActiveMove(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  int64_t x = 0, y = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    x = v->asInt();
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    y = v->asInt();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.moveWindow(info.id, static_cast<int>(x), static_cast<int>(y));
  return Value::makeBool(true);
}


Value
UIBridge::handleActiveResize(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  int64_t w = 0, h = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    w = v->asInt();
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    h = v->asInt();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.resizeWindow(info.id, static_cast<int>(w), static_cast<int>(h));
  return Value::makeBool(true);
}

} // namespace havel::compiler
