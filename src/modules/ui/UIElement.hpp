/*
 * UIElement.hpp - Core UI element structure for Havel UI module
 *
 * Design principles:
 * - No lifecycle: scripts create/use/dispose UI immediately
 * - Events = plain callbacks (no signals, no observers)
 * - Layout = row/col only (no CSS complexity)
 * - UI is disposable: show() → sleep() → close() should feel normal
 * - Creation is instant: no setup rituals
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace havel::ui {

// Forward declarations
struct UIElement;
class UIService;

// Element ID for referencing elements
using ElementId = uint64_t;

// Callback function type for UI events
using UIEventCallback = std::function<void()>;
using UIEventCallbackWithValue = std::function<void(const std::string &)>;
using UIEventCallbackWithXY = std::function<void(int, int)>;

// Property value types (variant for flexibility)
using PropValue =
    std::variant<std::nullptr_t, bool, int64_t, double, std::string,
                 ElementId // Reference to another element
                 >;

// Event handler variant - supports different callback signatures
using EventHandler = std::variant<UIEventCallback, UIEventCallbackWithValue,
                                  UIEventCallbackWithXY>;

/**
 * UIElement - Core UI element structure
 *
 * Simple struct that represents any UI element:
 * - type: "window", "btn", "text", "input", "col", "row", etc.
 * - props: named properties (width, height, text, etc.)
 * - events: callback handlers for interactions
 * - children: nested UI elements
 */
struct UIElement : public std::enable_shared_from_this<UIElement> {
  std::string type;
  std::map<std::string, PropValue> props;
  std::map<std::string, EventHandler> events;
  std::vector<std::shared_ptr<UIElement>> children;

  // Optional parent reference (weak to avoid cycles)
  std::weak_ptr<UIElement> parent;

  // Unique ID for this element instance
  ElementId id = 0;

  // Service handle (for Qt widget mapping)
  void *nativeHandle = nullptr;

  // Element is realized (Qt widget created)
  bool realized = false;

  // Element is visible
  bool visible = false;

  UIElement() = default;
  explicit UIElement(std::string t) : type(std::move(t)) {}

  // Helper to get property as specific type
  template <typename T> T getProp(const std::string &key, T defaultVal) const {
    auto it = props.find(key);
    if (it == props.end())
      return defaultVal;
    if (std::holds_alternative<T>(it->second)) {
      return std::get<T>(it->second);
    }
    return defaultVal;
  }

  // Specialization for int -> int64_t conversion
  int getProp(const std::string &key, int defaultVal) const {
    auto it = props.find(key);
    if (it == props.end())
      return defaultVal;
    if (std::holds_alternative<int64_t>(it->second)) {
      return static_cast<int>(std::get<int64_t>(it->second));
    }
    return defaultVal;
  }

  // Helper to set property and return self for chaining
  UIElement &set(const std::string &key, PropValue value) {
    props[key] = std::move(value);
    return *this;
  }

  // Set event handler and return self
  UIElement &on(const std::string &event, UIEventCallback handler) {
    events[event] = handler;
    return *this;
  }

  UIElement &on(const std::string &event, UIEventCallbackWithValue handler) {
    events[event] = handler;
    return *this;
  }

  UIElement &on(const std::string &event, UIEventCallbackWithXY handler) {
    events[event] = handler;
    return *this;
  }

  // Add child and return self for chaining
  UIElement &add(std::shared_ptr<UIElement> child) {
    if (child) {
      child->parent = weak_from_this();
      children.push_back(std::move(child));
    }
    return *this;
  }

  // Shorthand helpers for styling (return self for chaining)
  UIElement &pad(int p) { return set("padding", static_cast<int64_t>(p)); }
  UIElement &bg(const std::string &color) { return set("background", color); }
  UIElement &fg(const std::string &color) { return set("color", color); }
  UIElement &font(const std::string &family, int size) {
    set("fontFamily", family);
    return set("fontSize", static_cast<int64_t>(size));
  }
  UIElement &width(int w) { return set("width", static_cast<int64_t>(w)); }
  UIElement &height(int h) { return set("height", static_cast<int64_t>(h)); }
  UIElement &text(const std::string &t) { return set("text", t); }
  UIElement &bold(bool b = true) { return set("bold", b); }
  UIElement &align(const std::string &a) { return set("align", a); }
  UIElement &alignRight() { return set("align", std::string("right")); }
  UIElement &alignCenter() { return set("align", std::string("center")); }
};

