/*
 * WindowMonitorModule.cpp - Window monitoring module for bytecode VM
 * Provides dynamic window variables: title, class, exe, pid
 * Uses EXISTING WindowMonitor from HotkeyManager (no duplicate instances)
 */
#include "WindowMonitorModule.hpp"
#include "window/WindowMonitor.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// Shared window monitor pointer (set by setupDynamicWindowGlobals)
static WindowMonitor* g_sharedMonitor = nullptr;

// Helper to convert to Value
// TODO: String values need string pool registration - use makeNull() for now
static Value toValue(const std::string &s) {
  (void)s;
  return Value::makeNull();
}

static Value toValue(int64_t i) {
  return Value::makeInt(i);
}

// window.activeTitle() - Get active window title
static Value windowActiveTitle(const std::vector<Value> &args) {
  (void)args;
  if (!g_sharedMonitor) return toValue("");
  auto info = g_sharedMonitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->title);
  }
  return toValue("");
}

// window.activeClass() - Get active window class
static Value windowActiveClass(const std::vector<Value> &args) {
  (void)args;
  if (!g_sharedMonitor) return toValue("");
  auto info = g_sharedMonitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->windowClass);
  }
  return toValue("");
}

// window.activeExe() - Get active window executable
static Value windowActiveExe(const std::vector<Value> &args) {
  (void)args;
  if (!g_sharedMonitor) return toValue("");
  auto info = g_sharedMonitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->processName);
  }
  return toValue("");
}

// window.activePid() - Get active window PID
static Value windowActivePid(const std::vector<Value> &args) {
  (void)args;
  if (!g_sharedMonitor) return toValue(static_cast<int64_t>(0));
  auto info = g_sharedMonitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(static_cast<int64_t>(info->pid));
  }
  return toValue(static_cast<int64_t>(0));
}

// window.active() - Get all active window info as object
static Value windowActive(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  
  auto obj = api.makeObject();
  if (g_sharedMonitor) {
    auto info = g_sharedMonitor->GetActiveWindowInfo();
    if (info && info->isValid) {
      api.setField(obj, "title", toValue(info->title));
      api.setField(obj, "class", toValue(info->windowClass));
      api.setField(obj, "exe", toValue(info->processName));
      api.setField(obj, "pid", toValue(static_cast<int64_t>(info->pid)));
      return obj;
    }
  }

  api.setField(obj, "title", toValue(""));
  api.setField(obj, "class", toValue(""));
  api.setField(obj, "exe", toValue(""));
  api.setField(obj, "pid", toValue(static_cast<int64_t>(0)));
  return obj;
}

// Register window monitor module with VM
void registerWindowMonitorModule(VMApi &api) {
  // window.activeTitle()
  api.registerFunction("window.activeTitle", [](const std::vector<Value> &args) {
    return windowActiveTitle(args);
  });

  // window.activeClass()
  api.registerFunction("window.activeClass", [](const std::vector<Value> &args) {
    return windowActiveClass(args);
  });

  // window.activeExe()
  api.registerFunction("window.activeExe", [](const std::vector<Value> &args) {
    return windowActiveExe(args);
  });

  // window.activePid()
  api.registerFunction("window.activePid", [](const std::vector<Value> &args) {
    return windowActivePid(args);
  });

  // window.active()
  api.registerFunction("window.active", [&api](const std::vector<Value> &args) {
    return windowActive(api, args);
  });

  // Register window global object with methods
  auto windowObj = api.makeObject();
  api.setField(windowObj, "activeTitle", api.makeFunctionRef("window.activeTitle"));
  api.setField(windowObj, "activeClass", api.makeFunctionRef("window.activeClass"));
  api.setField(windowObj, "activeExe", api.makeFunctionRef("window.activeExe"));
  api.setField(windowObj, "activePid", api.makeFunctionRef("window.activePid"));
  api.setField(windowObj, "active", api.makeFunctionRef("window.active"));
  api.setGlobal("window", windowObj);

  debug("WindowMonitor module registered");
}

// Setup dynamic window globals - MUST be called with existing WindowMonitor
// This integrates with the WindowMonitor from HotkeyManager
void setupDynamicWindowGlobals(VMApi &api, WindowMonitor *monitor) {
  if (!monitor) {
    debug("WindowMonitor not available, skipping dynamic window globals");
    return;
  }
  
  // Store shared pointer
  g_sharedMonitor = monitor;
  
  // Get initial window info
  auto info = monitor->GetActiveWindowInfo();
  
  std::string title = "";
  std::string windowClass = "";
  std::string exe = "";
  int64_t pid = 0;
  
  if (info && info->isValid) {
    title = info->title;
    windowClass = info->windowClass;
    exe = info->processName;
    pid = static_cast<int64_t>(info->pid);
  }
  
  // Set initial global values
  // TODO: String values need string pool registration
  api.setGlobal("title", Value::makeNull());
  api.setGlobal("class", Value::makeNull());
  api.setGlobal("exe", Value::makeNull());
  api.setGlobal("pid", Value::makeInt(pid));
  
  debug("Dynamic window globals setup: title=" + title + ", exe=" + exe);
}

} // namespace havel::modules
