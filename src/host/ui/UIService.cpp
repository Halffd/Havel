/*
 * UIService.cpp - Qt widget mapping implementation
 */
#include "UIService.hpp"

#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QSystemTrayIcon>

namespace havel::host {

UIService::UIService() = default;

UIService::~UIService() {
  // Clean up windows
  for (auto &[id, window] : windows_) {
    if (window) {
      window->close();
      delete window;
    }
  }
  windows_.clear();
  widgets_.clear();
}

void UIService::ensureApp() {
  if (!QApplication::instance()) {
    static int argc = 1;
    static char *argv[] = {const_cast<char *>("havel"), nullptr};
    new QApplication(argc, argv);
    appOwned_ = true;
  }
}

// Element creation - these just create the UIElement tree
std::shared_ptr<ui::UIElement> UIService::window(const std::string &title) {
  ensureApp();
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::WINDOW);
  elem->id = nextId_++;
  elem->set("title", title);
  elem->set("width", static_cast<int64_t>(800));
  elem->set("height", static_cast<int64_t>(600));
  elem->set("resizable", true);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::panel(const std::string &side) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::PANEL);
  elem->id = nextId_++;
  elem->set("side", side);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::modal(const std::string &title) {
  ensureApp();
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::MODAL);
  elem->id = nextId_++;
  elem->set("title", title);
  elem->set("width", static_cast<int64_t>(400));
  elem->set("height", static_cast<int64_t>(300));
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::text(const std::string &content) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::TEXT);
  elem->id = nextId_++;
  elem->set("text", content);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::label(const std::string &content) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::LABEL);
  elem->id = nextId_++;
  elem->set("text", content);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::image(const std::string &path) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::IMAGE);
  elem->id = nextId_++;
  elem->set("path", path);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::icon(const std::string &name) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::ICON);
  elem->id = nextId_++;
  elem->set("name", name);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::divider() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::DIVIDER);
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::spacer(int size) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::SPACER);
  elem->id = nextId_++;
  elem->set("size", static_cast<int64_t>(size));
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::progress(int value, int max) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::PROGRESS);
  elem->id = nextId_++;
  elem->set("value", static_cast<int64_t>(value));
  elem->set("max", static_cast<int64_t>(max));
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::spinner() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::SPINNER);
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::btn(const std::string &label) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::BUTTON);
  elem->id = nextId_++;
  elem->set("text", label);
  return elem;
}

std::shared_ptr<ui::UIElement>
UIService::input(const std::string &placeholder) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::INPUT);
  elem->id = nextId_++;
  elem->set("placeholder", placeholder);
  elem->set("value", std::string(""));
  return elem;
}

std::shared_ptr<ui::UIElement>
UIService::textarea(const std::string &placeholder) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::TEXTAREA);
  elem->id = nextId_++;
  elem->set("placeholder", placeholder);
  elem->set("value", std::string(""));
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::checkbox(const std::string &label,
                                                   bool checked) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::CHECKBOX);
  elem->id = nextId_++;
  elem->set("label", label);
  elem->set("checked", checked);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::toggle(const std::string &label,
                                                 bool value) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::TOGGLE);
  elem->id = nextId_++;
  elem->set("label", label);
  elem->set("value", value);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::slider(int min, int max, int value) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::SLIDER);
  elem->id = nextId_++;
  elem->set("min", static_cast<int64_t>(min));
  elem->set("max", static_cast<int64_t>(max));
  elem->set("value", static_cast<int64_t>(value));
  return elem;
}

std::shared_ptr<ui::UIElement>
UIService::dropdown(const std::vector<std::string> &options) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::DROPDOWN);
  elem->id = nextId_++;
  // Store options as comma-separated or in a custom prop
  std::string opts;
  for (size_t i = 0; i < options.size(); ++i) {
    if (i > 0)
      opts += "|";
    opts += options[i];
  }
  elem->set("options", opts);
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::row() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::ROW);
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::col() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::COL);
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::grid(int cols) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::GRID);
  elem->id = nextId_++;
  elem->set("cols", static_cast<int64_t>(cols));
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::scroll() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::SCROLL);
  elem->id = nextId_++;
  return elem;
}

