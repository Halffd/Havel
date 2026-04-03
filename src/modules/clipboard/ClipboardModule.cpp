/*
 * ClipboardModule.cpp - Clipboard module implementation for Havel bytecode VM
 */
#include "ClipboardModule.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "host/clipboard/Clipboard.hpp"
#include "host/clipboard/HistoryClipboard.hpp"
#include "host/clipboard/MonitoringClipboard.hpp"

#include <algorithm>

namespace havel::modules {

using compiler::ArrayRef;
using compiler::Value;
using compiler::ObjectRef;
using compiler::VMApi;

// Global clipboard instances (singleton per VM)
static std::unique_ptr<host::Clipboard> g_clipboard;
static std::unique_ptr<host::HistoryClipboard> g_historyClipboard;
static std::unique_ptr<host::MonitoringClipboard> g_monitoringClipboard;

static host::Clipboard &getClipboard() {
  if (!g_clipboard) {
    g_clipboard = std::make_unique<host::Clipboard>();
  }
  return *g_clipboard;
}

static host::HistoryClipboard &getHistoryClipboard() {
  if (!g_historyClipboard) {
    g_historyClipboard = std::make_unique<host::HistoryClipboard>();
  }
  return *g_historyClipboard;
}

static host::MonitoringClipboard &getMonitoringClipboard() {
  if (!g_monitoringClipboard) {
    g_monitoringClipboard = std::make_unique<host::MonitoringClipboard>();
  }
  return *g_monitoringClipboard;
}

// Helper to convert Value to string
static std::string toString(const Value &v) {
  if (v.isStringValId()) {
    // TODO: string pool lookup
    return "<string:" + std::to_string(v.asStringValId()) + ">";
  }
  if (v.isInt())
    return std::to_string(v.asInt());
  if (v.isDouble())
    return std::to_string(v.asDouble());
  if (v.isBool())
    return v.asBool() ? "true" : "false";
  return "";
}

static int toInt(const Value &v) {
  if (v.isInt())
    return static_cast<int>(v.asInt());
  if (v.isDouble())
    return static_cast<int>(v.asDouble());
  if (v.isStringValId()) {
    try {
      // TODO: string pool lookup
      std::string s = "<string:" + std::to_string(v.asStringValId()) + ">";
      return std::stoi(s);
    } catch (...) {
    }
  }
  return 0;
}

// ============================================================================
// Basic Clipboard Functions
// ============================================================================

// clipboard.get() -> string
static Value clipboardGet(const std::vector<Value> &args) {
  (void)args;
  std::string text = getClipboard().getText();
  // TODO: string pool integration - for now return null
  (void)text;
  return Value::makeNull();
}

// clipboard.set(text) -> bool
static Value clipboardSet(const std::vector<Value> &args) {
  std::string text = "";
  if (args.size() > 0) {
    text = toString(args[0]);
  }
  bool result = getClipboard().setText(text);
  return Value::makeBool(result);
}

// clipboard.clear() -> bool
static Value clipboardClear(const std::vector<Value> &args) {
  (void)args;
  bool result = getClipboard().clear();
  return Value::makeBool(result);
}

// clipboard.hasText() -> bool
static Value clipboardHasText(const std::vector<Value> &args) {
  (void)args;
  bool result = getClipboard().hasText();
  return Value::makeBool(result);
}

// clipboard.setMethod(method) -> bool
static Value
clipboardSetMethod(const std::vector<Value> &args) {
  std::string methodStr = "auto";
  if (args.size() > 0) {
    methodStr = toString(args[0]);
  }

  host::Clipboard::Method method = host::Clipboard::Method::AUTO;
  if (methodStr == "qt")
    method = host::Clipboard::Method::QT;
  else if (methodStr == "x11")
    method = host::Clipboard::Method::X11;
  else if (methodStr == "wayland")
    method = host::Clipboard::Method::WAYLAND;
  else if (methodStr == "external")
    method = host::Clipboard::Method::EXTERNAL;
  else if (methodStr == "windows")
    method = host::Clipboard::Method::WINDOWS;
  else if (methodStr == "macos")
    method = host::Clipboard::Method::MACOS;

  getClipboard().setMethod(method);
  return Value::makeBool(true);
}

// clipboard.getMethod() -> string
static Value
clipboardGetMethod(const std::vector<Value> &args) {
  (void)args;
  host::Clipboard::Method method = getClipboard().getMethod();
  std::string methodStr = "auto";
  switch (method) {
  case host::Clipboard::Method::QT:
    methodStr = "qt";
    break;
  case host::Clipboard::Method::X11:
    methodStr = "x11";
    break;
  case host::Clipboard::Method::WAYLAND:
    methodStr = "wayland";
    break;
  case host::Clipboard::Method::EXTERNAL:
    methodStr = "external";
    break;
  case host::Clipboard::Method::WINDOWS:
    methodStr = "windows";
    break;
  case host::Clipboard::Method::MACOS:
    methodStr = "macos";
    break;
  default:
    methodStr = "auto";
    break;
  }
  // TODO: string pool integration - for now return null
  (void)methodStr;
  return Value::makeNull();
}

// clipboard.detectMethod() -> string (detect best available method)
static Value
clipboardDetectMethod(const std::vector<Value> &args) {
  (void)args;
  host::Clipboard::Method method = host::Clipboard::detectBestMethod();
  std::string methodStr = "auto";
  switch (method) {
  case host::Clipboard::Method::QT:
    methodStr = "qt";
    break;
  case host::Clipboard::Method::X11:
    methodStr = "x11";
    break;
  case host::Clipboard::Method::WAYLAND:
    methodStr = "wayland";
    break;
  case host::Clipboard::Method::EXTERNAL:
    methodStr = "external";
    break;
  case host::Clipboard::Method::WINDOWS:
    methodStr = "windows";
    break;
  case host::Clipboard::Method::MACOS:
    methodStr = "macos";
    break;
  default:
    methodStr = "auto";
    break;
  }
  // TODO: string pool integration - for now return null
  (void)methodStr;
  return Value::makeNull();
}

// clipboard.out() -> object with type, content, size, mimeType, files
static Value clipboardOut(VMApi &api,
                                  const std::vector<Value> &args) {
  (void)args;
  host::ClipboardInfo info = getClipboard().getInfo();

  auto obj = api.makeObject();

  // Set type as string
  std::string typeStr;
  switch (info.type) {
  case host::ClipboardInfo::Type::TEXT:
    typeStr = "text";
    break;
  case host::ClipboardInfo::Type::IMAGE:
    typeStr = "image";
    break;
  case host::ClipboardInfo::Type::FILES:
    typeStr = "files";
    break;
  case host::ClipboardInfo::Type::EMPTY:
    typeStr = "empty";
    break;
  }
  api.setField(obj, "type", Value::makeNull());

  // Set content (text or base64 image)
  api.setField(obj, "content", Value::makeNull());

  // Set size
  api.setField(obj, "size", Value::makeInt(static_cast<int64_t>(info.size)));

  // Set mime type
  api.setField(obj, "mimeType", Value::makeNull());

  // Set files array if files type
  if (info.type == host::ClipboardInfo::Type::FILES) {
    auto filesArr = api.makeArray();
    for (const auto &file : info.files) {
      // TODO: string pool integration - for now push null
      api.push(filesArr, Value::makeNull());
    }
    api.setField(obj, "files", filesArr);
  } else {
    api.setField(obj, "files", api.makeArray());
  }

  // Add helper methods
  api.setField(obj, "isText", Value::makeBool(info.isText()));
  api.setField(obj, "isImage", Value::makeBool(info.isImage()));
  api.setField(obj, "isFiles", Value::makeBool(info.isFiles()));
  api.setField(obj, "isEmpty", Value::makeBool(info.isEmpty()));

  // Convenience getters
  api.setField(obj, "getText", Value::makeNull());
  api.setField(obj, "getImage", Value::makeNull());

  return obj;
}

// ============================================================================
// History Clipboard Functions
// ============================================================================

// clipboard.history.add(text)
static Value
clipboardHistoryAdd(const std::vector<Value> &args) {
  std::string text = "";
  if (args.size() > 0) {
    text = toString(args[0]);
  }
  getHistoryClipboard().addToHistory(text);
  return Value::makeBool(true);
}

// clipboard.history.get(index) -> string
static Value
clipboardHistoryGet(const std::vector<Value> &args) {
  int index = 0;
  if (args.size() > 0) {
    index = toInt(args[0]);
  }
  std::string text = getHistoryClipboard().getHistoryItem(index);
  // TODO: string pool integration - for now return null
  (void)text;
  return Value::makeNull();
}

// clipboard.history.count() -> int
static Value
clipboardHistoryCount(const std::vector<Value> &args) {
  (void)args;
  int count = getHistoryClipboard().getHistoryCount();
  return Value::makeInt(static_cast<int64_t>(count));
}

// clipboard.history.clear()
static Value
clipboardHistoryClear(const std::vector<Value> &args) {
  (void)args;
  getHistoryClipboard().clearHistory();
  return Value::makeBool(true);
}

// clipboard.history.getAll() -> array
static Value
clipboardHistoryGetAll(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  const auto &history = getHistoryClipboard().getHistory();
  auto arr = api.makeArray();
  for (const auto &item : history) {
    // TODO: string pool integration - for now push null
    api.push(arr, Value::makeNull());
  }
  return arr;
}

// clipboard.history.last() -> string
static Value
clipboardHistoryLast(const std::vector<Value> &args) {
  (void)args;
  std::string text = getHistoryClipboard().getLast();
  // TODO: string pool integration - for now return null
  (void)text;
  return Value::makeNull();
}

// clipboard.history.recent(count) -> array
static Value
clipboardHistoryRecent(VMApi &api, const std::vector<Value> &args) {
  int count = 10;
  if (args.size() > 0) {
    count = toInt(args[0]);
  }
  auto recent = getHistoryClipboard().getRecent(count);
  auto arr = api.makeArray();
  for (const auto &item : recent) {
    // TODO: string pool integration - for now push null
    api.push(arr, Value::makeNull());
  }
  return arr;
}

// clipboard.history.setMaxSize(size)
static Value
clipboardHistorySetMaxSize(const std::vector<Value> &args) {
  int size = 100;
  if (args.size() > 0) {
    size = toInt(args[0]);
  }
  getHistoryClipboard().setMaxHistorySize(size);
  return Value::makeBool(true);
}

// clipboard.history.getMaxSize() -> int
static Value
clipboardHistoryGetMaxSize(const std::vector<Value> &args) {
  (void)args;
  int size = getHistoryClipboard().getMaxHistorySize();
  return Value::makeInt(static_cast<int64_t>(size));
}

// clipboard.history.filter(pattern) -> array
static Value
clipboardHistoryFilter(VMApi &api, const std::vector<Value> &args) {
  std::string pattern = "";
  if (args.size() > 0) {
    pattern = toString(args[0]);
  }
  auto results = getHistoryClipboard().filter(pattern);
  auto arr = api.makeArray();
  for (const auto &item : results) {
    // TODO: string pool integration - for now push null
    api.push(arr, Value::makeNull());
  }
  return arr;
}

// clipboard.history.find(pattern) -> string (first match)
static Value
clipboardHistoryFind(const std::vector<Value> &args) {
  std::string pattern = "";
  if (args.size() > 0) {
    pattern = toString(args[0]);
  }
  auto results = getHistoryClipboard().find(pattern);
  if (!results.empty()) {
    // TODO: string pool integration - for now return null
    return Value::makeNull();
  }
  return Value::makeNull();
}

// clipboard.history.getRange(start, end) -> array
static Value
clipboardHistoryGetRange(VMApi &api, const std::vector<Value> &args) {
  int start = 0;
  int end = 10;
  if (args.size() > 0) {
    start = toInt(args[0]);
  }
  if (args.size() > 1) {
    end = toInt(args[1]);
  }
  auto results = getHistoryClipboard().getHistoryRange(start, end);
  auto arr = api.makeArray();
  for (const auto &item : results) {
    // TODO: string pool integration - for now push null
    api.push(arr, Value::makeNull());
  }
  return arr;
}

// clipboard.history.remove(index) -> bool
static Value
clipboardHistoryRemove(const std::vector<Value> &args) {
  if (args.size() < 1) {
    return Value::makeBool(false);
  }
  int index = toInt(args[0]);
  // Get current history
  auto history = getHistoryClipboard().getHistory();
  if (index < 0 || index >= static_cast<int>(history.size())) {
    return Value::makeBool(false);
  }
  // Clear and rebuild without the removed item
  getHistoryClipboard().clearHistory();
  for (int i = 0; i < static_cast<int>(history.size()); ++i) {
    if (i != index) {
      getHistoryClipboard().addToHistory(history[i]);
    }
  }
  return Value::makeBool(true);
}

// clipboard.history.search(pattern) -> array (case-insensitive)
static Value
clipboardHistorySearch(VMApi &api, const std::vector<Value> &args) {
  std::string pattern = "";
  if (args.size() > 0) {
    pattern = toString(args[0]);
  }
  // Convert pattern to lowercase for case-insensitive search
  std::string patternLower = pattern;
  std::transform(patternLower.begin(), patternLower.end(), patternLower.begin(),
                 ::tolower);

  auto history = getHistoryClipboard().getHistory();
  auto arr = api.makeArray();
  for (const auto &item : history) {
    std::string itemLower = item;
    std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(),
                   ::tolower);
    if (itemLower.find(patternLower) != std::string::npos) {
      // TODO: string pool integration - for now push null
      api.push(arr, Value::makeNull());
    }
  }
  return arr;
}

