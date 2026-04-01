/*
 * ExtensionUIBridge.hpp - Bridge to load UI backends from extensions
 */
#pragma once

#include "UIBackend.hpp"
#include "extensions/ExtensionLoader.hpp"

#include <functional>
#include <unordered_map>

namespace havel {
namespace host {

/**
 * ExtensionUIBridge - Loads UI backends from extensions
 * 
 * This class implements UIBackend by delegating to functions
 * loaded from extension .so files (gtk_extension.so, imgui_extension.so)
 */
class ExtensionUIBridge : public UIBackend {
public:
  ExtensionUIBridge(const std::string& extensionName);
  ~ExtensionUIBridge() override;

  // Backend info
  Api getApi() const override;
  std::string getApiName() const override;
  bool isAvailable() const override;

  // Initialization
  bool initialize() override;
  void shutdown() override;

  // Element creation
  std::shared_ptr<ui::UIElement> window(const std::string& title) override;
  std::shared_ptr<ui::UIElement> panel(const std::string& side) override;
  std::shared_ptr<ui::UIElement> modal(const std::string& title) override;
  std::shared_ptr<ui::UIElement> text(const std::string& content) override;
  std::shared_ptr<ui::UIElement> label(const std::string& content) override;
  std::shared_ptr<ui::UIElement> image(const std::string& path) override;
  std::shared_ptr<ui::UIElement> icon(const std::string& name) override;
  std::shared_ptr<ui::UIElement> divider() override;
  std::shared_ptr<ui::UIElement> spacer(int size) override;
  std::shared_ptr<ui::UIElement> progress(int value, int max) override;
  std::shared_ptr<ui::UIElement> spinner() override;
  std::shared_ptr<ui::UIElement> btn(const std::string& label) override;
  std::shared_ptr<ui::UIElement> input(const std::string& placeholder) override;
  std::shared_ptr<ui::UIElement> textarea(const std::string& placeholder) override;
  std::shared_ptr<ui::UIElement> checkbox(const std::string& label, bool checked) override;
  std::shared_ptr<ui::UIElement> toggle(const std::string& label, bool value) override;
  std::shared_ptr<ui::UIElement> slider(int min, int max, int value) override;
  std::shared_ptr<ui::UIElement> dropdown(const std::vector<std::string>& options) override;
  std::shared_ptr<ui::UIElement> row() override;
  std::shared_ptr<ui::UIElement> col() override;
  std::shared_ptr<ui::UIElement> grid(int cols) override;
  std::shared_ptr<ui::UIElement> table(int rows, int cols) override;
  std::shared_ptr<ui::UIElement> flex(const std::string& direction) override;
  std::shared_ptr<ui::UIElement> scroll() override;
  std::shared_ptr<ui::UIElement> canvas(int width, int height) override;

  // Menu elements
  std::shared_ptr<ui::UIElement> menu(const std::string& title) override;
  std::shared_ptr<ui::UIElement> menuItem(const std::string& label, const std::string& shortcut) override;
  std::shared_ptr<ui::UIElement> menuSeparator() override;

  // Realization
  void realize(std::shared_ptr<ui::UIElement> element) override;

  // Show/hide/close
  void show(std::shared_ptr<ui::UIElement> element) override;
  void hide(std::shared_ptr<ui::UIElement> element) override;
  void close(std::shared_ptr<ui::UIElement> element) override;

  // Dialogs
  void alert(const std::string& message) override;
  bool confirm(const std::string& message) override;
  std::string filePicker(const std::string& title) override;
  std::string dirPicker(const std::string& title) override;
  void notify(const std::string& message, const std::string& type) override;

  // System tray
  void trayIcon(const std::string& iconPath, const std::string& tooltip) override;
  void trayShow() override;
  void trayHide() override;
  void trayMenu(std::shared_ptr<ui::UIElement> menu) override;
  void trayNotify(const std::string& title, const std::string& message, const std::string& iconType) override;
  bool trayIsVisible() const override;

  // Event pumping
  void pumpEvents(int timeoutMs) override;
  bool hasActiveWindows() const override;
  void onAllWindowsClosed(std::function<void()> callback) override;

  // Get/set element value
  std::string getValue(std::shared_ptr<ui::UIElement> element) override;
  void setValue(std::shared_ptr<ui::UIElement> element, const std::string& value) override;

  // Style application
  void applyStyle(std::shared_ptr<ui::UIElement> element, const std::string& key, const ui::PropValue& value) override;

  // Extension specific
  bool loadExtension();
  bool isExtensionLoaded() const;

private:
  std::string extensionName_;
  Api apiType_;
  bool loaded_ = false;
  std::unique_ptr<havel::ExtensionLoader> loader_;

  // Element ID counter for extension bridge
  static ui::ElementId nextId_;
  
  // Track created elements
  std::unordered_map<ui::ElementId, std::shared_ptr<ui::UIElement>> elements_;

  // Helper to create element
  std::shared_ptr<ui::UIElement> createElement(const std::string& type);
};

} // namespace host
} // namespace havel