// Realization - creates actual Qt widgets
void UIService::realize(std::shared_ptr<ui::UIElement> element) {
  if (!element || element->realized)
    return;

  element->realized = true;

  QWidget *widget = createWidget(element.get());
  if (!widget)
    return;

  widgets_[element->id] = widget;

  // Wire events
  wireEvents(element.get(), widget);

  // Recursively realize children
  for (auto &child : element->children) {
    realize(child);

    // Add child widget to parent layout
    if (auto *childWidget = widgets_[child->id]) {
      if (auto *layout = widget->layout()) {
        layout->addWidget(childWidget);
      }
    }
  }
}

QWidget *UIService::createWidget(ui::UIElement *element) {
  const std::string &type = element->type;

  if (type == ui::ElementType::WINDOW) {
    return createWindow(element);
  } else if (type == ui::ElementType::PANEL) {
    return createPanel(element);
  } else if (type == ui::ElementType::BUTTON) {
    return createButton(element);
  } else if (type == ui::ElementType::INPUT) {
    return createInput(element);
  } else if (type == ui::ElementType::TEXTAREA) {
    return createTextEdit(element);
  } else if (type == ui::ElementType::CHECKBOX) {
    return createCheckbox(element);
  } else if (type == ui::ElementType::SLIDER) {
    return createSlider(element);
  } else if (type == ui::ElementType::DROPDOWN) {
    return createDropdown(element);
  } else if (type == ui::ElementType::PROGRESS) {
    return createProgress(element);
  } else if (type == ui::ElementType::SPINNER) {
    return createSpinner(element);
  } else if (type == ui::ElementType::TEXT || type == ui::ElementType::LABEL) {
    return createLabel(element);
  } else if (type == ui::ElementType::IMAGE) {
    return createImage(element);
  } else if (type == ui::ElementType::DIVIDER) {
    return createDivider(element);
  } else if (type == ui::ElementType::SPACER) {
    return createSpacer(element);
  } else if (type == ui::ElementType::ROW) {
    return createRow(element);
  } else if (type == ui::ElementType::COL) {
    return createCol(element);
  } else if (type == ui::ElementType::GRID) {
    return createGrid(element);
  } else if (type == ui::ElementType::SCROLL) {
    return createScroll(element);
  }

  return nullptr;
}

QMainWindow *UIService::createWindow(ui::UIElement *element) {
  auto *window = new QMainWindow();

  std::string title = element->getProp("title", std::string("Window"));
  int width = element->getProp("width", 800);
  int height = element->getProp("height", 600);
  bool resizable = element->getProp("resizable", true);

  window->setWindowTitle(QString::fromStdString(title));
  window->resize(width, height);

  if (!resizable) {
    window->setFixedSize(width, height);
  }

  // Create central widget with layout
  auto *central = new QWidget();
  window->setCentralWidget(central);

  // Default to vertical layout for windows
  new QVBoxLayout(central);

  // Store in windows map
  windows_[element->id] = window;
  openWindowCount_++;

  return window;
}

QWidget *UIService::createPanel(ui::UIElement *element) {
  auto *panel = new QWidget();

  std::string side = element->getProp("side", std::string("center"));
  int width = element->getProp("width", 200);
  int height = element->getProp("height", 0);

  if (width > 0) {
    panel->setMinimumWidth(width);
    panel->setMaximumWidth(width);
  }
  if (height > 0) {
    panel->setMinimumHeight(height);
    panel->setMaximumHeight(height);
  }

  // Default layout based on side
  if (side == "left" || side == "right") {
    new QVBoxLayout(panel);
  } else {
    new QHBoxLayout(panel);
  }

  applyStylesheet(panel, element);
  return panel;
}

QWidget *UIService::createButton(ui::UIElement *element) {
  std::string text = element->getProp("text", std::string("Button"));
  auto *btn = new QPushButton(QString::fromStdString(text));
  applyStylesheet(btn, element);
  return btn;
}

