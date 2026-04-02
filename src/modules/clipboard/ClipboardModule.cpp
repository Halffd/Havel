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
using compiler::BytecodeValue;
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

// Helper to convert BytecodeValue to string
static std::string toString(const BytecodeValue &v) {
  if (std::holds_alternative<std::string>(v))
    return std::get<std::string>(v);
  if (std::holds_alternative<int64_t>(v))
    return std::to_string(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v))
    return std::to_string(std::get<double>(v));
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? "true" : "false";
  return "";
}

static int toInt(const BytecodeValue &v) {
  if (std::holds_alternative<int64_t>(v))
    return static_cast<int>(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v))
    return static_cast<int>(std::get<double>(v));
  if (std::holds_alternative<std::string>(v)) {
    try {
      return std::stoi(std::get<std::string>(v));
    } catch (...) {
    }
  }
  return 0;
}

// ============================================================================
// Basic Clipboard Functions
// ============================================================================

// clipboard.get() -> string
static BytecodeValue clipboardGet(const std::vector<BytecodeValue> &args) {
  (void)args;
  std::string text = getClipboard().getText();
  return BytecodeValue(text);
}

// clipboard.set(text) -> bool
static BytecodeValue clipboardSet(const std::vector<BytecodeValue> &args) {
  std::string text = "";
  if (args.size() > 0) {
    text = toString(args[0]);
  }
  bool result = getClipboard().setText(text);
  return BytecodeValue(result);
}

// clipboard.clear() -> bool
static BytecodeValue clipboardClear(const std::vector<BytecodeValue> &args) {
  (void)args;
  bool result = getClipboard().clear();
  return BytecodeValue(result);
}

// clipboard.hasText() -> bool
static BytecodeValue clipboardHasText(const std::vector<BytecodeValue> &args) {
  (void)args;
  bool result = getClipboard().hasText();
  return BytecodeValue(result);
}

// clipboard.setMethod(method) -> bool
static BytecodeValue
clipboardSetMethod(const std::vector<BytecodeValue> &args) {
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
  return BytecodeValue(true);
}

// clipboard.getMethod() -> string
static BytecodeValue
clipboardGetMethod(const std::vector<BytecodeValue> &args) {
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
  return BytecodeValue(methodStr);
}

// clipboard.detectMethod() -> string (detect best available method)
static BytecodeValue
clipboardDetectMethod(const std::vector<BytecodeValue> &args) {
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
  return BytecodeValue(methodStr);
}

// clipboard.out() -> object with type, content, size, mimeType, files
static BytecodeValue clipboardOut(VMApi &api,
                                  const std::vector<BytecodeValue> &args) {
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
  api.setField(obj, "type", BytecodeValue(typeStr));

  // Set content (text or base64 image)
  api.setField(obj, "content", BytecodeValue(info.content));

  // Set size
  api.setField(obj, "size", BytecodeValue(static_cast<int64_t>(info.size)));

  // Set mime type
  api.setField(obj, "mimeType", BytecodeValue(info.mimeType));

  // Set files array if files type
  if (info.type == host::ClipboardInfo::Type::FILES) {
    auto filesArr = api.makeArray();
    for (const auto &file : info.files) {
      api.push(filesArr, BytecodeValue(file));
    }
    api.setField(obj, "files", filesArr);
  } else {
    api.setField(obj, "files", api.makeArray());
  }

  // Add helper methods
  api.setField(obj, "isText", BytecodeValue(info.isText()));
  api.setField(obj, "isImage", BytecodeValue(info.isImage()));
  api.setField(obj, "isFiles", BytecodeValue(info.isFiles()));
  api.setField(obj, "isEmpty", BytecodeValue(info.isEmpty()));

  // Convenience getters
  api.setField(obj, "getText", BytecodeValue(info.getText()));
  api.setField(obj, "getImage", BytecodeValue(info.getImage()));

  return obj;
}

// ============================================================================
// History Clipboard Functions
// ============================================================================

// clipboard.history.add(text)
static BytecodeValue
clipboardHistoryAdd(const std::vector<BytecodeValue> &args) {
  std::string text = "";
  if (args.size() > 0) {
    text = toString(args[0]);
  }
  getHistoryClipboard().addToHistory(text);
  return BytecodeValue(true);
}

// clipboard.history.get(index) -> string
static BytecodeValue
clipboardHistoryGet(const std::vector<BytecodeValue> &args) {
  int index = 0;
  if (args.size() > 0) {
    index = toInt(args[0]);
  }
  std::string text = getHistoryClipboard().getHistoryItem(index);
  return BytecodeValue(text);
}

// clipboard.history.count() -> int
static BytecodeValue
clipboardHistoryCount(const std::vector<BytecodeValue> &args) {
  (void)args;
  int count = getHistoryClipboard().getHistoryCount();
  return BytecodeValue(static_cast<int64_t>(count));
}

// clipboard.history.clear()
static BytecodeValue
clipboardHistoryClear(const std::vector<BytecodeValue> &args) {
  (void)args;
  getHistoryClipboard().clearHistory();
  return BytecodeValue(true);
}

