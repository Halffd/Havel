/*
 * ExtensionUIBridge.cpp - Bridge implementation for extension-based UI backends
 */
#include "ExtensionUIBridge.hpp"

#include <iostream>

namespace havel {
namespace host {

ui::ElementId ExtensionUIBridge::nextId_ = 1;

ExtensionUIBridge::ExtensionUIBridge(const std::string& extensionName)
    : extensionName_(extensionName), apiType_(Api::AUTO), loaded_(false) {
  if (extensionName == "gtk") {
    apiType_ = Api::GTK;
  } else if (extensionName == "imgui") {
    apiType_ = Api::IMGUI;
  }
}

ExtensionUIBridge::~ExtensionUIBridge() = default;

bool ExtensionUIBridge::loadExtension() {
  if (loaded_) {
    return true;
  }

  loader_ = std::make_unique<havel::ExtensionLoader>();
  
  // Add build directory for development
  loader_->addSearchPath("/home/all/repos/havel-wm/havel/build-debug/extensions");
  
  // Try to load the extension
  std::string extName = extensionName_ + "_extension";
  loaded_ = loader_->loadExtensionByName(extName);
  
  if (!loaded_) {
    std::cerr << "[ExtensionUIBridge] Failed to load " << extName << std::endl;
  } else {
    std::cout << "[ExtensionUIBridge] Loaded " << extName << std::endl;
  }
  
  return loaded_;
}

bool ExtensionUIBridge::isExtensionLoaded() const {
  return loaded_;
}

UIBackend::Api ExtensionUIBridge::getApi() const {
  return apiType_;
}

std::string ExtensionUIBridge::getApiName() const {
  return extensionName_;
}

bool ExtensionUIBridge::initialize() {
  // Extension is loaded via loadExtension() before use
  return loaded_;
}

void ExtensionUIBridge::shutdown() {
  elements_.clear();
  loader_.reset();
  loaded_ = false;
}

bool ExtensionUIBridge::isAvailable() const {
  return loaded_;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::createElement(const std::string& type) {
  auto elem = std::make_shared<ui::UIElement>(type);
  elem->id = nextId_++;
  elements_[elem->id] = elem;
  return elem;
}

// Element creation - delegates to extension
std::shared_ptr<ui::UIElement> ExtensionUIBridge::window(const std::string& title) {
  auto elem = createElement(ui::ElementType::WINDOW);
  elem->set("title", title);
  elem->set("width", static_cast<int64_t>(800));
  elem->set("height", static_cast<int64_t>(600));
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::panel(const std::string& side) {
  auto elem = createElement(ui::ElementType::PANEL);
  elem->set("side", side);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::modal(const std::string& title) {
  auto elem = createElement(ui::ElementType::MODAL);
  elem->set("title", title);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::text(const std::string& content) {
  auto elem = createElement(ui::ElementType::TEXT);
  elem->set("text", content);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::label(const std::string& content) {
  auto elem = createElement(ui::ElementType::LABEL);
  elem->set("text", content);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::image(const std::string& path) {
  auto elem = createElement(ui::ElementType::IMAGE);
  elem->set("path", path);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::icon(const std::string& name) {
  auto elem = createElement(ui::ElementType::ICON);
  elem->set("name", name);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::divider() {
  return createElement(ui::ElementType::DIVIDER);
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::spacer(int size) {
  auto elem = createElement(ui::ElementType::SPACER);
  elem->set("size", static_cast<int64_t>(size));
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::progress(int value, int max) {
  auto elem = createElement(ui::ElementType::PROGRESS);
  elem->set("value", static_cast<int64_t>(value));
  elem->set("max", static_cast<int64_t>(max));
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::spinner() {
  return createElement(ui::ElementType::SPINNER);
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::btn(const std::string& label) {
  auto elem = createElement(ui::ElementType::BUTTON);
  elem->set("text", label);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::input(const std::string& placeholder) {
  auto elem = createElement(ui::ElementType::INPUT);
  elem->set("placeholder", placeholder);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::textarea(const std::string& placeholder) {
  auto elem = createElement(ui::ElementType::TEXTAREA);
  elem->set("placeholder", placeholder);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::checkbox(const std::string& label, bool checked) {
  auto elem = createElement(ui::ElementType::CHECKBOX);
  elem->set("label", label);
  elem->set("checked", checked);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::toggle(const std::string& label, bool value) {
  auto elem = createElement(ui::ElementType::TOGGLE);
  elem->set("label", label);
  elem->set("value", value);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::slider(int min, int max, int value) {
  auto elem = createElement(ui::ElementType::SLIDER);
  elem->set("min", static_cast<int64_t>(min));
  elem->set("max", static_cast<int64_t>(max));
  elem->set("value", static_cast<int64_t>(value));
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::dropdown(const std::vector<std::string>& options) {
  auto elem = createElement(ui::ElementType::DROPDOWN);
  std::string opts;
  for (size_t i = 0; i < options.size(); ++i) {
    if (i > 0) opts += "|";
    opts += options[i];
  }
  elem->set("options", opts);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::row() {
  return createElement(ui::ElementType::ROW);
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::col() {
  return createElement(ui::ElementType::COL);
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::grid(int cols) {
  auto elem = createElement(ui::ElementType::GRID);
  elem->set("cols", static_cast<int64_t>(cols));
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::scroll() {
  return createElement(ui::ElementType::SCROLL);
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::menu(const std::string& title) {
  auto elem = createElement(ui::ElementType::MENU);
  elem->set("title", title);
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::menuItem(const std::string& label, const std::string& shortcut) {
  auto elem = createElement(ui::ElementType::MENU_ITEM);
  elem->set("label", label);
  if (!shortcut.empty()) {
    elem->set("shortcut", shortcut);
  }
  return elem;
}

std::shared_ptr<ui::UIElement> ExtensionUIBridge::menuSeparator() {
  return createElement(ui::ElementType::MENU_SEP);
}

void ExtensionUIBridge::realize(std::shared_ptr<ui::UIElement> element) {
  if (element) {
    element->realized = true;
  }
}

void ExtensionUIBridge::show(std::shared_ptr<ui::UIElement> element) {
  if (element) {
    element->visible = true;
  }
}

void ExtensionUIBridge::hide(std::shared_ptr<ui::UIElement> element) {
  if (element) {
    element->visible = false;
  }
}

void ExtensionUIBridge::close(std::shared_ptr<ui::UIElement> element) {
  if (element) {
    elements_.erase(element->id);
  }
}

void ExtensionUIBridge::alert(const std::string& message) {
  std::cout << "[" << extensionName_ << "::alert] " << message << std::endl;
}

bool ExtensionUIBridge::confirm(const std::string& message) {
  std::cout << "[" << extensionName_ << "::confirm] " << message << std::endl;
  return false;
}

std::string ExtensionUIBridge::filePicker(const std::string& title) {
  (void)title;
  return "";
}

std::string ExtensionUIBridge::dirPicker(const std::string& title) {
  (void)title;
  return "";
}

void ExtensionUIBridge::notify(const std::string& message, const std::string& type) {
  std::cout << "[" << extensionName_ << "::notify] (" << type << ") " << message << std::endl;
}

void ExtensionUIBridge::trayIcon(const std::string& iconPath, const std::string& tooltip) {
  (void)iconPath;
  (void)tooltip;
}

void ExtensionUIBridge::trayShow() {
}

void ExtensionUIBridge::trayHide() {
}

void ExtensionUIBridge::trayMenu(std::shared_ptr<ui::UIElement> menu) {
  (void)menu;
}

void ExtensionUIBridge::trayNotify(const std::string& title, const std::string& message, const std::string& iconType) {
  std::cout << "[" << extensionName_ << "::trayNotify] " << title << ": " << message << " (" << iconType << ")" << std::endl;
}

bool ExtensionUIBridge::trayIsVisible() const {
  return false;
}

void ExtensionUIBridge::pumpEvents(int timeoutMs) {
  (void)timeoutMs;
}

bool ExtensionUIBridge::hasActiveWindows() const {
  return false;
}

void ExtensionUIBridge::onAllWindowsClosed(std::function<void()> callback) {
  (void)callback;
}

std::string ExtensionUIBridge::getValue(std::shared_ptr<ui::UIElement> element) {
  if (!element) return "";
  return element->getProp("value", std::string(""));
}

void ExtensionUIBridge::setValue(std::shared_ptr<ui::UIElement> element, const std::string& value) {
  if (element) {
    element->set("value", value);
  }
}

void ExtensionUIBridge::applyStyle(std::shared_ptr<ui::UIElement> element, const std::string& key, const ui::PropValue& value) {
  if (element) {
    element->set(key, value);
  }
}

} // namespace host
} // namespace havel