QWidget *UIService::createInput(ui::UIElement *element) {
  std::string placeholder = element->getProp("placeholder", std::string(""));
  std::string value = element->getProp("value", std::string(""));

  auto *input = new QLineEdit();
  input->setPlaceholderText(QString::fromStdString(placeholder));
  input->setText(QString::fromStdString(value));

  applyStylesheet(input, element);
  return input;
}

QWidget *UIService::createTextEdit(ui::UIElement *element) {
  std::string placeholder = element->getProp("placeholder", std::string(""));

  auto *textEdit = new QTextEdit();
  textEdit->setPlaceholderText(QString::fromStdString(placeholder));

  applyStylesheet(textEdit, element);
  return textEdit;
}

QWidget *UIService::createCheckbox(ui::UIElement *element) {
  std::string label = element->getProp("label", std::string(""));
  bool checked = element->getProp("checked", false);

  auto *checkbox = new QCheckBox(QString::fromStdString(label));
  checkbox->setChecked(checked);

  applyStylesheet(checkbox, element);
  return checkbox;
}

QWidget *UIService::createSlider(ui::UIElement *element) {
  int min = element->getProp("min", 0);
  int max = element->getProp("max", 100);
  int value = element->getProp("value", 0);

  auto *slider = new QSlider(Qt::Horizontal);
  slider->setMinimum(min);
  slider->setMaximum(max);
  slider->setValue(value);

  applyStylesheet(slider, element);
  return slider;
}

QWidget *UIService::createDropdown(ui::UIElement *element) {
  std::string options = element->getProp("options", std::string(""));

  auto *dropdown = new QComboBox();

  // Parse options (pipe-separated)
  QString opts = QString::fromStdString(options);
  QStringList list = opts.split("|");
  for (const auto &item : list) {
    if (!item.isEmpty()) {
      dropdown->addItem(item);
    }
  }

  applyStylesheet(dropdown, element);
  return dropdown;
}

QWidget *UIService::createProgress(ui::UIElement *element) {
  int value = element->getProp("value", 0);
  int max = element->getProp("max", 100);

  auto *progress = new QProgressBar();
  progress->setMinimum(0);
  progress->setMaximum(max);
  progress->setValue(value);

  applyStylesheet(progress, element);
  return progress;
}

QWidget *UIService::createSpinner(ui::UIElement *element) {
  (void)element;
  // Qt doesn't have a native spinner, use progress bar with indeterminate mode
  auto *spinner = new QProgressBar();
  spinner->setRange(0, 0); // Indeterminate

  applyStylesheet(spinner, element);
  return spinner;
}

QWidget *UIService::createLabel(ui::UIElement *element) {
  std::string text = element->getProp("text", std::string(""));
  bool bold = element->getProp("bold", false);
  std::string align = element->getProp("align", std::string("left"));

  auto *label = new QLabel(QString::fromStdString(text));

  if (bold) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
  }

  if (align == "center") {
    label->setAlignment(Qt::AlignCenter);
  } else if (align == "right") {
    label->setAlignment(Qt::AlignRight);
  }

  applyStylesheet(label, element);
  return label;
}

QWidget *UIService::createImage(ui::UIElement *element) {
  std::string path = element->getProp("path", std::string(""));

  auto *label = new QLabel();
  label->setPixmap(QPixmap(QString::fromStdString(path)));
  label->setScaledContents(true);

  applyStylesheet(label, element);
  return label;
}

QWidget *UIService::createDivider(ui::UIElement *element) {
  (void)element;
  auto *line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  return line;
}

QWidget *UIService::createSpacer(ui::UIElement *element) {
  int size = element->getProp("size", 10);
  auto *spacer = new QSpacerItem(size, size, QSizePolicy::Expanding,
                                 QSizePolicy::Expanding);
  // Note: QSpacerItem isn't a QWidget, so we wrap it
  auto *widget = new QWidget();
  widget->setFixedSize(size, size);
  return widget;
}

QWidget *UIService::createRow(ui::UIElement *element) {
  auto *widget = new QWidget();
  auto *layout = new QHBoxLayout(widget);
  layout->setSpacing(5);
  layout->setContentsMargins(5, 5, 5, 5);
  applyStylesheet(widget, element);
  return widget;
}

