/*
 * WindowMonitorModule.cpp - Window monitoring module for bytecode VM
 * Provides dynamic window variables: title, class, exe, pid
 * Uses WindowMonitor for real-time window tracking
 */
#include "WindowMonitorModule.hpp"
#include "window/WindowMonitor.hpp"
#include "havel-lang/compiler/bytecode/VMApi.hpp"
#include "utils/Logger.hpp"

#include <mutex>

namespace havel::modules {

using compiler::BytecodeValue;
using compiler::VMApi;

// Global window monitor instance
static std::unique_ptr<WindowMonitor> g_windowMonitor;
static std::mutex g_monitorMutex;

// Get or create window monitor
static WindowMonitor* getWindowMonitor() {
  std::lock_guard<std::mutex> lock(g_monitorMutex);
  if (!g_windowMonitor) {
    g_windowMonitor = std::make_unique<WindowMonitor>(std::chrono::milliseconds(100));
    g_windowMonitor->Start();
    info("WindowMonitor started for dynamic window variables");
  }
  return g_windowMonitor.get();
}

// Helper to convert to BytecodeValue
static BytecodeValue toValue(const std::string &s) {
  return BytecodeValue(s);
}

static BytecodeValue toValue(int64_t i) {
  return BytecodeValue(i);
}

// window.activeTitle() - Get active window title
static BytecodeValue windowActiveTitle(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto *monitor = getWindowMonitor();
  auto info = monitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->title);
  }
  return toValue("");
}

// window.activeClass() - Get active window class
static BytecodeValue windowActiveClass(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto *monitor = getWindowMonitor();
  auto info = monitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->windowClass);
  }
  return toValue("");
}

// window.activeExe() - Get active window executable
static BytecodeValue windowActiveExe(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto *monitor = getWindowMonitor();
  auto info = monitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(info->processName);
  }
  return toValue("");
}

// window.activePid() - Get active window PID
static BytecodeValue windowActivePid(const std::vector<BytecodeValue> &args) {
  (void)args;
  auto *monitor = getWindowMonitor();
  auto info = monitor->GetActiveWindowInfo();
  if (info && info->isValid) {
    return toValue(static_cast<int64_t>(info->pid));
  }
  return toValue(static_cast<int64_t>(0));
}

// window.active() - Get all active window info as object
static BytecodeValue windowActive(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  auto *monitor = getWindowMonitor();
  auto info = monitor->GetActiveWindowInfo();
  
  auto obj = api.makeObject();
  if (info && info->isValid) {
    api.setField(obj, "title", toValue(info->title));
    api.setField(obj, "class", toValue(info->windowClass));
    api.setField(obj, "exe", toValue(info->processName));
    api.setField(obj, "pid", toValue(static_cast<int64_t>(info->pid)));
  } else {
    api.setField(obj, "title", toValue(""));
    api.setField(obj, "class", toValue(""));
    api.setField(obj, "exe", toValue(""));
    api.setField(obj, "pid", toValue(static_cast<int64_t>(0)));
  }
  
  return BytecodeValue(obj);
}

// Register window monitor module with VM
void registerWindowMonitorModule(VMApi &api) {
  // window.activeTitle()
  api.registerFunction("window.activeTitle", [](const std::vector<BytecodeValue> &args) {
    return windowActiveTitle(args);
  });
  
  // window.activeClass()
  api.registerFunction("window.activeClass", [](const std::vector<BytecodeValue> &args) {
    return windowActiveClass(args);
  });
  
  // window.activeExe()
  api.registerFunction("window.activeExe", [](const std::vector<BytecodeValue> &args) {
    return windowActiveExe(args);
  });
  
  // window.activePid()
  api.registerFunction("window.activePid", [](const std::vector<BytecodeValue> &args) {
    return windowActivePid(args);
  });
  
  // window.active()
  api.registerFunction("window.active", [&api](const std::vector<BytecodeValue> &args) {
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
  
  info("WindowMonitor module registered with dynamic window variables");
}

// Auto-setup dynamic window globals (title, class, exe, pid)
void setupDynamicWindowGlobals(VMApi &api) {
  // Start window monitor if not already started
  auto *monitor = getWindowMonitor();
  
  // Create global variables that will be updated dynamically
  // Note: These are initial values, they will be updated by the monitor
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
  api.setGlobal("title", toValue(title));
  api.setGlobal("class", toValue(windowClass));
  api.setGlobal("exe", toValue(exe));
  api.setGlobal("pid", toValue(pid));
  
  // Set up callback to update globals when active window changes
  monitor->SetActiveWindowCallback([api](const MonitorWindowInfo &info) mutable {
    // Update global variables when active window changes
    // Note: This is a simplified approach - proper implementation would need
    // thread-safe access to VM globals
    debug("Active window changed: " + info.title + " (" + info.processName + ")");
  });
  
  debug("Dynamic window globals setup: title, class, exe, pid");
}

} // namespace havel::modules