// clipboard.history.getAll() -> array
static BytecodeValue
clipboardHistoryGetAll(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  const auto &history = getHistoryClipboard().getHistory();
  auto arr = api.makeArray();
  for (const auto &item : history) {
    api.push(arr, BytecodeValue(item));
  }
  return arr;
}

// clipboard.history.last() -> string
static BytecodeValue
clipboardHistoryLast(const std::vector<BytecodeValue> &args) {
  (void)args;
  std::string text = getHistoryClipboard().getLast();
  return BytecodeValue(text);
}

// clipboard.history.recent(count) -> array
static BytecodeValue
clipboardHistoryRecent(VMApi &api, const std::vector<BytecodeValue> &args) {
  int count = 10;
  if (args.size() > 0) {
    count = toInt(args[0]);
  }
  auto recent = getHistoryClipboard().getRecent(count);
  auto arr = api.makeArray();
  for (const auto &item : recent) {
    api.push(arr, BytecodeValue(item));
  }
  return arr;
}

// clipboard.history.setMaxSize(size)
static BytecodeValue
clipboardHistorySetMaxSize(const std::vector<BytecodeValue> &args) {
  int size = 100;
  if (args.size() > 0) {
    size = toInt(args[0]);
  }
  getHistoryClipboard().setMaxHistorySize(size);
  return BytecodeValue(true);
}

// clipboard.history.getMaxSize() -> int
static BytecodeValue
clipboardHistoryGetMaxSize(const std::vector<BytecodeValue> &args) {
  (void)args;
  int size = getHistoryClipboard().getMaxHistorySize();
  return BytecodeValue(static_cast<int64_t>(size));
}

// clipboard.history.filter(pattern) -> array
static BytecodeValue
clipboardHistoryFilter(VMApi &api, const std::vector<BytecodeValue> &args) {
  std::string pattern = "";
  if (args.size() > 0) {
    pattern = toString(args[0]);
  }
  auto results = getHistoryClipboard().filter(pattern);
  auto arr = api.makeArray();
  for (const auto &item : results) {
    api.push(arr, BytecodeValue(item));
  }
  return arr;
}

// clipboard.history.find(pattern) -> string (first match)
static BytecodeValue
clipboardHistoryFind(const std::vector<BytecodeValue> &args) {
  std::string pattern = "";
  if (args.size() > 0) {
    pattern = toString(args[0]);
  }
  auto results = getHistoryClipboard().find(pattern);
  if (!results.empty()) {
    return BytecodeValue(results[0]);
  }
  return BytecodeValue("");
}

// clipboard.history.getRange(start, end) -> array
static BytecodeValue
clipboardHistoryGetRange(VMApi &api, const std::vector<BytecodeValue> &args) {
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
    api.push(arr, BytecodeValue(item));
  }
  return arr;
}

// clipboard.history.remove(index) -> bool
static BytecodeValue
clipboardHistoryRemove(const std::vector<BytecodeValue> &args) {
  if (args.size() < 1) {
    return BytecodeValue(false);
  }
  int index = toInt(args[0]);
  // Get current history
  auto history = getHistoryClipboard().getHistory();
  if (index < 0 || index >= static_cast<int>(history.size())) {
    return BytecodeValue(false);
  }
  // Clear and rebuild without the removed item
  getHistoryClipboard().clearHistory();
  for (int i = 0; i < static_cast<int>(history.size()); ++i) {
    if (i != index) {
      getHistoryClipboard().addToHistory(history[i]);
    }
  }
  return BytecodeValue(true);
}

// clipboard.history.search(pattern) -> array (case-insensitive)
static BytecodeValue
clipboardHistorySearch(VMApi &api, const std::vector<BytecodeValue> &args) {
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
      api.push(arr, BytecodeValue(item));
    }
  }
  return arr;
}

// clipboard.history.unique() -> array (remove duplicates)
static BytecodeValue
clipboardHistoryUnique(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  auto history = getHistoryClipboard().getHistory();
  auto arr = api.makeArray();
  std::vector<std::string> seen;
  for (const auto &item : history) {
    if (std::find(seen.begin(), seen.end(), item) == seen.end()) {
      seen.push_back(item);
      api.push(arr, BytecodeValue(item));
    }
  }
  return arr;
}

// clipboard.history.stats() -> object with statistics
static BytecodeValue
clipboardHistoryStats(VMApi &api, const std::vector<BytecodeValue> &args) {
  (void)args;
  auto history = getHistoryClipboard().getHistory();

  auto obj = api.makeObject();
  api.setField(obj, "totalCount",
               BytecodeValue(static_cast<int64_t>(history.size())));
  api.setField(obj, "maxSize",
               BytecodeValue(static_cast<int64_t>(
                   getHistoryClipboard().getMaxHistorySize())));

  // Calculate total size
  size_t totalSize = 0;
  for (const auto &item : history) {
    totalSize += item.size();
  }
  api.setField(obj, "totalBytes",
               BytecodeValue(static_cast<int64_t>(totalSize)));

  // Average item size
  if (!history.empty()) {
    api.setField(
        obj, "avgSize",
        BytecodeValue(static_cast<int64_t>(totalSize / history.size())));
  } else {
    api.setField(obj, "avgSize", BytecodeValue(static_cast<int64_t>(0)));
  }

  return obj;
}