QWidget *UIService::createCol(ui::UIElement *element) {
  auto *widget = new QWidget();
  auto *layout = new QVBoxLayout(widget);
  layout->setSpacing(5);
  layout->setContentsMargins(5, 5, 5, 5);
  applyStylesheet(widget, element);
  return widget;
}

QWidget *UIService::createGrid(ui::UIElement *element) {
  int cols = element->getProp("cols", 2);

  auto *widget = new QWidget();
  auto *layout = new QGridLayout(widget);
  layout->setSpacing(5);
  layout->setContentsMargins(5, 5, 5, 5);

  // Store column count for child placement
  element->set("__cols", static_cast<int64_t>(cols));

  applyStylesheet(widget, element);
  return widget;
}

QWidget *UIService::createScroll(ui::UIElement *element) {
  (void)element;
  auto *scroll = new QScrollArea();
  scroll->setWidgetResizable(true);

  auto *content = new QWidget();
  new QVBoxLayout(content);
  scroll->setWidget(content);

  applyStylesheet(scroll, element);
  return scroll;
}

// Event wiring
void UIService::wireEvents(ui::UIElement *element, QWidget *widget) {
  if (element->events.empty())
    return;

  const std::string &type = element->type;

  if (type == ui::ElementType::BUTTON) {
    if (auto *btn = qobject_cast<QPushButton *>(widget)) {
      wireButtonEvents(btn, element);
    }
  } else if (type == ui::ElementType::INPUT) {
    if (auto *input = qobject_cast<QLineEdit *>(widget)) {
      wireInputEvents(input, element);
    }
  } else if (type == ui::ElementType::TEXTAREA) {
    if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
      wireTextEditEvents(textEdit, element);
    }
  } else if (type == ui::ElementType::CHECKBOX) {
    if (auto *checkbox = qobject_cast<QCheckBox *>(widget)) {
      wireCheckboxEvents(checkbox, element);
    }
  } else if (type == ui::ElementType::SLIDER) {
    if (auto *slider = qobject_cast<QSlider *>(widget)) {
      wireSliderEvents(slider, element);
    }
  } else if (type == ui::ElementType::DROPDOWN) {
    if (auto *dropdown = qobject_cast<QComboBox *>(widget)) {
      wireDropdownEvents(dropdown, element);
    }
  } else if (type == ui::ElementType::WINDOW) {
    if (auto *window = qobject_cast<QMainWindow *>(widget)) {
      wireWindowEvents(window, element);
    }
  }
}

void UIService::wireButtonEvents(QPushButton *btn, ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CLICK);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallback>(handler)) {
      auto callback = std::get<ui::UIEventCallback>(handler);
      QObject::connect(btn, &QPushButton::clicked, [callback]() {
        if (callback)
          callback();
      });
    }
  }
}

void UIService::wireInputEvents(QLineEdit *input, ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CHANGE);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(input, &QLineEdit::textChanged,
                       [callback](const QString &text) {
                         if (callback)
                           callback(text.toStdString());
                       });
    }
  }

  auto submitIt = element->events.find(ui::EventType::SUBMIT);
  if (submitIt != element->events.end()) {
    auto &handler = submitIt->second;
    if (std::holds_alternative<ui::UIEventCallback>(handler)) {
      auto callback = std::get<ui::UIEventCallback>(handler);
      QObject::connect(input, &QLineEdit::returnPressed, [callback]() {
        if (callback)
          callback();
      });
    }
  }
}

void UIService::wireTextEditEvents(QTextEdit *textEdit,
                                   ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CHANGE);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(textEdit, &QTextEdit::textChanged,
                       [callback, textEdit]() {
                         if (callback)
                           callback(textEdit->toPlainText().toStdString());
                       });
    }
  }
}

void UIService::wireCheckboxEvents(QCheckBox *checkbox,
                                   ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CHANGE);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(checkbox, &QCheckBox::stateChanged,
                       [callback](int state) {
                         if (callback)
                           callback(state == Qt::Checked ? "true" : "false");
                       });
    }
  }
}

