/*
 * UIModule.cpp - UI module implementation for Havel bytecode VM
 */
#include "UIModule.hpp"
#include "UIElement.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "host/ui/UIManager.hpp"

#include <unordered_map>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static host::UIBackend *getUIBackend() {
  return host::UIManager::instance().backend();
}

static std::unordered_map<ui::ElementId, std::shared_ptr<ui::UIElement>>
    &elementRegistry() {
  static std::unordered_map<ui::ElementId, std::shared_ptr<ui::UIElement>>
      registry;
  return registry;
}

static void registerElement(std::shared_ptr<ui::UIElement> elem) {
  if (elem) {
    elementRegistry()[elem->id] = elem;
  }
}

static std::shared_ptr<ui::UIElement>
getElementFromObject(VMApi &api, const Value &obj) {
  if (!obj.isObjectId())
    return nullptr;
  auto idVal = api.getField(obj, "__element");
  if (!idVal.isInt())
    return nullptr;
  auto it = elementRegistry().find(static_cast<ui::ElementId>(idVal.asInt()));
  return it != elementRegistry().end() ? it->second : nullptr;
}

static int toInt(const Value &v) {
  if (v.isInt())
    return static_cast<int>(v.asInt());
  if (v.isDouble())
    return static_cast<int>(v.asDouble());
  return 0;
}

static bool toBool(const Value &v) {
  if (v.isBool())
    return v.asBool();
  if (v.isInt())
    return v.asInt() != 0;
  if (v.isDouble())
    return v.asDouble() != 0;
  return false;
}

static std::string getStringArg(VMApi &api,
                                const std::vector<Value> &args,
                                size_t index,
                                const std::string &defaultVal = "") {
  if (args.size() <= index)
    return defaultVal;
  return api.toString(args[index]);
}

static int getIntArg(const std::vector<Value> &args, size_t index,
                     int defaultVal = 0) {
  if (args.size() <= index)
    return defaultVal;
  return toInt(args[index]);
}

static void attachElementToObject(VMApi &api, Value obj,
                                  std::shared_ptr<ui::UIElement> element) {
  registerElement(element);
  api.setField(obj, "__element",
               Value::makeInt(static_cast<int64_t>(element->id)));
  api.setField(obj, "on", api.makeFunctionRef("ui.element.on"));
  api.setField(obj, "style", api.makeFunctionRef("ui.element.style"));
}

// ============================================================================
// UI Creation Functions
// ============================================================================

static Value uiWindow(VMApi &api, const std::vector<Value> &args) {
  std::string title = getStringArg(api, args, 0, "Window");
  auto elem = getUIBackend()->window(title);

  if (args.size() > 1 && args[1].isObjectId()) {
    auto widthVal = api.getField(args[1], "width");
    if (!widthVal.isNull())
      elem->set("width", static_cast<int64_t>(toInt(widthVal)));
    auto heightVal = api.getField(args[1], "height");
    if (!heightVal.isNull())
      elem->set("height", static_cast<int64_t>(toInt(heightVal)));
    auto resizeVal = api.getField(args[1], "resizable");
    if (!resizeVal.isNull())
      elem->set("resizable", toBool(resizeVal));
  }

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  api.setField(obj, "show", api.makeFunctionRef("ui.element.show"));
  api.setField(obj, "hide", api.makeFunctionRef("ui.element.hide"));
  api.setField(obj, "close", api.makeFunctionRef("ui.element.close"));
  api.setField(obj, "setTitle", api.makeFunctionRef("ui.element.setTitle"));
  api.setField(obj, "setSize", api.makeFunctionRef("ui.element.setSize"));
  api.setField(obj, "setPos", api.makeFunctionRef("ui.element.setPos"));

  return obj;
}

