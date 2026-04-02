/*
 * UIModule.cpp - UI module implementation for Havel bytecode VM
 */
#include "UIModule.hpp"
#include "UIElement.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "host/ui/UIManager.hpp"

#include <cmath>
#include <sstream>

namespace havel::modules {

using compiler::ArrayRef;
using compiler::BytecodeValue;
using compiler::ObjectRef;
using compiler::VMApi;

// Global UI backend instance (via UIManager singleton)
static host::UIBackend *getUIBackend() {
  return host::UIManager::instance().backend();
}

// Helper to convert BytecodeValue to string
static std::string toString(const BytecodeValue &v) {
  if (std::holds_alternative<std::string>(v))
    return std::get<std::string>(v);
  if (std::holds_alternative<int64_t>(v))
    return std::to_string(std::get<int64_t>(v));
  if (std::holds_alternative<double>(v)) {
    double val = std::get<double>(v);
    if (val == std::floor(val) && std::abs(val) < 1e15) {
      return std::to_string(static_cast<long long>(val));
    }
    std::ostringstream oss;
    oss.precision(15);
    oss << val;
    return oss.str();
  }
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? "true" : "false";
  return "";
}

// Helper to convert BytecodeValue to int
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

static bool toBool(const BytecodeValue &v) {
  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v);
  if (std::holds_alternative<int64_t>(v))
    return std::get<int64_t>(v) != 0;
  if (std::holds_alternative<double>(v))
    return std::get<double>(v) != 0;
  if (std::holds_alternative<std::string>(v)) {
    const auto &s = std::get<std::string>(v);
    return !s.empty() && s != "false" && s != "0";
  }
  return false;
}

// Extract string from options object or args
static std::string getStringArg(VMApi &api,
                                const std::vector<BytecodeValue> &args,
                                size_t index,
                                const std::string &defaultVal = "") {
  if (args.size() <= index)
    return defaultVal;

  const auto &arg = args[index];
  if (std::holds_alternative<std::string>(arg)) {
    return std::get<std::string>(arg);
  }
  if (std::holds_alternative<ObjectRef>(arg)) {
    // Try to get from object field
    auto val = api.getField(arg, "text");
    if (!std::holds_alternative<std::nullptr_t>(val)) {
      return toString(val);
    }
  }
  return defaultVal;
}

static int getIntArg(const std::vector<BytecodeValue> &args, size_t index,
                     int defaultVal = 0) {
  if (args.size() <= index)
    return defaultVal;
  return toInt(args[index]);
}

// Helper to store UIElement reference in BytecodeValue object
// We use ObjectRef with special fields to track the element
static void attachElementToObject(VMApi &api, BytecodeValue obj,
                                  std::shared_ptr<ui::UIElement> element) {
  // Store element ID as special field
  api.setField(obj, "__ui_type", BytecodeValue(element->type));
  api.setField(obj, "__ui_id",
               BytecodeValue(static_cast<int64_t>(element->id)));
}

static std::shared_ptr<ui::UIElement>
getElementFromObject(VMApi &api, const BytecodeValue &obj) {
  if (!std::holds_alternative<ObjectRef>(obj))
    return nullptr;

  // Get element ID from object
  auto idVal = api.getField(obj, "__ui_id");
  if (!std::holds_alternative<int64_t>(idVal))
    return nullptr;

  ui::ElementId id = static_cast<ui::ElementId>(std::get<int64_t>(idVal));

  // TODO: Keep a registry of elements by ID in UIService
  // For now, we can't easily retrieve elements, so we'll store a pointer in the
  // object

  return nullptr;
}

// ============================================================================
// UI Creation Functions
// ============================================================================