// ============================================================================
// Monitoring Clipboard Functions
// ============================================================================

// clipboard.monitor.start()
static BytecodeValue
clipboardMonitorStart(const std::vector<BytecodeValue> &args) {
  (void)args;
  getMonitoringClipboard().startMonitoring();
  return BytecodeValue(true);
}

// clipboard.monitor.stop()
static BytecodeValue
clipboardMonitorStop(const std::vector<BytecodeValue> &args) {
  (void)args;
  getMonitoringClipboard().stopMonitoring();
  return BytecodeValue(true);
}

// clipboard.monitor.isActive() -> bool
static BytecodeValue
clipboardMonitorIsActive(const std::vector<BytecodeValue> &args) {
  (void)args;
  bool active = getMonitoringClipboard().isMonitoring();
  return BytecodeValue(active);
}

// clipboard.monitor.setInterval(ms)
static BytecodeValue
clipboardMonitorSetInterval(const std::vector<BytecodeValue> &args) {
  int interval = 500;
  if (args.size() > 0) {
    interval = toInt(args[0]);
  }
  getMonitoringClipboard().setMonitorInterval(interval);
  return BytecodeValue(true);
}

// clipboard.monitor.getInterval() -> int
static BytecodeValue
clipboardMonitorGetInterval(const std::vector<BytecodeValue> &args) {
  (void)args;
  int interval = getMonitoringClipboard().getMonitorInterval();
  return BytecodeValue(static_cast<int64_t>(interval));
}

// clipboard.monitor.onChange(callback) - sets callback for changes
static BytecodeValue
clipboardMonitorOnChange(VMApi &api, const std::vector<BytecodeValue> &args) {
  if (args.size() < 1) {
    return BytecodeValue(false);
  }

  // Store callback reference
  // For now, we just set up the monitoring; callback invocation
  // would need more infrastructure
  getMonitoringClipboard().onClipboardChanged([](const std::string &text) {
    // TODO: Invoke Havel callback with text
    (void)text;
  });

  return BytecodeValue(true);
}

// ============================================================================
// Register Clipboard Module
// ============================================================================

void registerClipboardModule(compiler::VMApi &api) {
  // Basic clipboard functions
  api.registerFunction("clipboard.get",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardGet(args);
                       });

  api.registerFunction("clipboard.set",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardSet(args);
                       });

  api.registerFunction("clipboard.clear",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardClear(args);
                       });

  api.registerFunction("clipboard.hasText",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHasText(args);
                       });

  api.registerFunction("clipboard.setMethod",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardSetMethod(args);
                       });

  api.registerFunction("clipboard.getMethod",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardGetMethod(args);
                       });

  api.registerFunction("clipboard.detectMethod",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardDetectMethod(args);
                       });

  api.registerFunction("clipboard.out",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardOut(api, args);
                       });

  // History functions
  api.registerFunction("clipboard.history.add",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryAdd(args);
                       });

  api.registerFunction("clipboard.history.get",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryGet(args);
                       });

  api.registerFunction("clipboard.history.count",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryCount(args);
                       });

  api.registerFunction("clipboard.history.clear",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryClear(args);
                       });

  api.registerFunction("clipboard.history.getAll",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryGetAll(api, args);
                       });

  api.registerFunction("clipboard.history.last",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryLast(args);
                       });

  api.registerFunction("clipboard.history.recent",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryRecent(api, args);
                       });

  api.registerFunction("clipboard.history.setMaxSize",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistorySetMaxSize(args);
                       });

  api.registerFunction("clipboard.history.getMaxSize",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryGetMaxSize(args);
                       });

  api.registerFunction("clipboard.history.filter",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryFilter(api, args);
                       });

  api.registerFunction("clipboard.history.find",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryFind(args);
                       });

  api.registerFunction("clipboard.history.getRange",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryGetRange(api, args);
                       });

  api.registerFunction("clipboard.history.remove",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryRemove(args);
                       });

  api.registerFunction("clipboard.history.search",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistorySearch(api, args);
                       });

  api.registerFunction("clipboard.history.unique",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryUnique(api, args);
                       });

  api.registerFunction("clipboard.history.stats",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return clipboardHistoryStats(api, args);
                       });

  // Monitoring functions
  api.registerFunction("clipboard.monitor.start",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardMonitorStart(args);
                       });

  api.registerFunction("clipboard.monitor.stop",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardMonitorStop(args);
                       });

  api.registerFunction("clipboard.monitor.isActive",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardMonitorIsActive(args);
                       });

  api.registerFunction("clipboard.monitor.setInterval",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardMonitorSetInterval(args);
                       });

  api.registerFunction("clipboard.monitor.getInterval",
                       [](const std::vector<BytecodeValue> &args) {
                         return clipboardMonitorGetInterval(args);
                       });

  api.registerFunction("clipboard.monitor.onChange",
                       [&api](const std::vector<BytecodeValue> &args) {
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