void UIService::wireSliderEvents(QSlider *slider, ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CHANGE);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(slider, &QSlider::valueChanged, [callback](int value) {
        if (callback)
          callback(std::to_string(value));
      });
    }
  }
}

void UIService::wireDropdownEvents(QComboBox *dropdown,
                                   ui::UIElement *element) {
  auto it = element->events.find(ui::EventType::CHANGE);
  if (it != element->events.end()) {
    auto &handler = it->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(dropdown,
                       QOverload<int>::of(&QComboBox::currentIndexChanged),
                       [callback, dropdown](int) {
                         if (callback)
                           callback(dropdown->currentText().toStdString());
                       });
    }
  }

  auto selectIt = element->events.find(ui::EventType::SELECT);
  if (selectIt != element->events.end()) {
    auto &handler = selectIt->second;
    if (std::holds_alternative<ui::UIEventCallbackWithValue>(handler)) {
      auto callback = std::get<ui::UIEventCallbackWithValue>(handler);
      QObject::connect(dropdown,
                       QOverload<int>::of(&QComboBox::currentIndexChanged),
                       [callback, dropdown](int index) {
                         if (callback)
                           callback(std::to_string(index));
                       });
    }
  }
}

void UIService::wireWindowEvents(QMainWindow *window, ui::UIElement *element) {
  auto closeIt = element->events.find(ui::EventType::CLOSE);
  if (closeIt != element->events.end()) {
    auto &handler = closeIt->second;
    if (std::holds_alternative<ui::UIEventCallback>(handler)) {
      auto callback = std::get<ui::UIEventCallback>(handler);
      // Use close event filter
      window->installEventFilter(new QObject());
      QObject::connect(window, &QMainWindow::destroyed, [callback]() {
        if (callback)
          callback();
      });
    }
  }
}

// Style application
void UIService::applyStylesheet(QWidget *widget, ui::UIElement *element) {
  QString style;

  // Background color
  if (auto it = element->props.find("background"); it != element->props.end()) {
    if (std::holds_alternative<std::string>(it->second)) {
      style +=
          QString("background-color: %1; ")
              .arg(QString::fromStdString(std::get<std::string>(it->second)));
    }
  }

  // Foreground color
  if (auto it = element->props.find("color"); it != element->props.end()) {
    if (std::holds_alternative<std::string>(it->second)) {
      style +=
          QString("color: %1; ")
              .arg(QString::fromStdString(std::get<std::string>(it->second)));
    }
  }

  // Font size
  if (auto it = element->props.find("fontSize"); it != element->props.end()) {
    if (std::holds_alternative<int64_t>(it->second)) {
      style += QString("font-size: %1px; ")
                   .arg(static_cast<int>(std::get<int64_t>(it->second)));
    }
  }

  // Font family
  if (auto it = element->props.find("fontFamily"); it != element->props.end()) {
    if (std::holds_alternative<std::string>(it->second)) {
      style +=
          QString("font-family: %1; ")
              .arg(QString::fromStdString(std::get<std::string>(it->second)));
    }
  }

  // Padding
  if (auto it = element->props.find("padding"); it != element->props.end()) {
    if (std::holds_alternative<int64_t>(it->second)) {
      int pad = static_cast<int>(std::get<int64_t>(it->second));
      style += QString("padding: %1px; ").arg(pad);
    }
  }

  if (!style.isEmpty()) {
    widget->setStyleSheet(style);
  }
}

// Show/hide/close
void UIService::show(std::shared_ptr<ui::UIElement> element) {
  if (!element)
    return;

  // Realize if needed
  if (!element->realized) {
    realize(element);
  }

  auto it = windows_.find(element->id);
  if (it != windows_.end()) {
    it->second->show();
    element->visible = true;
  }
}

void UIService::hide(std::shared_ptr<ui::UIElement> element) {
  if (!element)
    return;

  auto it = windows_.find(element->id);
  if (it != windows_.end()) {
    it->second->hide();
    element->visible = false;
  }
}