// ui.window(title, options...)
static BytecodeValue uiWindow(VMApi &api,
                              const std::vector<BytecodeValue> &args) {
  std::string title = getStringArg(api, args, 0, "Window");

  auto elem = getUIBackend()->window(title);

  // Parse named args from options object if provided
  if (args.size() > 1) {
    const auto &opts = args[1];
    if (std::holds_alternative<ObjectRef>(opts)) {
      auto widthVal = api.getField(opts, "width");
      if (!std::holds_alternative<std::nullptr_t>(widthVal)) {
        elem->set("width", static_cast<int64_t>(toInt(widthVal)));
      }

      auto heightVal = api.getField(opts, "height");
      if (!std::holds_alternative<std::nullptr_t>(heightVal)) {
        elem->set("height", static_cast<int64_t>(toInt(heightVal)));
      }

      auto resizeVal = api.getField(opts, "resizable");
      if (!std::holds_alternative<std::nullptr_t>(resizeVal)) {
        elem->set("resizable", toBool(resizeVal));
      }
    }
  }

  // Create object to return
  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  // Store element pointer (we use a global registry for now)
  // In production, UIService should maintain an element registry

  // Add methods to the object
  api.setField(obj, "add", api.makeFunctionRef("ui.element.add"));
  api.setField(obj, "show", api.makeFunctionRef("ui.element.show"));
  api.setField(obj, "hide", api.makeFunctionRef("ui.element.hide"));
  api.setField(obj, "close", api.makeFunctionRef("ui.element.close"));
  api.setField(obj, "onClose", api.makeFunctionRef("ui.element.onClose"));
  api.setField(obj, "onResize", api.makeFunctionRef("ui.element.onResize"));
  api.setField(obj, "onMove", api.makeFunctionRef("ui.element.onMove"));
  api.setField(obj, "status", api.makeFunctionRef("ui.window.status"));
  api.setField(obj, "panel", api.makeFunctionRef("ui.window.panel"));
  api.setField(obj, "menu", api.makeFunctionRef("ui.window.menu"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.btn(label, callback)
static BytecodeValue uiBtn(VMApi &api, const std::vector<BytecodeValue> &args) {
  std::string label = getStringArg(api, args, 0, "Button");

  auto elem = getUIBackend()->btn(label);

  // Parse callback if provided
  if (args.size() > 1) {
    // Callback could be a function reference or closure
    // Store it for later invocation
    // TODO: Implement callback registration
  }

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onClick", api.makeFunctionRef("ui.element.onClick"));
  api.setField(obj, "alignRight", api.makeFunctionRef("ui.element.alignRight"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.text(content)
static BytecodeValue uiText(VMApi &api,
                            const std::vector<BytecodeValue> &args) {
  std::string content = getStringArg(api, args, 0, "");

  auto elem = getUIBackend()->text(content);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "bold", api.makeFunctionRef("ui.element.bold"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.label(content)
static BytecodeValue uiLabel(VMApi &api,
                             const std::vector<BytecodeValue> &args) {
  std::string content = getStringArg(api, args, 0, "");

  auto elem = getUIBackend()->label(content);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ui.input(placeholder, options)
static BytecodeValue uiInput(VMApi &api,
                             const std::vector<BytecodeValue> &args) {
  std::string placeholder = getStringArg(api, args, 0, "");

  auto elem = getUIBackend()->input(placeholder);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onChange", api.makeFunctionRef("ui.element.onChange"));
  api.setField(obj, "value", api.makeFunctionRef("ui.input.value"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.textarea(placeholder, options)
static BytecodeValue uiTextarea(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  std::string placeholder = getStringArg(api, args, 0, "");

  auto elem = getUIBackend()->textarea(placeholder);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onChange", api.makeFunctionRef("ui.element.onChange"));
  api.setField(obj, "value", api.makeFunctionRef("ui.textarea.value"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.checkbox(label, checked)
static BytecodeValue uiCheckbox(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  std::string label = getStringArg(api, args, 0, "");
  bool checked = false;
  if (args.size() > 1) {
    checked = toBool(args[1]);
  }

  auto elem = getUIBackend()->checkbox(label, checked);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onChange", api.makeFunctionRef("ui.element.onChange"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.slider(min, max, value)
static BytecodeValue uiSlider(VMApi &api,
                              const std::vector<BytecodeValue> &args) {
  int min = getIntArg(args, 0, 0);
  int max = getIntArg(args, 1, 100);
  int value = getIntArg(args, 2, 0);

  auto elem = getUIBackend()->slider(min, max, value);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onChange", api.makeFunctionRef("ui.element.onChange"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.dropdown(options)
static BytecodeValue uiDropdown(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  std::vector<std::string> options;

  if (args.size() > 0 && std::holds_alternative<ArrayRef>(args[0])) {
    // Extract from array
    auto arr = std::get<ArrayRef>(args[0]);
    size_t len = api.getArrayLength(args[0]);
    for (size_t i = 0; i < len; i++) {
      auto val = api.getArrayValue(args[0], i);
      options.push_back(toString(val));
    }
  }

  auto elem = getUIBackend()->dropdown(options);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onChange", api.makeFunctionRef("ui.element.onChange"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.image(path)
static BytecodeValue uiImage(VMApi &api,
                             const std::vector<BytecodeValue> &args) {
  std::string path = getStringArg(api, args, 0, "");

  auto elem = getUIBackend()->image(path);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ui.divider()
static BytecodeValue uiDivider(VMApi &api,
                               const std::vector<BytecodeValue> &args) {
  (void)args;
  auto elem = getUIBackend()->divider();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ui.spacer(size)
static BytecodeValue uiSpacer(VMApi &api,
                              const std::vector<BytecodeValue> &args) {
  int size = getIntArg(args, 0, 10);

  auto elem = getUIBackend()->spacer(size);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ui.progress(value, max)
static BytecodeValue uiProgress(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  int value = getIntArg(args, 0, 0);
  int max = getIntArg(args, 1, 100);

  auto elem = getUIBackend()->progress(value, max);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ui.spinner()
static BytecodeValue uiSpinner(VMApi &api,
                               const std::vector<BytecodeValue> &args) {
  (void)args;
  auto elem = getUIBackend()->spinner();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  return obj;
}

// ============================================================================
// Menu Elements
// ============================================================================

// ui.menu(title)
static BytecodeValue uiMenu(VMApi &api,
                            const std::vector<BytecodeValue> &args) {
  std::string title = getStringArg(api, args, 0, "Menu");

  auto elem = getUIBackend()->menu(title);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.menuItem(label, shortcut)
static BytecodeValue uiMenuItem(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  std::string label = getStringArg(api, args, 0, "Item");
  std::string shortcut = getStringArg(api, args, 1, "");

  auto elem = getUIBackend()->menuItem(label, shortcut);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "onClick", api.makeFunctionRef("ui.element.onClick"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.menuSeparator()
static BytecodeValue uiMenuSeparator(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  (void)args;
  auto elem = getUIBackend()->menuSeparator();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ============================================================================
// Layout Containers
// ============================================================================

// ui.row([...])
static BytecodeValue uiRow(VMApi &api, const std::vector<BytecodeValue> &args) {
  auto elem = getUIBackend()->row();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  // Add children from array if provided
  if (args.size() > 0 && std::holds_alternative<ArrayRef>(args[0])) {
    // Store children array reference
    api.setField(obj, "children", args[0]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.col([...])
static BytecodeValue uiCol(VMApi &api, const std::vector<BytecodeValue> &args) {
  auto elem = getUIBackend()->col();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  if (args.size() > 0 && std::holds_alternative<ArrayRef>(args[0])) {
    api.setField(obj, "children", args[0]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.grid(cols, [...])
static BytecodeValue uiGrid(VMApi &api,
                            const std::vector<BytecodeValue> &args) {
  int cols = getIntArg(args, 0, 2);

  auto elem = getUIBackend()->grid(cols);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  if (args.size() > 1 && std::holds_alternative<ArrayRef>(args[1])) {
    api.setField(obj, "children", args[1]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.table(rows, cols, data)
static BytecodeValue uiTable(VMApi &api,
                             const std::vector<BytecodeValue> &args) {
  int rows = getIntArg(args, 0, 3);
  int cols = getIntArg(args, 1, 2);

  auto elem = getUIBackend()->table(rows, cols);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  // Add data from array if provided
  if (args.size() > 2 && std::holds_alternative<ArrayRef>(args[2])) {
    api.setField(obj, "data", args[2]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.flex(direction, [...]) - direction: "row" or "col"
static BytecodeValue uiFlex(VMApi &api,
                            const std::vector<BytecodeValue> &args) {
  std::string direction = "row";
  if (args.size() > 0 && std::holds_alternative<std::string>(args[0])) {
    direction = std::get<std::string>(args[0]);
  }

  auto elem = getUIBackend()->flex(direction);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  // Add children from array if provided (second arg or first if it's an array)
  size_t childrenIdx = 1;
  if (args.size() > 0 && std::holds_alternative<ArrayRef>(args[0])) {
    childrenIdx = 0;
  }
  if (args.size() > childrenIdx && std::holds_alternative<ArrayRef>(args[childrenIdx])) {
    api.setField(obj, "children", args[childrenIdx]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.scroll([...])
static BytecodeValue uiScroll(VMApi &api,
                              const std::vector<BytecodeValue> &args) {
  auto elem = getUIBackend()->scroll();

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  if (args.size() > 0 && std::holds_alternative<ArrayRef>(args[0])) {
    api.setField(obj, "children", args[0]);
  }

  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ui.canvas(width, height)
static BytecodeValue uiCanvas(VMApi &api,
                              const std::vector<BytecodeValue> &args) {
  int width = getIntArg(args, 0, 800);
  int height = getIntArg(args, 1, 600);

  auto elem = getUIBackend()->canvas(width, height);

  auto obj = api.makeObject();
  attachElementToObject(api, obj, elem);

  // Canvas methods
  api.setField(obj, "clear", api.makeFunctionRef("ui.canvas.clear"));
  api.setField(obj, "fill", api.makeFunctionRef("ui.canvas.fill"));
  api.setField(obj, "drawPoint", api.makeFunctionRef("ui.canvas.drawPoint"));
  api.setField(obj, "drawLine", api.makeFunctionRef("ui.canvas.drawLine"));
  api.setField(obj, "drawRect", api.makeFunctionRef("ui.canvas.drawRect"));
  api.setField(obj, "drawCircle", api.makeFunctionRef("ui.canvas.drawCircle"));
  api.setField(obj, "drawImage", api.makeFunctionRef("ui.canvas.drawImage"));
  api.setField(obj, "__element", BytecodeValue(static_cast<int64_t>(elem->id)));

  return obj;
}

// ============================================================================
// Canvas Operations
// ============================================================================

// canvas.clear()
static BytecodeValue uiCanvasClear(VMApi &api,
                                   const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);
  // Clear canvas to background color
  return args[0];
}

// canvas.fill(color)
static BytecodeValue uiCanvasFill(VMApi &api,
                                  const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);
  std::string color = toString(args[1]);
  (void)color;
  // Fill entire canvas with color
  return args[0];
}

// canvas.drawPoint(x, y, color, size)
static BytecodeValue uiCanvasDrawPoint(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 4)
    return BytecodeValue(nullptr);
  int x = toInt(args[1]);
  int y = toInt(args[2]);
  std::string color = toString(args[3]);
  int size = args.size() > 4 ? toInt(args[4]) : 1;
  (void)x; (void)y; (void)color; (void)size;
  return args[0];
}

// canvas.drawLine(x1, y1, x2, y2, color, width)
static BytecodeValue uiCanvasDrawLine(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 6)
    return BytecodeValue(nullptr);
  int x1 = toInt(args[1]);
  int y1 = toInt(args[2]);
  int x2 = toInt(args[3]);
  int y2 = toInt(args[4]);
  std::string color = toString(args[5]);
  int width = args.size() > 6 ? toInt(args[6]) : 1;
  (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)width;
  return args[0];
}

// canvas.drawRect(x, y, w, h, color, fill)
static BytecodeValue uiCanvasDrawRect(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 6)
    return BytecodeValue(nullptr);
  int x = toInt(args[1]);
  int y = toInt(args[2]);
  int w = toInt(args[3]);
  int h = toInt(args[4]);
  std::string color = toString(args[5]);
  bool fill = args.size() > 6 ? toBool(args[6]) : true;
  (void)x; (void)y; (void)w; (void)h; (void)color; (void)fill;
  return args[0];
}

// canvas.drawCircle(x, y, r, color, fill)
static BytecodeValue uiCanvasDrawCircle(VMApi &api,
                                        const std::vector<BytecodeValue> &args) {
  if (args.size() < 5)
    return BytecodeValue(nullptr);
  int x = toInt(args[1]);
  int y = toInt(args[2]);
  int r = toInt(args[3]);
  std::string color = toString(args[4]);
  bool fill = args.size() > 5 ? toBool(args[5]) : true;
  (void)x; (void)y; (void)r; (void)color; (void)fill;
  return args[0];
}

// canvas.drawImage(path, x, y)
static BytecodeValue uiCanvasDrawImage(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 4)
    return BytecodeValue(nullptr);
  std::string path = toString(args[1]);
  int x = toInt(args[2]);
  int y = toInt(args[3]);
  (void)path; (void)x; (void)y;
  return args[0];
}

// ============================================================================
// Styling Operations
// ============================================================================

// element.style(key, value)
static BytecodeValue uiElementStyle(VMApi &api,
                                    const std::vector<BytecodeValue> &args) {
  if (args.size() < 3)
    return BytecodeValue(nullptr);
  std::string key = toString(args[1]);
  std::string value = toString(args[2]);
  (void)key; (void)value;
  return args[0];
}

// element.width(w)
static BytecodeValue uiElementWidth(VMApi &api,
                                    const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);
  int w = toInt(args[1]);
  (void)w;
  return args[0];
}

// element.height(h)
static BytecodeValue uiElementHeight(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);
  int h = toInt(args[1]);
  (void)h;
  return args[0];
}

// element.border(color, width)
static BytecodeValue uiElementBorder(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);
  std::string color = toString(args[1]);
  int width = args.size() > 2 ? toInt(args[2]) : 1;
  (void)color; (void)width;
  return args[0];
}

// ============================================================================
// Dialogs
// ============================================================================

// ui.alert(message)
static BytecodeValue uiAlert(const std::vector<BytecodeValue> &args) {
  std::string message = "";
  if (args.size() > 0) {
    message = toString(args[0]);
  }
  getUIBackend()->alert(message);
  return BytecodeValue(true);
}

// ui.confirm(message)
static BytecodeValue uiConfirm(const std::vector<BytecodeValue> &args) {
  std::string message = "";
  if (args.size() > 0) {
    message = toString(args[0]);
  }
  bool result = getUIBackend()->confirm(message);
  return BytecodeValue(result);
}

// ui.filePicker(title)
static BytecodeValue uiFilePicker(const std::vector<BytecodeValue> &args) {
  std::string title = "Select file";
  if (args.size() > 0) {
    title = toString(args[0]);
  }
  std::string result = getUIBackend()->filePicker(title);
  return BytecodeValue(result);
}

// ui.dirPicker(title)
static BytecodeValue uiDirPicker(const std::vector<BytecodeValue> &args) {
  std::string title = "Select directory";
  if (args.size() > 0) {
    title = toString(args[0]);
  }
  std::string result = getUIBackend()->dirPicker(title);
  return BytecodeValue(result);
}

// ui.notify(message, type)
static BytecodeValue uiNotify(const std::vector<BytecodeValue> &args) {
  std::string message = "";
  std::string type = "info";

  if (args.size() > 0) {
    message = toString(args[0]);
  }
  if (args.size() > 1) {
    type = toString(args[1]);
  }

  getUIBackend()->notify(message, type);
  return BytecodeValue(true);
}

// ============================================================================
// System Tray
// ============================================================================

// ui.trayIcon(iconPath, tooltip)
static BytecodeValue uiTrayIcon(const std::vector<BytecodeValue> &args) {
  std::string iconPath = "";
  std::string tooltip = "";

  if (args.size() > 0) {
    iconPath = toString(args[0]);
  }
  if (args.size() > 1) {
    tooltip = toString(args[1]);
  }

  getUIBackend()->trayIcon(iconPath, tooltip);
  return BytecodeValue(true);
}

// ui.trayShow()
static BytecodeValue uiTrayShow(const std::vector<BytecodeValue> &args) {
  (void)args;
  getUIBackend()->trayShow();
  return BytecodeValue(true);
}

// ui.trayHide()
static BytecodeValue uiTrayHide(const std::vector<BytecodeValue> &args) {
  (void)args;
  getUIBackend()->trayHide();
  return BytecodeValue(true);
}

// ui.trayMenu(menuElement)
static BytecodeValue uiTrayMenu(VMApi &api,
                                const std::vector<BytecodeValue> &args) {
  // Menu element would need to be extracted from args
  // For now, just set up an empty tray menu
  (void)api;
  (void)args;
  getUIBackend()->trayMenu(nullptr);
  return BytecodeValue(true);
}

// ui.trayNotify(title, message, iconType)
static BytecodeValue uiTrayNotify(const std::vector<BytecodeValue> &args) {
  std::string title = "Notification";
  std::string message = "";
  std::string iconType = "info";

  if (args.size() > 0) {
    title = toString(args[0]);
  }
  if (args.size() > 1) {
    message = toString(args[1]);
  }
  if (args.size() > 2) {
    iconType = toString(args[2]);
  }

  getUIBackend()->trayNotify(title, message, iconType);
  return BytecodeValue(true);
}

// ============================================================================
// Element Methods (operate on element objects)
// ============================================================================

// element.add(child)
static BytecodeValue uiElementAdd(VMApi &api,
                                  const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  // This would need proper element registry to implement correctly
  // For now, return the element for chaining
  return args[0];
}

// element.show()
static BytecodeValue uiElementShow(VMApi &api,
                                   const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  // Get element from object
  auto idVal = api.getField(args[0], "__element");
  if (std::holds_alternative<int64_t>(idVal)) {
    ui::ElementId id = static_cast<ui::ElementId>(std::get<int64_t>(idVal));
    // Would need to retrieve element from registry and show
    // getUIBackend()->show(element);
    (void)id;
  }

  return args[0];
}

// element.hide()
static BytecodeValue uiElementHide(VMApi &api,
                                   const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  // Similar to show
  return args[0];
}

// element.close()
static BytecodeValue uiElementClose(VMApi &api,
                                    const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  // Similar to show but close
  return args[0];
}

// element.onClick(callback)
static BytecodeValue uiElementOnClick(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  // Store callback for element
  // TODO: Register callback with element

  return args[0];
}

// element.onChange(callback)
static BytecodeValue uiElementOnChange(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  // Store callback
  return args[0];
}

// Mouse Events
static BytecodeValue uiElementOnMouseDown(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnMouseUp(VMApi &api,
                                        const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDblClick(VMApi &api,
                                         const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnMouseMove(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnMouseOver(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnMouseOut(VMApi &api,
                                         const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDrag(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDragEnter(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDragLeave(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDropEnter(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDropLeave(VMApi &api,
                                          const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnDrop(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnRightClick(VMApi &api,
                                           const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

// Keyboard Events
static BytecodeValue uiElementOnKeyDown(VMApi &api,
                                        const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnKeyUp(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnKeyPress(VMApi &api,
                                         const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

// UI Events
static BytecodeValue uiElementOnFocus(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnBlur(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnLoad(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnUnload(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnScroll(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnCopy(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnCut(VMApi &api,
                                    const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnPaste(VMApi &api,
                                      const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnLoaded(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

static BytecodeValue uiElementOnSubmit(VMApi &api,
                                       const std::vector<BytecodeValue> &args) {
  if (args.size() < 2) return BytecodeValue(nullptr);
  return args[0];
}

// element.pad(n)
static BytecodeValue uiElementPad(VMApi &api,
                                  const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  // Set padding property
  return args[0];
}

// element.bg(color)
static BytecodeValue uiElementBg(VMApi &api,
                                 const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  std::string color = toString(args[1]);
  // Set background property
  (void)color;

  return args[0];
}

// element.fg(color)
static BytecodeValue uiElementFg(VMApi &api,
                                 const std::vector<BytecodeValue> &args) {
  if (args.size() < 2)
    return BytecodeValue(nullptr);

  std::string color = toString(args[1]);
  (void)color;

  return args[0];
}

// element.alignRight()
static BytecodeValue
uiElementAlignRight(VMApi &api, const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  // Set align property
  return args[0];
}

// element.bold()
static BytecodeValue uiElementBold(VMApi &api,
                                   const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  // Set bold property
  return args[0];
}

// input.value() / input.value(newValue)
static BytecodeValue uiInputValue(VMApi &api,
                                  const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  if (args.size() == 1) {
    // Getter - return current value
    return BytecodeValue(std::string(""));
  } else {
    // Setter - set value
    return args[0];
  }
}

// textarea.value() / textarea.value(newValue)
static BytecodeValue uiTextareaValue(VMApi &api,
                                     const std::vector<BytecodeValue> &args) {
  if (args.empty())
    return BytecodeValue(nullptr);

  if (args.size() == 1) {
    return BytecodeValue(std::string(""));
  } else {
    return args[0];
  }
}

// ============================================================================
// API Selection Functions
// ============================================================================

// ui.setApi(apiName) - switch between qt/gtk/imgui
static BytecodeValue uiSetApi(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    return BytecodeValue(false);
  }

  std::string apiName = toString(args[0]);
  bool success = host::UIManager::instance().setBackend(apiName);
  return BytecodeValue(success);
}

// ui.getApi() -> current API name
static BytecodeValue uiGetApi() {
  std::string apiName = host::UIManager::instance().currentApiName();
  return BytecodeValue(apiName);
}

// ui.isAvailable(apiName) -> bool
static BytecodeValue uiIsAvailable(const std::vector<BytecodeValue> &args) {
  if (args.empty()) {
    return BytecodeValue(false);
  }

  std::string apiName = toString(args[0]);
  bool available = host::UIManager::instance().isBackendAvailable(apiName);
  return BytecodeValue(available);
}

// ============================================================================
// Register UI Module
// ============================================================================

void registerUIModule(compiler::VMApi &api) {
  // Element creation
  api.registerFunction("ui.window",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiWindow(api, args);
                       });

  api.registerFunction("ui.btn",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiBtn(api, args);
                       });

  api.registerFunction("ui.text",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiText(api, args);
                       });

  api.registerFunction("ui.label",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiLabel(api, args);
                       });

  api.registerFunction("ui.input",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiInput(api, args);
                       });

  api.registerFunction("ui.textarea",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiTextarea(api, args);
                       });

  api.registerFunction("ui.checkbox",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCheckbox(api, args);
                       });

  api.registerFunction("ui.slider",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiSlider(api, args);
                       });

  api.registerFunction("ui.dropdown",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiDropdown(api, args);
                       });

  api.registerFunction("ui.image",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiImage(api, args);
                       });

  api.registerFunction("ui.divider",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiDivider(api, args);
                       });

  api.registerFunction("ui.spacer",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiSpacer(api, args);
                       });

  api.registerFunction("ui.progress",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiProgress(api, args);
                       });

  api.registerFunction("ui.spinner",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiSpinner(api, args);
                       });

  // Menu elements
  api.registerFunction("ui.menu",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiMenu(api, args);
                       });

  api.registerFunction("ui.menuItem",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiMenuItem(api, args);
                       });

  api.registerFunction("ui.menuSeparator",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiMenuSeparator(api, args);
                       });

  // Layout containers
  api.registerFunction("ui.row",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiRow(api, args);
                       });

  api.registerFunction("ui.col",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCol(api, args);
                       });

  api.registerFunction("ui.grid",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiGrid(api, args);
                       });

  api.registerFunction("ui.table",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiTable(api, args);
                       });

  api.registerFunction("ui.flex",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiFlex(api, args);
                       });

  api.registerFunction("ui.scroll",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiScroll(api, args);
                       });

  api.registerFunction("ui.canvas",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvas(api, args);
                       });

  // Canvas operations
  api.registerFunction("ui.canvas.clear",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasClear(api, args);
                       });

  api.registerFunction("ui.canvas.fill",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasFill(api, args);
                       });

  api.registerFunction("ui.canvas.drawPoint",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasDrawPoint(api, args);
                       });

  api.registerFunction("ui.canvas.drawLine",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasDrawLine(api, args);
                       });

  api.registerFunction("ui.canvas.drawRect",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasDrawRect(api, args);
                       });

  api.registerFunction("ui.canvas.drawCircle",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasDrawCircle(api, args);
                       });

  api.registerFunction("ui.canvas.drawImage",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiCanvasDrawImage(api, args);
                       });

  // Dialogs
  api.registerFunction("ui.alert", [](const std::vector<BytecodeValue> &args) {
    return uiAlert(args);
  });

  api.registerFunction(
      "ui.confirm",
      [](const std::vector<BytecodeValue> &args) { return uiConfirm(args); });

  api.registerFunction("ui.filePicker",
                       [](const std::vector<BytecodeValue> &args) {
                         return uiFilePicker(args);
                       });

  api.registerFunction(
      "ui.dirPicker",
      [](const std::vector<BytecodeValue> &args) { return uiDirPicker(args); });

  api.registerFunction("ui.notify", [](const std::vector<BytecodeValue> &args) {
    return uiNotify(args);
  });

  // System Tray
  api.registerFunction(
      "ui.trayIcon",
      [](const std::vector<BytecodeValue> &args) { return uiTrayIcon(args); });

  api.registerFunction(
      "ui.trayShow",
      [](const std::vector<BytecodeValue> &args) { return uiTrayShow(args); });

  api.registerFunction(
      "ui.trayHide",
      [](const std::vector<BytecodeValue> &args) { return uiTrayHide(args); });

  api.registerFunction("ui.trayMenu",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiTrayMenu(api, args);
                       });

  api.registerFunction("ui.trayNotify",
                       [](const std::vector<BytecodeValue> &args) {
                         return uiTrayNotify(args);
                       });

  // Element methods
  api.registerFunction("ui.element.add",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementAdd(api, args);
                       });

  api.registerFunction("ui.element.show",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementShow(api, args);
                       });

  api.registerFunction("ui.element.hide",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementHide(api, args);
                       });

  api.registerFunction("ui.element.close",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementClose(api, args);
                       });

  api.registerFunction("ui.element.onClick",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnClick(api, args);
                       });

  api.registerFunction("ui.element.onChange",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnChange(api, args);
                       });

  // Mouse event handlers
  api.registerFunction("ui.element.onMouseDown",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnMouseDown(api, args);
                       });

  api.registerFunction("ui.element.onMouseUp",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnMouseUp(api, args);
                       });

  api.registerFunction("ui.element.onDblClick",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDblClick(api, args);
                       });

  api.registerFunction("ui.element.onMouseMove",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnMouseMove(api, args);
                       });

  api.registerFunction("ui.element.onMouseOver",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnMouseOver(api, args);
                       });

  api.registerFunction("ui.element.onMouseOut",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnMouseOut(api, args);
                       });

  api.registerFunction("ui.element.onDrag",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDrag(api, args);
                       });

  api.registerFunction("ui.element.onDragEnter",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDragEnter(api, args);
                       });

  api.registerFunction("ui.element.onDragLeave",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDragLeave(api, args);
                       });

  api.registerFunction("ui.element.onDropEnter",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDropEnter(api, args);
                       });

  api.registerFunction("ui.element.onDropLeave",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDropLeave(api, args);
                       });

  api.registerFunction("ui.element.onDrop",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnDrop(api, args);
                       });

  api.registerFunction("ui.element.onRightClick",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnRightClick(api, args);
                       });

  // Keyboard event handlers
  api.registerFunction("ui.element.onKeyDown",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnKeyDown(api, args);
                       });

  api.registerFunction("ui.element.onKeyUp",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnKeyUp(api, args);
                       });

  api.registerFunction("ui.element.onKeyPress",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnKeyPress(api, args);
                       });

  // UI event handlers
  api.registerFunction("ui.element.onFocus",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnFocus(api, args);
                       });

  api.registerFunction("ui.element.onBlur",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnBlur(api, args);
                       });

  api.registerFunction("ui.element.onLoad",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnLoad(api, args);
                       });

  api.registerFunction("ui.element.onUnload",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnUnload(api, args);
                       });

  api.registerFunction("ui.element.onScroll",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnScroll(api, args);
                       });

  api.registerFunction("ui.element.onCopy",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnCopy(api, args);
                       });

  api.registerFunction("ui.element.onCut",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnCut(api, args);
                       });

  api.registerFunction("ui.element.onPaste",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnPaste(api, args);
                       });

  api.registerFunction("ui.element.onLoaded",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnLoaded(api, args);
                       });

  api.registerFunction("ui.element.onSubmit",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementOnSubmit(api, args);
                       });

  api.registerFunction("ui.element.pad",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementPad(api, args);
                       });

  api.registerFunction("ui.element.bg",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementBg(api, args);
                       });

  api.registerFunction("ui.element.fg",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementFg(api, args);
                       });

  api.registerFunction("ui.element.alignRight",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementAlignRight(api, args);
                       });

  api.registerFunction("ui.element.bold",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementBold(api, args);
                       });

  api.registerFunction("ui.input.value",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiInputValue(api, args);
                       });

  api.registerFunction("ui.textarea.value",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiTextareaValue(api, args);
                       });

  // Styling functions
  api.registerFunction("ui.element.style",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementStyle(api, args);
                       });

  api.registerFunction("ui.element.width",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementWidth(api, args);
                       });

  api.registerFunction("ui.element.height",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementHeight(api, args);
                       });

  api.registerFunction("ui.element.border",
                       [&api](const std::vector<BytecodeValue> &args) {
                         return uiElementBorder(api, args);
                       });

  // API selection functions
  api.registerFunction("ui.setApi", [](const std::vector<BytecodeValue> &args) {
    return uiSetApi(args);
  });

  api.registerFunction("ui.getApi", [](const std::vector<BytecodeValue> &args) {
    (void)args;
    return uiGetApi();
  });

  api.registerFunction("ui.isAvailable",
                       [](const std::vector<BytecodeValue> &args) {
                         return uiIsAvailable(args);
                       });

  // Register global 'ui' object
  auto uiObj = api.makeObject();

  // Attach all creation functions as methods
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

  // API selection methods
  api.setField(uiObj, "setApi", api.makeFunctionRef("ui.setApi"));
  api.setField(uiObj, "getApi", api.makeFunctionRef("ui.getApi"));
  api.setField(uiObj, "isAvailable", api.makeFunctionRef("ui.isAvailable"));

  api.setGlobal("ui", uiObj);
}

} // namespace havel::modules