// Element type constants
namespace ElementType {
inline constexpr const char *WINDOW = "window";
inline constexpr const char *PANEL = "panel";
inline constexpr const char *MODAL = "modal";
inline constexpr const char *DRAWER = "drawer";
inline constexpr const char *TABS = "tabs";

inline constexpr const char *TEXT = "text";
inline constexpr const char *LABEL = "label";
inline constexpr const char *IMAGE = "image";
inline constexpr const char *ICON = "icon";
inline constexpr const char *DIVIDER = "divider";
inline constexpr const char *SPACER = "spacer";
inline constexpr const char *PROGRESS = "progress";
inline constexpr const char *SPINNER = "spinner";

inline constexpr const char *BUTTON = "btn";
inline constexpr const char *INPUT = "input";
inline constexpr const char *TEXTAREA = "textarea";
inline constexpr const char *CHECKBOX = "checkbox";
inline constexpr const char *TOGGLE = "toggle";
inline constexpr const char *SLIDER = "slider";
inline constexpr const char *DROPDOWN = "dropdown";
inline constexpr const char *DATEPICKER = "datepicker";
inline constexpr const char *COLORPICKER = "colorpicker";

inline constexpr const char *ROW = "row";
inline constexpr const char *COL = "col";
inline constexpr const char *GRID = "grid";
inline constexpr const char *SCROLL = "scroll";
inline constexpr const char *CANVAS = "canvas";

inline constexpr const char *MENU = "menu";
inline constexpr const char *MENU_ITEM = "menu_item";
inline constexpr const char *MENU_ACTION = "menu_action";
inline constexpr const char *MENU_SEP = "menu_sep";
} // namespace ElementType

// Event name constants
namespace EventType {
// Mouse Events
inline constexpr const char *MOUSEDOWN = "mousedown";
inline constexpr const char *MOUSEUP = "mouseup";
inline constexpr const char *CLICK = "click";
inline constexpr const char *DBLCLICK = "dblclick";
inline constexpr const char *MOUSEMOVE = "mousemove";
inline constexpr const char *MOUSEOVER = "mouseover";
inline constexpr const char *MOUSEOUT = "mouseout";
inline constexpr const char *DRAG = "drag";
inline constexpr const char *DRAGENTER = "dragenter";
inline constexpr const char *DRAGLEAVE = "dragleave";
inline constexpr const char *DROPENTER = "dropenter";
inline constexpr const char *DROPLEAVE = "dropleave";
inline constexpr const char *DROP = "drop";
inline constexpr const char *RIGHTCLICK = "rightclick";

// Keyboard Events
inline constexpr const char *KEYDOWN = "keydown";
inline constexpr const char *KEYUP = "keyup";
inline constexpr const char *KEYPRESS = "keypress";

// UI Events
inline constexpr const char *SUBMIT = "submit";
inline constexpr const char *CHANGE = "change";
inline constexpr const char *FOCUS = "focus";
inline constexpr const char *BLUR = "blur";
inline constexpr const char *LOAD = "load";
inline constexpr const char *UNLOAD = "unload";
inline constexpr const char *RESIZE = "resize";
inline constexpr const char *SCROLL = "scroll";
inline constexpr const char *COPY = "copy";
inline constexpr const char *CUT = "cut";
inline constexpr const char *PASTE = "paste";
inline constexpr const char *LOADED = "loaded";

// Window/Other Events
inline constexpr const char *CLOSE = "close";
inline constexpr const char *MOVE = "move";
inline constexpr const char *SELECT = "select";
} // namespace EventType

} // namespace havel::ui
