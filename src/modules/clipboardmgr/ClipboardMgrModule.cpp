/*
 * ClipboardMgrModule.cpp - GUI Clipboard Manager module for Havel bytecode VM
 * Disabled by default, can be enabled from script via clipboardMgr.enable()
 */
#include "ClipboardMgrModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "core/IO.hpp"
#include "gui/clipboard_manager/ClipboardManager.hpp"
#include "gui/automation_suite/AutomationSuite.hpp"
#include <QApplication>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// ============================================================================
// Clipboard Manager Module State
// ============================================================================

static bool clipboardMgrEnabled = false;
static ClipboardManager* clipboardMgrInstance = nullptr;

// Get or create the clipboard manager instance
static ClipboardManager* getClipboardManager(VMApi &api) {
  if (!clipboardMgrInstance) {
    // Check if QApplication exists (required for GUI)
    if (!QApplication::instance()) {
      return nullptr;
    }
    
    IO* io = nullptr;
    if (const auto* hc = api.vm.hostContext()) {
      io = hc->io;
    }
    if (!io) {
      return nullptr;
    }
    
    // Create the clipboard manager
    clipboardMgrInstance = new ClipboardManager(io);
  }
  return clipboardMgrInstance;
}

// ============================================================================
// Module Functions
// ============================================================================

// clipboardMgr.enable() -> bool - Enable the GUI clipboard manager
static Value clipboardMgrEnable(VMApi &api, const std::vector<Value> &args) {
  if (clipboardMgrEnabled) {
    return Value::makeBool(true);  // Already enabled
  }
  
  if (!QApplication::instance()) {
    return Value::makeBool(false);  // No GUI available
  }
  
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  clipboardMgrEnabled = true;
  mgr->show();
  return Value::makeBool(true);
}

// clipboardMgr.disable() -> bool - Disable/hide the clipboard manager
static Value clipboardMgrDisable(VMApi &api, const std::vector<Value> &args) {
  if (!clipboardMgrEnabled || !clipboardMgrInstance) {
    return Value::makeBool(false);
  }
  
  clipboardMgrInstance->hide();
  clipboardMgrEnabled = false;
  return Value::makeBool(true);
}

// clipboardMgr.isEnabled() -> bool - Check if enabled
static Value clipboardMgrIsEnabled(VMApi &api, const std::vector<Value> &args) {
  return Value::makeBool(clipboardMgrEnabled);
}

// clipboardMgr.show() -> bool - Show the clipboard manager window
static Value clipboardMgrShow(VMApi &api, const std::vector<Value> &args) {
  if (!clipboardMgrEnabled) {
    return Value::makeBool(false);
  }
  
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  mgr->show();
  return Value::makeBool(true);
}

// clipboardMgr.hide() -> bool - Hide the clipboard manager window
static Value clipboardMgrHide(VMApi &api, const std::vector<Value> &args) {
  if (!clipboardMgrEnabled || !clipboardMgrInstance) {
    return Value::makeBool(false);
  }
  
  clipboardMgrInstance->hide();
  return Value::makeBool(true);
}

// clipboardMgr.toggle() -> bool - Toggle visibility
static Value clipboardMgrToggle(VMApi &api, const std::vector<Value> &args) {
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  mgr->toggleVisibility();
  clipboardMgrEnabled = true;  // Enabling on toggle
  return Value::makeBool(true);
}

// clipboardMgr.addToHistory(text) -> bool - Add text to clipboard history
static Value clipboardMgrAddToHistory(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return Value::makeBool(false);
  }
  
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  std::string text;
  if (args[0].isStringValId()) {
    // TODO: Get actual string from string pool
    text = "<string>";
  } else if (args[0].isInt()) {
    text = std::to_string(args[0].asInt());
  } else if (args[0].isDouble()) {
    text = std::to_string(args[0].asDouble());
  } else if (args[0].isBool()) {
    text = args[0].asBool() ? "true" : "false";
  }
  
  mgr->addToHistoryPublic(QString::fromStdString(text));
  return Value::makeBool(true);
}

// clipboardMgr.clearHistory() -> bool - Clear clipboard history
static Value clipboardMgrClearHistory(VMApi &api, const std::vector<Value> &args) {
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  mgr->clearHistoryPublic();
  return Value::makeBool(true);
}