// clipboard.history.unique() -> array (remove duplicates)
static Value
clipboardHistoryUnique(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto history = getHistoryClipboard().getHistory();
  auto arr = api.makeArray();
  std::vector<std::string> seen;
  for (const auto &item : history) {
    if (std::find(seen.begin(), seen.end(), item) == seen.end()) {
      seen.push_back(item);
      // TODO: string pool integration - for now push null
      api.push(arr, Value::makeNull());
    }
  }
  return arr;
}

// clipboard.history.stats() -> object with statistics
static Value
clipboardHistoryStats(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto history = getHistoryClipboard().getHistory();

  auto obj = api.makeObject();
  api.setField(obj, "totalCount",
               Value::makeInt(static_cast<int64_t>(history.size())));
  api.setField(obj, "maxSize",
               Value::makeInt(static_cast<int64_t>(
                   getHistoryClipboard().getMaxHistorySize())));

  // Calculate total size
  size_t totalSize = 0;
  for (const auto &item : history) {
    totalSize += item.size();
  }
  api.setField(obj, "totalBytes",
               Value::makeInt(static_cast<int64_t>(totalSize)));

  // Average item size
  if (!history.empty()) {
    api.setField(
        obj, "avgSize",
        Value::makeInt(static_cast<int64_t>(totalSize / history.size())));
  } else {
    api.setField(obj, "avgSize", Value::makeInt(static_cast<int64_t>(0)));
  }

  return obj;
}