void UIService::close(std::shared_ptr<ui::UIElement> element) {
  if (!element)
    return;

  auto it = windows_.find(element->id);
  if (it != windows_.end()) {
    it->second->close();
    // Remove from maps
    windows_.erase(it);
    widgets_.erase(element->id);

    openWindowCount_--;
    if (openWindowCount_ <= 0 && allWindowsClosedCallback_) {
      allWindowsClosedCallback_();
    }
  }
}

// Dialogs
void UIService::alert(const std::string &message) {
  ensureApp();
  QMessageBox::information(nullptr, "Alert", QString::fromStdString(message));
}

bool UIService::confirm(const std::string &message) {
  ensureApp();
  auto result =
      QMessageBox::question(nullptr, "Confirm", QString::fromStdString(message),
                            QMessageBox::Yes | QMessageBox::No);
  return result == QMessageBox::Yes;
}

std::string UIService::filePicker(const std::string &title) {
  ensureApp();
  QString path =
      QFileDialog::getOpenFileName(nullptr, QString::fromStdString(title));
  return path.toStdString();
}

std::string UIService::dirPicker(const std::string &title) {
  ensureApp();
  QString path =
      QFileDialog::getExistingDirectory(nullptr, QString::fromStdString(title));
  return path.toStdString();
}

void UIService::notify(const std::string &message, const std::string &type) {
  (void)type;
  ensureApp();
  // Use a simple message box for now
  QMessageBox::information(nullptr, "Notification",
                           QString::fromStdString(message));
}

// Event pumping
void UIService::pumpEvents(int timeoutMs) {
  if (!QApplication::instance())
    return;

  if (timeoutMs <= 0) {
    QApplication::processEvents();
  } else {
    QApplication::processEvents(QEventLoop::WaitForMoreEvents, timeoutMs);
  }
}

bool UIService::hasActiveWindows() const { return openWindowCount_ > 0; }

void UIService::onAllWindowsClosed(std::function<void()> callback) {
  allWindowsClosedCallback_ = std::move(callback);
}

// Get/set element value
std::string UIService::getValue(std::shared_ptr<ui::UIElement> element) {
  if (!element)
    return "";

  auto it = widgets_.find(element->id);
  if (it == widgets_.end())
    return "";

  QWidget *widget = it->second;

  if (element->type == ui::ElementType::INPUT) {
    if (auto *input = qobject_cast<QLineEdit *>(widget)) {
      return input->text().toStdString();
    }
  } else if (element->type == ui::ElementType::TEXTAREA) {
    if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
      return textEdit->toPlainText().toStdString();
    }
  } else if (element->type == ui::ElementType::CHECKBOX) {
    if (auto *checkbox = qobject_cast<QCheckBox *>(widget)) {
      return checkbox->isChecked() ? "true" : "false";
    }
  } else if (element->type == ui::ElementType::SLIDER) {
    if (auto *slider = qobject_cast<QSlider *>(widget)) {
      return std::to_string(slider->value());
    }
  } else if (element->type == ui::ElementType::DROPDOWN) {
    if (auto *dropdown = qobject_cast<QComboBox *>(widget)) {
      return dropdown->currentText().toStdString();
    }
  }

  return "";
}

void UIService::setValue(std::shared_ptr<ui::UIElement> element,
                         const std::string &value) {
  if (!element)
    return;

  // Update element property
  element->set("value", value);

  // Update widget if realized
  auto it = widgets_.find(element->id);
  if (it == widgets_.end())
    return;

  QWidget *widget = it->second;

  if (element->type == ui::ElementType::INPUT) {
    if (auto *input = qobject_cast<QLineEdit *>(widget)) {
      input->setText(QString::fromStdString(value));
    }
  } else if (element->type == ui::ElementType::TEXTAREA) {
    if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
      textEdit->setPlainText(QString::fromStdString(value));
    }
  } else if (element->type == ui::ElementType::CHECKBOX) {
    if (auto *checkbox = qobject_cast<QCheckBox *>(widget)) {
      checkbox->setChecked(value == "true");
    }
  } else if (element->type == ui::ElementType::SLIDER) {
    if (auto *slider = qobject_cast<QSlider *>(widget)) {
      try {
        slider->setValue(std::stoi(value));
      } catch (...) {
      }
    }
  } else if (element->type == ui::ElementType::DROPDOWN) {
    if (auto *dropdown = qobject_cast<QComboBox *>(widget)) {
      int index = dropdown->findText(QString::fromStdString(value));
      if (index >= 0) {
        dropdown->setCurrentIndex(index);
      }
    }
  } else if (element->type == ui::ElementType::PROGRESS) {
    if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
      try {
        progress->setValue(std::stoi(value));
      } catch (...) {
      }
    }
  }
}