// clipboardMgr.getHistoryItem(index) -> string - Get history item at index
static Value clipboardMgrGetHistoryItem(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return api.makeNull();
  }
  
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return api.makeNull();
  }
  
  int index = 0;
  if (args[0].isInt()) {
    index = static_cast<int>(args[0].asInt());
  } else if (args[0].isDouble()) {
    index = static_cast<int>(args[0].asDouble());
  }
  
  QString item = mgr->getHistoryItem(index);
  // TODO: Return actual string when string pool is available
  return api.makeNull();  // Placeholder
}

// clipboardMgr.getHistoryCount() -> int - Get number of history items
static Value clipboardMgrGetHistoryCount(VMApi &api, const std::vector<Value> &args) {
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeInt(0);
  }
  
  return Value::makeInt(mgr->getHistoryCount());
}

// clipboardMgr.pasteItem(index) -> bool - Paste history item at index
static Value clipboardMgrPasteItem(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 1) {
    return Value::makeBool(false);
  }
  
  auto* mgr = getClipboardManager(api);
  if (!mgr) {
    return Value::makeBool(false);
  }
  
  int index = 0;
  if (args[0].isInt()) {
    index = static_cast<int>(args[0].asInt());
  } else if (args[0].isDouble()) {
    index = static_cast<int>(args[0].asDouble());
  }
  
  mgr->pasteHistoryItem(index);
  return Value::makeBool(true);
}

// ============================================================================
// Register Clipboard Manager Module
// ============================================================================

void registerClipboardMgrModule(compiler::VMApi &api) {
  // Enable/disable functions
  api.registerFunction("clipboardMgr.enable",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrEnable(api, args);
                       });
  
  api.registerFunction("clipboardMgr.disable",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrDisable(api, args);
                       });
  
  api.registerFunction("clipboardMgr.isEnabled",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrIsEnabled(api, args);
                       });
  
  // Window visibility functions
  api.registerFunction("clipboardMgr.show",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrShow(api, args);
                       });
  
  api.registerFunction("clipboardMgr.hide",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrHide(api, args);
                       });
  
  api.registerFunction("clipboardMgr.toggle",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrToggle(api, args);
                       });
  
  // History functions
  api.registerFunction("clipboardMgr.addToHistory",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrAddToHistory(api, args);
                       });
  
  api.registerFunction("clipboardMgr.clearHistory",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrClearHistory(api, args);
                       });
  
  api.registerFunction("clipboardMgr.getHistoryItem",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrGetHistoryItem(api, args);
                       });
  
  api.registerFunction("clipboardMgr.getHistoryCount",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrGetHistoryCount(api, args);
                       });
  
  api.registerFunction("clipboardMgr.pasteItem",
                       [&api](const std::vector<Value> &args) {
                         return clipboardMgrPasteItem(api, args);
                       });
  
  // Register global 'clipboardMgr' object (disabled by default)
  auto clipboardMgrObj = api.makeObject();
  
  api.setField(clipboardMgrObj, "enable", api.makeFunctionRef("clipboardMgr.enable"));
  api.setField(clipboardMgrObj, "disable", api.makeFunctionRef("clipboardMgr.disable"));
  api.setField(clipboardMgrObj, "isEnabled", api.makeFunctionRef("clipboardMgr.isEnabled"));
  api.setField(clipboardMgrObj, "show", api.makeFunctionRef("clipboardMgr.show"));
  api.setField(clipboardMgrObj, "hide", api.makeFunctionRef("clipboardMgr.hide"));
  api.setField(clipboardMgrObj, "toggle", api.makeFunctionRef("clipboardMgr.toggle"));
  api.setField(clipboardMgrObj, "addToHistory", api.makeFunctionRef("clipboardMgr.addToHistory"));
  api.setField(clipboardMgrObj, "clearHistory", api.makeFunctionRef("clipboardMgr.clearHistory"));
  api.setField(clipboardMgrObj, "getHistoryItem", api.makeFunctionRef("clipboardMgr.getHistoryItem"));
  api.setField(clipboardMgrObj, "getHistoryCount", api.makeFunctionRef("clipboardMgr.getHistoryCount"));
  api.setField(clipboardMgrObj, "pasteItem", api.makeFunctionRef("clipboardMgr.pasteItem"));
  
  api.setGlobal("clipboardMgr", clipboardMgrObj);
}

} // namespace havel::modules