// ============================================================================
// Monitoring Clipboard Functions
// ============================================================================

// clipboard.monitor.start()
static Value
clipboardMonitorStart(const std::vector<Value> &args) {
  (void)args;
  getMonitoringClipboard().startMonitoring();
  return Value::makeBool(true);
}

// clipboard.monitor.stop()
static Value
clipboardMonitorStop(const std::vector<Value> &args) {
  (void)args;
  getMonitoringClipboard().stopMonitoring();
  return Value::makeBool(true);
}

// clipboard.monitor.isActive() -> bool
static Value
clipboardMonitorIsActive(const std::vector<Value> &args) {
  (void)args;
  bool active = getMonitoringClipboard().isMonitoring();
  return Value::makeBool(active);
}

// clipboard.monitor.setInterval(ms)
static Value
clipboardMonitorSetInterval(const std::vector<Value> &args) {
  int interval = 500;
  if (args.size() > 0) {
    interval = toInt(args[0]);
  }
  getMonitoringClipboard().setMonitorInterval(interval);
  return Value::makeBool(true);
}

// clipboard.monitor.getInterval() -> int
static Value
clipboardMonitorGetInterval(const std::vector<Value> &args) {
  (void)args;
  int interval = getMonitoringClipboard().getMonitorInterval();
  return Value::makeInt(static_cast<int64_t>(interval));
}

