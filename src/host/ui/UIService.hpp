/*
 * UIService.hpp - Qt widget mapping service for Havel UI
 *
 * Maps UIElement tree to actual Qt widgets/windows.
 * Handles realization, event wiring, and lifecycle.
 */
#pragma once

#include "modules/ui/UIElement.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <memory>
#include <unordered_map>

namespace havel::host {

/**
 * UIService - Maps UIElement structures to Qt widgets
 *
 * Design:
 * - Create UIElement tree from Havel script
 * - Realize elements -> create Qt widgets
 * - Wire events to Bytecode callbacks
 * - Manage window lifetime
 */
class UIService {
public:
  UIService();
  ~UIService();

  // Element creation (builds UIElement tree, not Qt widgets yet)
  std::shared_ptr<ui::UIElement> window(const std::string &title);
  std::shared_ptr<ui::UIElement> panel(const std::string &side = "center");
  std::shared_ptr<ui::UIElement> modal(const std::string &title);

  // Display elements
  std::shared_ptr<ui::UIElement> text(const std::string &content);
  std::shared_ptr<ui::UIElement> label(const std::string &content);
  std::shared_ptr<ui::UIElement> image(const std::string &path);
  std::shared_ptr<ui::UIElement> icon(const std::string &name);
  std::shared_ptr<ui::UIElement> divider();
  std::shared_ptr<ui::UIElement> spacer(int size = 10);
  std::shared_ptr<ui::UIElement> progress(int value = 0, int max = 100);
  std::shared_ptr<ui::UIElement> spinner();

  // Input elements
  std::shared_ptr<ui::UIElement> btn(const std::string &label);
  std::shared_ptr<ui::UIElement> input(const std::string &placeholder = "");
  std::shared_ptr<ui::UIElement> textarea(const std::string &placeholder = "");
  std::shared_ptr<ui::UIElement> checkbox(const std::string &label,
                                          bool checked = false);
  std::shared_ptr<ui::UIElement> toggle(const std::string &label,
                                        bool value = false);
  std::shared_ptr<ui::UIElement> slider(int min = 0, int max = 100,
                                        int value = 0);
  std::shared_ptr<ui::UIElement>
  dropdown(const std::vector<std::string> &options = {});

  // Layout containers
  std::shared_ptr<ui::UIElement> row();
  std::shared_ptr<ui::UIElement> col();
  std::shared_ptr<ui::UIElement> grid(int cols = 2);
  std::shared_ptr<ui::UIElement> scroll();

  // Menu elements
  std::shared_ptr<ui::UIElement> menu(const std::string &title);
  std::shared_ptr<ui::UIElement> menuItem(const std::string &label,
                                          const std::string &shortcut = "");
  std::shared_ptr<ui::UIElement> menuSeparator();

  // Realization - creates actual Qt widgets from UIElement tree
  void realize(std::shared_ptr<ui::UIElement> element);

  // Show/hide windows
  void show(std::shared_ptr<ui::UIElement> window);
  void hide(std::shared_ptr<ui::UIElement> window);
  void close(std::shared_ptr<ui::UIElement> window);

  // Dialogs
  void alert(const std::string &message);
  bool confirm(const std::string &message);
  std::string filePicker(const std::string &title = "Select file");
  std::string dirPicker(const std::string &title = "Select directory");
  void notify(const std::string &message, const std::string &type = "info");

  // Event pumping (call this to process Qt events)
  void pumpEvents(int timeoutMs = 0);

  // Check if any windows are still open
  bool hasActiveWindows() const;

  // Register callback for when all windows close
  void onAllWindowsClosed(std::function<void()> callback);

  // Get/set element value
  std::string getValue(std::shared_ptr<ui::UIElement> element);
  void setValue(std::shared_ptr<ui::UIElement> element,
                const std::string &value);

  // Style helpers
  void applyStyle(std::shared_ptr<ui::UIElement> element,
                  const std::string &key, const ui::PropValue &value);

private:
  // Qt widget creation for each element type
  QWidget *createWidget(ui::UIElement *element);
  QMainWindow *createWindow(ui::UIElement *element);
  QWidget *createPanel(ui::UIElement *element);
  QWidget *createButton(ui::UIElement *element);
  QWidget *createInput(ui::UIElement *element);
  QWidget *createTextEdit(ui::UIElement *element);
  QWidget *createCheckbox(ui::UIElement *element);
  QWidget *createSlider(ui::UIElement *element);
  QWidget *createDropdown(ui::UIElement *element);
  QWidget *createProgress(ui::UIElement *element);
  QWidget *createSpinner(ui::UIElement *element);
  QWidget *createLabel(ui::UIElement *element);
  QWidget *createImage(ui::UIElement *element);
  QWidget *createDivider(ui::UIElement *element);
  QWidget *createSpacer(ui::UIElement *element);

  // Layout containers
  QWidget *createRow(ui::UIElement *element);
  QWidget *createCol(ui::UIElement *element);
  QWidget *createGrid(ui::UIElement *element);
  QWidget *createScroll(ui::UIElement *element);

  // Menu creation
  void buildMenu(QMenuBar *menuBar, ui::UIElement *menuElement);

  // Event wiring
  void wireEvents(ui::UIElement *element, QWidget *widget);
  void wireButtonEvents(QPushButton *btn, ui::UIElement *element);
  void wireInputEvents(QLineEdit *input, ui::UIElement *element);
  void wireTextEditEvents(QTextEdit *textEdit, ui::UIElement *element);
  void wireCheckboxEvents(QCheckBox *checkbox, ui::UIElement *element);
  void wireSliderEvents(QSlider *slider, ui::UIElement *element);
  void wireDropdownEvents(QComboBox *dropdown, ui::UIElement *element);
  void wireWindowEvents(QMainWindow *window, ui::UIElement *element);

  // Style application
  void applyStylesheet(QWidget *widget, ui::UIElement *element);

  // Element -> Widget mapping
  std::unordered_map<ui::ElementId, QWidget *> widgets_;
  std::unordered_map<ui::ElementId, QMainWindow *> windows_;

  // Next element ID
  ui::ElementId nextId_ = 1;

  // Callback for when all windows close
  std::function<void()> allWindowsClosedCallback_;

  // Count of open windows
  int openWindowCount_ = 0;

  // Ensure QApplication exists
  void ensureApp();
  bool appOwned_ = false;
};

} // namespace havel::host