static Value uiBtn(VMApi &api, const std::vector<Value> &args) {
  std::string label = getStringArg(api, args, 0, "Button");
  auto elem = getUIBackend()->btn(label);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiText(VMApi &api, const std::vector<Value> &args) {
  std::string content = getStringArg(api, args, 0, "");
  auto elem = getUIBackend()->text(content);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiLabel(VMApi &api, const std::vector<Value> &args) {
  std::string content = getStringArg(api, args, 0, "");
  auto elem = getUIBackend()->label(content);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiInput(VMApi &api, const std::vector<Value> &args) {
  std::string placeholder = getStringArg(api, args, 0, "");
  auto elem = getUIBackend()->input(placeholder);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "value", api.makeFunctionRef("ui.input.value"));
  return obj;
}

static Value uiTextarea(VMApi &api, const std::vector<Value> &args) {
  std::string placeholder = getStringArg(api, args, 0, "");
  auto elem = getUIBackend()->textarea(placeholder);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "value", api.makeFunctionRef("ui.textarea.value"));
  return obj;
}

static Value uiCheckbox(VMApi &api, const std::vector<Value> &args) {
  std::string label = getStringArg(api, args, 0, "");
  bool checked = args.size() > 1 ? toBool(args[1]) : false;
  auto elem = getUIBackend()->checkbox(label, checked);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiSlider(VMApi &api, const std::vector<Value> &args) {
  int min = getIntArg(args, 0, 0);
  int max = getIntArg(args, 1, 100);
  int value = getIntArg(args, 2, 0);
  auto elem = getUIBackend()->slider(min, max, value);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiDropdown(VMApi &api, const std::vector<Value> &args) {
  std::vector<std::string> options;
  if (args.size() > 0 && args[0].isArrayId()) {
    size_t len = api.length(args[0]);
    for (size_t i = 0; i < len; i++) {
      options.push_back(api.toString(api.getAt(args[0], i)));
    }
  }
  auto elem = getUIBackend()->dropdown(options);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiImage(VMApi &api, const std::vector<Value> &args) {
  std::string path = getStringArg(api, args, 0, "");
  auto elem = getUIBackend()->image(path);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiDivider(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto elem = getUIBackend()->divider();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiSpacer(VMApi &api, const std::vector<Value> &args) {
  int size = getIntArg(args, 0, 10);
  auto elem = getUIBackend()->spacer(size);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiProgress(VMApi &api, const std::vector<Value> &args) {
  int value = getIntArg(args, 0, 0);
  int max = getIntArg(args, 1, 100);
  auto elem = getUIBackend()->progress(value, max);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiSpinner(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto elem = getUIBackend()->spinner();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

// ============================================================================
// Menu Elements
// ============================================================================

static Value uiMenu(VMApi &api, const std::vector<Value> &args) {
  std::string title = getStringArg(api, args, 0, "Menu");
  auto elem = getUIBackend()->menu(title);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiMenuItem(VMApi &api, const std::vector<Value> &args) {
  std::string label = getStringArg(api, args, 0, "Item");
  std::string shortcut = getStringArg(api, args, 1, "");
  auto elem = getUIBackend()->menuItem(label, shortcut);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiMenuSeparator(VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto elem = getUIBackend()->menuSeparator();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

// ============================================================================
// Layout Containers
// ============================================================================

static Value uiRow(VMApi &api, const std::vector<Value> &args) {
  auto elem = getUIBackend()->row();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiCol(VMApi &api, const std::vector<Value> &args) {
  auto elem = getUIBackend()->col();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiGrid(VMApi &api, const std::vector<Value> &args) {
  int cols = getIntArg(args, 0, 2);
  auto elem = getUIBackend()->grid(cols);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiTable(VMApi &api, const std::vector<Value> &args) {
  int rows = getIntArg(args, 0, 3);
  int cols = getIntArg(args, 1, 2);
  auto elem = getUIBackend()->table(rows, cols);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

static Value uiFlex(VMApi &api, const std::vector<Value> &args) {
  std::string direction = "row";
  if (args.size() > 0 && !args[0].isArrayId()) {
    direction = getStringArg(api, args, 0, "row");
  }
  auto elem = getUIBackend()->flex(direction);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiScroll(VMApi &api, const std::vector<Value> &args) {
  auto elem = getUIBackend()->scroll();
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  return obj;
}

static Value uiCanvas(VMApi &api, const std::vector<Value> &args) {
  int width = getIntArg(args, 0, 800);
  int height = getIntArg(args, 1, 600);
  auto elem = getUIBackend()->canvas(width, height);
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);
  return obj;
}

// ============================================================================
// Element Methods
// ============================================================================

static Value uiElementAdd(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 2)
    return Value::makeNull();

  auto parent = getElementFromObject(api, args[0]);
  auto child = getElementFromObject(api, args[1]);
  if (parent && child) {
    parent->add(child);
  }

  return args[0];
}

static Value uiElementShow(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    auto *backend = getUIBackend();
    if (backend) {
      backend->realize(elem);
      backend->show(elem);
    }
  }

  return args[0];
}

static Value uiElementHide(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    auto *backend = getUIBackend();
    if (backend) {
      backend->hide(elem);
    }
  }

  return args[0];
}

static Value uiElementClose(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    auto *backend = getUIBackend();
    if (backend) {
      backend->close(elem);
    }
    elementRegistry().erase(elem->id);
  }

  return args[0];
}

static Value uiElementSetTitle(VMApi &api,
                               const std::vector<Value> &args) {
  if (args.size() < 2)
    return args.empty() ? Value::makeNull() : args[0];

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    elem->set("title", getStringArg(api, args, 1, ""));
  }

  return args[0];
}

static Value uiElementSetSize(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 3)
    return args[0];

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    elem->set("width", static_cast<int64_t>(toInt(args[1])));
    elem->set("height", static_cast<int64_t>(toInt(args[2])));
  }

  return args[0];
}

static Value uiElementSetPos(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 3)
    return args[0];

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    elem->set("x", static_cast<int64_t>(toInt(args[1])));
    elem->set("y", static_cast<int64_t>(toInt(args[2])));
  }

  return args[0];
}

// ============================================================================
// Generic event handler: elem.on("click", callback)
// ============================================================================

static Value uiElementOn(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 3)
    return args.empty() ? Value::makeNull() : args[0];

  auto elem = getElementFromObject(api, args[0]);
  std::string event = getStringArg(api, args, 1, "");

  if (elem && !event.empty()) {
    auto cbId = api.registerCallback(args[2]);
    elem->on(event, [&api, cbId]() {
      api.invokeCallback(cbId);
      api.releaseCallback(cbId);
    });
  }

  return args[0];
}

// ============================================================================
// Styling
// ============================================================================

static Value uiElementStyle(VMApi &api, const std::vector<Value> &args) {
  if (args.size() < 3)
    return args.empty() ? Value::makeNull() : args[0];

  auto elem = getElementFromObject(api, args[0]);
  if (elem) {
    std::string key = getStringArg(api, args, 1, "");
    std::string value = getStringArg(api, args, 2, "");
    auto *backend = getUIBackend();
    if (backend) {
      backend->applyStyle(elem, key, ui::PropValue{value});
    }
  }

  return args[0];
}

// ============================================================================
// Input value getters/setters
// ============================================================================

static Value uiInputValue(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();

  auto elem = getElementFromObject(api, args[0]);
  if (!elem)
    return Value::makeNull();

  auto *backend = getUIBackend();
  if (!backend)
    return Value::makeNull();

  if (args.size() == 1) {
    return api.makeString(backend->getValue(elem));
  } else {
    backend->setValue(elem, getStringArg(api, args, 1, ""));
    return args[0];
  }
}

static Value uiTextareaValue(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeNull();

  auto elem = getElementFromObject(api, args[0]);
  if (!elem)
    return Value::makeNull();

  auto *backend = getUIBackend();
  if (!backend)
    return Value::makeNull();

  if (args.size() == 1) {
    return api.makeString(backend->getValue(elem));
  } else {
    backend->setValue(elem, getStringArg(api, args, 1, ""));
    return args[0];
  }
}

// ============================================================================
// Dialogs
// ============================================================================

static Value uiAlert(VMApi &api, const std::vector<Value> &args) {
  std::string message = getStringArg(api, args, 0, "");
  getUIBackend()->alert(message);
  return Value::makeBool(true);
}

static Value uiConfirm(VMApi &api, const std::vector<Value> &args) {
  std::string message = getStringArg(api, args, 0, "");
  bool result = getUIBackend()->confirm(message);
  return Value::makeBool(result);
}

static Value uiFilePicker(VMApi &api, const std::vector<Value> &args) {
  std::string title = getStringArg(api, args, 0, "Select file");
  std::string result = getUIBackend()->filePicker(title);
  return api.makeString(result);
}

static Value uiDirPicker(VMApi &api, const std::vector<Value> &args) {
  std::string title = getStringArg(api, args, 0, "Select directory");
  std::string result = getUIBackend()->dirPicker(title);
  return api.makeString(result);
}

static Value uiNotify(VMApi &api, const std::vector<Value> &args) {
  std::string message = getStringArg(api, args, 0, "");
  std::string type = getStringArg(api, args, 1, "info");
  getUIBackend()->notify(message, type);
  return Value::makeBool(true);
}

// ============================================================================
// System Tray
// ============================================================================

static Value uiTrayIcon(VMApi &api, const std::vector<Value> &args) {
  std::string iconPath = getStringArg(api, args, 0, "");
  std::string tooltip = getStringArg(api, args, 1, "");
  getUIBackend()->trayIcon(iconPath, tooltip);
  return Value::makeBool(true);
}

static Value uiTrayShow(const std::vector<Value> &args) {
  (void)args;
  getUIBackend()->trayShow();
  return Value::makeBool(true);
}

static Value uiTrayHide(const std::vector<Value> &args) {
  (void)args;
  getUIBackend()->trayHide();
  return Value::makeBool(true);
}

static Value uiTrayMenu(VMApi &api, const std::vector<Value> &args) {
  auto elem = args.empty() ? nullptr : getElementFromObject(api, args[0]);
  getUIBackend()->trayMenu(elem);
  return Value::makeBool(true);
}

static Value uiTrayNotify(VMApi &api, const std::vector<Value> &args) {
  std::string title = getStringArg(api, args, 0, "Notification");
  std::string message = getStringArg(api, args, 1, "");
  std::string iconType = getStringArg(api, args, 2, "info");
  getUIBackend()->trayNotify(title, message, iconType);
  return Value::makeBool(true);
}

// ============================================================================
// API Selection
// ============================================================================

static Value uiSetApi(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeBool(false);
  std::string apiName = getStringArg(api, args, 0, "");
  bool success = host::UIManager::instance().setBackend(apiName);
  return Value::makeBool(success);
}

static Value uiGetApi(VMApi &api) {
  return api.makeString(host::UIManager::instance().currentApiName());
}

static Value uiIsAvailable(VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return Value::makeBool(false);
  std::string apiName = getStringArg(api, args, 0, "");
  bool available = host::UIManager::instance().isBackendAvailable(apiName);
  return Value::makeBool(available);
}

// ============================================================================
// Register UI Module
// ============================================================================

void registerUIModule(compiler::VMApi &api) {
  // Element creation
  api.registerFunction("ui.window",
                       [&api](const std::vector<Value> &args) {
                         return uiWindow(api, args);
                       });

  api.registerFunction("ui.btn",
                       [&api](const std::vector<Value> &args) {
                         return uiBtn(api, args);
                       });

  api.registerFunction("ui.text",
                       [&api](const std::vector<Value> &args) {
                         return uiText(api, args);
                       });

  api.registerFunction("ui.label",
                       [&api](const std::vector<Value> &args) {
                         return uiLabel(api, args);
                       });

  api.registerFunction("ui.input",
                       [&api](const std::vector<Value> &args) {
                         return uiInput(api, args);
                       });

  api.registerFunction("ui.textarea",
                       [&api](const std::vector<Value> &args) {
                         return uiTextarea(api, args);
                       });

  api.registerFunction("ui.checkbox",
                       [&api](const std::vector<Value> &args) {
                         return uiCheckbox(api, args);
                       });

  api.registerFunction("ui.slider",
                       [&api](const std::vector<Value> &args) {
                         return uiSlider(api, args);
                       });

  api.registerFunction("ui.dropdown",
                       [&api](const std::vector<Value> &args) {
                         return uiDropdown(api, args);
                       });

  api.registerFunction("ui.image",
                       [&api](const std::vector<Value> &args) {
                         return uiImage(api, args);
                       });

  api.registerFunction("ui.divider",
                       [&api](const std::vector<Value> &args) {
                         return uiDivider(api, args);
                       });

  api.registerFunction("ui.spacer",
                       [&api](const std::vector<Value> &args) {
                         return uiSpacer(api, args);
                       });

  api.registerFunction("ui.progress",
                       [&api](const std::vector<Value> &args) {
                         return uiProgress(api, args);
                       });

  api.registerFunction("ui.spinner",
                       [&api](const std::vector<Value> &args) {
                         return uiSpinner(api, args);
                       });

  // Menu elements
  api.registerFunction("ui.menu",
                       [&api](const std::vector<Value> &args) {
                         return uiMenu(api, args);
                       });

  api.registerFunction("ui.menuItem",
                       [&api](const std::vector<Value> &args) {
                         return uiMenuItem(api, args);
                       });

  api.registerFunction("ui.menuSeparator",
                       [&api](const std::vector<Value> &args) {
                         return uiMenuSeparator(api, args);
                       });

  // Layout containers
  api.registerFunction("ui.row",
                       [&api](const std::vector<Value> &args) {
                         return uiRow(api, args);
                       });

  api.registerFunction("ui.col",
                       [&api](const std::vector<Value> &args) {
                         return uiCol(api, args);
                       });

  api.registerFunction("ui.grid",
                       [&api](const std::vector<Value> &args) {
                         return uiGrid(api, args);
                       });

  api.registerFunction("ui.table",
                       [&api](const std::vector<Value> &args) {
                         return uiTable(api, args);
                       });

  api.registerFunction("ui.flex",
                       [&api](const std::vector<Value> &args) {
                         return uiFlex(api, args);
                       });

  api.registerFunction("ui.scroll",
                       [&api](const std::vector<Value> &args) {
                         return uiScroll(api, args);
                       });

  api.registerFunction("ui.canvas",
                       [&api](const std::vector<Value> &args) {
                         return uiCanvas(api, args);
                       });

  // Element methods
  api.registerFunction("ui.element.add",
                       [&api](const std::vector<Value> &args) {
                         return uiElementAdd(api, args);
                       });

  api.registerFunction("ui.element.show",
                       [&api](const std::vector<Value> &args) {
                         return uiElementShow(api, args);
                       });

  api.registerFunction("ui.element.hide",
                       [&api](const std::vector<Value> &args) {
                         return uiElementHide(api, args);
                       });

  api.registerFunction("ui.element.close",
                       [&api](const std::vector<Value> &args) {
                         return uiElementClose(api, args);
                       });

  api.registerFunction("ui.element.setTitle",
                       [&api](const std::vector<Value> &args) {
                         return uiElementSetTitle(api, args);
                       });

  api.registerFunction("ui.element.setSize",
                       [&api](const std::vector<Value> &args) {
                         return uiElementSetSize(api, args);
                       });

  api.registerFunction("ui.element.setPos",
                       [&api](const std::vector<Value> &args) {
                         return uiElementSetPos(api, args);
                       });

  // Generic event handler
  api.registerFunction("ui.element.on",
                       [&api](const std::vector<Value> &args) {
                         return uiElementOn(api, args);
                       });

  // Styling
  api.registerFunction("ui.element.style",
                       [&api](const std::vector<Value> &args) {
                         return uiElementStyle(api, args);
                       });

  // Input value
  api.registerFunction("ui.input.value",
                       [&api](const std::vector<Value> &args) {
                         return uiInputValue(api, args);
                       });

  api.registerFunction("ui.textarea.value",
                       [&api](const std::vector<Value> &args) {
                         return uiTextareaValue(api, args);
                       });

  // Dialogs
  api.registerFunction("ui.alert",
                       [&api](const std::vector<Value> &args) {
                         return uiAlert(api, args);
                       });

  api.registerFunction("ui.confirm",
                       [&api](const std::vector<Value> &args) {
                         return uiConfirm(api, args);
                       });

  api.registerFunction("ui.filePicker",
                       [&api](const std::vector<Value> &args) {
                         return uiFilePicker(api, args);
                       });

  api.registerFunction("ui.dirPicker",
                       [&api](const std::vector<Value> &args) {
                         return uiDirPicker(api, args);
                       });

  api.registerFunction("ui.notify",
                       [&api](const std::vector<Value> &args) {
                         return uiNotify(api, args);
                       });

  // System Tray
  api.registerFunction("ui.trayIcon",
                       [&api](const std::vector<Value> &args) {
                         return uiTrayIcon(api, args);
                       });

  api.registerFunction("ui.trayShow",
                       [](const std::vector<Value> &args) {
                         return uiTrayShow(args);
                       });

  api.registerFunction("ui.trayHide",
                       [](const std::vector<Value> &args) {
                         return uiTrayHide(args);
                       });

  api.registerFunction("ui.trayMenu",
                       [&api](const std::vector<Value> &args) {
                         return uiTrayMenu(api, args);
                       });

  api.registerFunction("ui.trayNotify",
                       [&api](const std::vector<Value> &args) {
                         return uiTrayNotify(api, args);
                       });

  // API selection
  api.registerFunction("ui.setApi",
                       [&api](const std::vector<Value> &args) {
                         return uiSetApi(api, args);
                       });

  api.registerFunction("ui.getApi",
                       [&api](const std::vector<Value> &args) {
                         (void)args;
                         return uiGetApi(api);
                       });

  api.registerFunction("ui.isAvailable",
                       [&api](const std::vector<Value> &args) {
                         return uiIsAvailable(api, args);
                       });

  // Register global 'ui' object
  auto uiObj = api.makeObject();

  api.setField(uiObj, "window", api.makeFunctionRef("ui.window"));
  api.setField(uiObj, "btn", api.makeFunctionRef("ui.btn"));
  api.setField(uiObj, "text", api.makeFunctionRef("ui.text"));
  api.setField(uiObj, "label", api.makeFunctionRef("ui.label"));
  api.setField(uiObj, "input", api.makeFunctionRef("ui.input"));
  api.setField(uiObj, "textarea", api.makeFunctionRef("ui.textarea"));
  api.setField(uiObj, "checkbox", api.makeFunctionRef("ui.checkbox"));
  api.setField(uiObj, "slider", api.makeFunctionRef("ui.slider"));
  api.setField(uiObj, "dropdown", api.makeFunctionRef("ui.dropdown"));
  api.setField(uiObj, "image", api.makeFunctionRef("ui.image"));
  api.setField(uiObj, "divider", api.makeFunctionRef("ui.divider"));
  api.setField(uiObj, "spacer", api.makeFunctionRef("ui.spacer"));
  api.setField(uiObj, "progress", api.makeFunctionRef("ui.progress"));
  api.setField(uiObj, "spinner", api.makeFunctionRef("ui.spinner"));
  api.setField(uiObj, "menu", api.makeFunctionRef("ui.menu"));
  api.setField(uiObj, "menuItem", api.makeFunctionRef("ui.menuItem"));
  api.setField(uiObj, "menuSeparator", api.makeFunctionRef("ui.menuSeparator"));
  api.setField(uiObj, "row", api.makeFunctionRef("ui.row"));
  api.setField(uiObj, "col", api.makeFunctionRef("ui.col"));
  api.setField(uiObj, "grid", api.makeFunctionRef("ui.grid"));
  api.setField(uiObj, "scroll", api.makeFunctionRef("ui.scroll"));
  api.setField(uiObj, "canvas", api.makeFunctionRef("ui.canvas"));
  api.setField(uiObj, "alert", api.makeFunctionRef("ui.alert"));
  api.setField(uiObj, "confirm", api.makeFunctionRef("ui.confirm"));
  api.setField(uiObj, "filePicker", api.makeFunctionRef("ui.filePicker"));
  api.setField(uiObj, "dirPicker", api.makeFunctionRef("ui.dirPicker"));
  api.setField(uiObj, "notify", api.makeFunctionRef("ui.notify"));
  api.setField(uiObj, "trayIcon", api.makeFunctionRef("ui.trayIcon"));
  api.setField(uiObj, "trayShow", api.makeFunctionRef("ui.trayShow"));
  api.setField(uiObj, "trayHide", api.makeFunctionRef("ui.trayHide"));
  api.setField(uiObj, "trayMenu", api.makeFunctionRef("ui.trayMenu"));
  api.setField(uiObj, "trayNotify", api.makeFunctionRef("ui.trayNotify"));
  api.setField(uiObj, "setApi", api.makeFunctionRef("ui.setApi"));
  api.setField(uiObj, "getApi", api.makeFunctionRef("ui.getApi"));
  api.setField(uiObj, "isAvailable", api.makeFunctionRef("ui.isAvailable"));

  api.setGlobal("ui", uiObj);
}

} // namespace havel::modules