// clipboard.monitor.onChange(callback) - sets callback for changes
static Value
clipboardMonitorOnChange(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return Value::makeBool(false);
  }

  // Store callback reference
  // For now, we just set up the monitoring; callback invocation
  // would need more infrastructure
  getMonitoringClipboard().onClipboardChanged([](const std::string &text) {
    // TODO: Invoke Havel callback with text
    (void)text;
  });

  return Value::makeBool(true);
}

// ============================================================================
// Register Clipboard Module
// ============================================================================

void registerClipboardModule(compiler::VMApi &api) {
  // Basic clipboard functions
  api.registerFunction("clipboard.get",
                       [](const std::vector<Value> &args) {
                         return clipboardGet(args);
                       });

  api.registerFunction("clipboard.set",
                       [](const std::vector<Value> &args) {
                         return clipboardSet(args);
                       });

  api.registerFunction("clipboard.clear",
                       [](const std::vector<Value> &args) {
                         return clipboardClear(args);
                       });

  api.registerFunction("clipboard.hasText",
                       [](const std::vector<Value> &args) {
                         return clipboardHasText(args);
                       });

  api.registerFunction("clipboard.setMethod",
                       [](const std::vector<Value> &args) {
                         return clipboardSetMethod(args);
                       });

  api.registerFunction("clipboard.getMethod",
                       [](const std::vector<Value> &args) {
                         return clipboardGetMethod(args);
                       });

  api.registerFunction("clipboard.detectMethod",
                       [](const std::vector<Value> &args) {
                         return clipboardDetectMethod(args);
                       });

  api.registerFunction("clipboard.out",
                       [&api](const std::vector<Value> &args) {
                         return clipboardOut(api, args);
                       });

  // History functions
  api.registerFunction("clipboard.history.add",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryAdd(args);
                       });

  api.registerFunction("clipboard.history.get",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryGet(args);
                       });

  api.registerFunction("clipboard.history.count",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryCount(args);
                       });

  api.registerFunction("clipboard.history.clear",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryClear(args);
                       });

  api.registerFunction("clipboard.history.getAll",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryGetAll(api, args);
                       });

  api.registerFunction("clipboard.history.last",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryLast(args);
                       });

  api.registerFunction("clipboard.history.recent",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryRecent(api, args);
                       });

  api.registerFunction("clipboard.history.setMaxSize",
                       [](const std::vector<Value> &args) {
                         return clipboardHistorySetMaxSize(args);
                       });

  api.registerFunction("clipboard.history.getMaxSize",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryGetMaxSize(args);
                       });

  api.registerFunction("clipboard.history.filter",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryFilter(api, args);
                       });

  api.registerFunction("clipboard.history.find",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryFind(args);
                       });

  api.registerFunction("clipboard.history.getRange",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryGetRange(api, args);
                       });

  api.registerFunction("clipboard.history.remove",
                       [](const std::vector<Value> &args) {
                         return clipboardHistoryRemove(args);
                       });

  api.registerFunction("clipboard.history.search",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistorySearch(api, args);
                       });

  api.registerFunction("clipboard.history.unique",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryUnique(api, args);
                       });

  api.registerFunction("clipboard.history.stats",
                       [&api](const std::vector<Value> &args) {
                         return clipboardHistoryStats(api, args);
                       });

  // Monitoring functions
  api.registerFunction("clipboard.monitor.start",
                       [](const std::vector<Value> &args) {
                         return clipboardMonitorStart(args);
                       });

  api.registerFunction("clipboard.monitor.stop",
                       [](const std::vector<Value> &args) {
                         return clipboardMonitorStop(args);
                       });

  api.registerFunction("clipboard.monitor.isActive",
                       [](const std::vector<Value> &args) {
                         return clipboardMonitorIsActive(args);
                       });

  api.registerFunction("clipboard.monitor.setInterval",
                       [](const std::vector<Value> &args) {
                         return clipboardMonitorSetInterval(args);
                       });

  api.registerFunction("clipboard.monitor.getInterval",
                       [](const std::vector<Value> &args) {
                         return clipboardMonitorGetInterval(args);
                       });

  api.registerFunction("clipboard.monitor.onChange",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMonitorOnChange(api, args);
                       });

  // Register global 'clipboard' object
  auto clipboardObj = api.makeObject();

  // Basic methods
  api.setField(clipboardObj, "get", api.makeFunctionRef("clipboard.get"));
  api.setField(clipboardObj, "set", api.makeFunctionRef("clipboard.set"));
  api.setField(clipboardObj, "clear", api.makeFunctionRef("clipboard.clear"));
  api.setField(clipboardObj, "hasText",
               api.makeFunctionRef("clipboard.hasText"));
  api.setField(clipboardObj, "setMethod",
               api.makeFunctionRef("clipboard.setMethod"));
  api.setField(clipboardObj, "getMethod",
               api.makeFunctionRef("clipboard.getMethod"));
  api.setField(clipboardObj, "detectMethod",
               api.makeFunctionRef("clipboard.detectMethod"));
  api.setField(clipboardObj, "out", api.makeFunctionRef("clipboard.out"));

  // Create history sub-object
  auto historyObj = api.makeObject();
  api.setField(historyObj, "add", api.makeFunctionRef("clipboard.history.add"));
  api.setField(historyObj, "get", api.makeFunctionRef("clipboard.history.get"));
  api.setField(historyObj, "count",
               api.makeFunctionRef("clipboard.history.count"));
  api.setField(historyObj, "clear",
               api.makeFunctionRef("clipboard.history.clear"));
  api.setField(historyObj, "getAll",
               api.makeFunctionRef("clipboard.history.getAll"));
  api.setField(historyObj, "last",
               api.makeFunctionRef("clipboard.history.last"));
  api.setField(historyObj, "recent",
               api.makeFunctionRef("clipboard.history.recent"));
  api.setField(historyObj, "setMaxSize",
               api.makeFunctionRef("clipboard.history.setMaxSize"));
  api.setField(historyObj, "getMaxSize",
               api.makeFunctionRef("clipboard.history.getMaxSize"));
  api.setField(historyObj, "filter",
               api.makeFunctionRef("clipboard.history.filter"));
  api.setField(historyObj, "find",
               api.makeFunctionRef("clipboard.history.find"));
  api.setField(historyObj, "getRange",
               api.makeFunctionRef("clipboard.history.getRange"));
  api.setField(historyObj, "remove",
               api.makeFunctionRef("clipboard.history.remove"));
  api.setField(historyObj, "search",
               api.makeFunctionRef("clipboard.history.search"));
  api.setField(historyObj, "unique",
               api.makeFunctionRef("clipboard.history.unique"));
  api.setField(historyObj, "stats",
               api.makeFunctionRef("clipboard.history.stats"));
  api.setField(clipboardObj, "history", historyObj);

  // Create monitor sub-object
  auto monitorObj = api.makeObject();
  api.setField(monitorObj, "start",
               api.makeFunctionRef("clipboard.monitor.start"));
  api.setField(monitorObj, "stop",
               api.makeFunctionRef("clipboard.monitor.stop"));
  api.setField(monitorObj, "isActive",
               api.makeFunctionRef("clipboard.monitor.isActive"));
  api.setField(monitorObj, "setInterval",
               api.makeFunctionRef("clipboard.monitor.setInterval"));
  api.setField(monitorObj, "getInterval",
               api.makeFunctionRef("clipboard.monitor.getInterval"));
  api.setField(monitorObj, "onChange",
               api.makeFunctionRef("clipboard.monitor.onChange"));
  api.setField(clipboardObj, "monitor", monitorObj);

  api.setGlobal("clipboard", clipboardObj);
}

} // namespace havel::modules