// Menu elements
std::shared_ptr<ui::UIElement> UIService::menu(const std::string &title) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::MENU);
  elem->set("title", title);
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement>
UIService::menuItem(const std::string &label, const std::string &shortcut) {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::MENU_ITEM);
  elem->set("label", label);
  if (!shortcut.empty()) {
    elem->set("shortcut", shortcut);
  }
  elem->id = nextId_++;
  return elem;
}

std::shared_ptr<ui::UIElement> UIService::menuSeparator() {
  auto elem = std::make_shared<ui::UIElement>(ui::ElementType::MENU_SEP);
  elem->id = nextId_++;
  return elem;
}

// ============================================================================
// System Tray
// ============================================================================

void UIService::trayIcon(const std::string &iconPath,
                         const std::string &tooltip) {
  ensureApp();

  if (!trayIcon_) {
    trayIcon_ = new QSystemTrayIcon();
  }

  if (!iconPath.empty()) {
    trayIcon_->setIcon(QIcon(QString::fromStdString(iconPath)));
  }

  if (!tooltip.empty()) {
    trayIcon_->setToolTip(QString::fromStdString(tooltip));
  }
}

void UIService::trayMenu(std::shared_ptr<ui::UIElement> menu) {
  ensureApp();

  if (!trayIcon_) {
    trayIcon_ = new QSystemTrayIcon();
  }

  // Clean up old menu
  if (trayMenu_) {
    trayMenu_->clear();
  } else {
    trayMenu_ = new QMenu();
  }

  // Build menu from UIElement
  if (menu) {
    for (const auto &child : menu->children) {
      if (child->type == ui::ElementType::MENU_ITEM) {
        std::string label = child->getProp("label", std::string("Item"));
        std::string shortcut = child->getProp("shortcut", std::string(""));

        QString text = QString::fromStdString(label);
        if (!shortcut.empty()) {
          text += "\t" + QString::fromStdString(shortcut);
        }

        QAction *action = trayMenu_->addAction(text);

        // Connect click handler if present
        if (auto it = child->events.find("click"); it != child->events.end()) {
          auto &handler = it->second;
          if (std::holds_alternative<ui::UIEventCallback>(handler)) {
            auto callback = std::get<ui::UIEventCallback>(handler);
            QObject::connect(action, &QAction::triggered, [callback]() {
              if (callback)
                callback();
            });
          }
        }
      } else if (child->type == ui::ElementType::MENU_SEP) {
        trayMenu_->addSeparator();
      }
    }
  }

  trayIcon_->setContextMenu(trayMenu_);
}

void UIService::trayNotify(const std::string &title, const std::string &message,
                           const std::string &iconType) {
  ensureApp();

  if (!trayIcon_) {
    trayIcon_ = new QSystemTrayIcon();
  }

  QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
  if (iconType == "warning") {
    icon = QSystemTrayIcon::Warning;
  } else if (iconType == "error") {
    icon = QSystemTrayIcon::Critical;
  }

  trayIcon_->showMessage(QString::fromStdString(title),
                         QString::fromStdString(message), icon, 5000);
}

void UIService::trayShow() {
  if (trayIcon_) {
    trayIcon_->show();
  }
}

void UIService::trayHide() {
  if (trayIcon_) {
    trayIcon_->hide();
  }
}

bool UIService::trayIsVisible() const {
  return trayIcon_ && trayIcon_->isVisible();
}

} // namespace havel::host
