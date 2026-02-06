#include "GUIManager.hpp"
#include "utils/Logger.hpp"
#include <QApplication>
#include <QColorDialog>
#include <QCursor>
#include <QFileDialog>
#include <QGenericArgument>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace havel {

GUIManager::GUIManager(WindowManager &windowMgr)
    : QObject(nullptr), windowManager(windowMgr) {
  ensureQApplication();
}

GUIManager::~GUIManager() {
  // Clean up any open custom windows
  for (auto &[id, widget] : customWindows) {
    if (widget) {
      widget->close();
      delete widget;
    }
  }
  customWindows.clear();
}

void GUIManager::ensureQApplication() {
  if (!QApplication::instance()) {
    Logger::getInstance().warning("[GUIManager] QApplication not initialized, "
                                  "some GUI features may not work");
  }
}

// === MENU FUNCTIONS ===

std::string GUIManager::showMenu(const std::string &title,
                                 const std::vector<std::string> &options,
                                 bool multiSelect) {
  if (options.empty()) {
    return "";
  }

  QDialog dialog;
  dialog.setWindowTitle(QString::fromStdString(title));
  dialog.setModal(true);

  QVBoxLayout *layout = new QVBoxLayout(&dialog);

  QListWidget *listWidget = new QListWidget(&dialog);
  if (multiSelect) {
    listWidget->setSelectionMode(QAbstractItemView::MultiSelection);
  }

  for (const auto &option : options) {
    listWidget->addItem(QString::fromStdString(option));
  }

  layout->addWidget(listWidget);

  QPushButton *okButton = new QPushButton("OK", &dialog);
  QPushButton *cancelButton = new QPushButton("Cancel", &dialog);

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->addWidget(okButton);
  buttonLayout->addWidget(cancelButton);
  layout->addLayout(buttonLayout);

  QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancelButton, &QPushButton::clicked, &dialog,
                   &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    QList<QListWidgetItem *> selected = listWidget->selectedItems();
    if (!selected.isEmpty()) {
      if (multiSelect) {
        std::string result;
        for (int i = 0; i < selected.size(); ++i) {
          result += selected[i]->text().toStdString();
          if (i < selected.size() - 1) {
            result += ",";
          }
        }
        return result;
      } else {
        return selected.first()->text().toStdString();
      }
    }
  }

  return "";
}

std::string
GUIManager::showContextMenu(const std::vector<std::string> &options) {
  if (options.empty()) {
    return "";
  }

  QMenu menu;
  std::vector<QAction *> actions;

  for (const auto &option : options) {
    actions.push_back(menu.addAction(QString::fromStdString(option)));
  }

  QAction *selected = menu.exec(QCursor::pos());
  if (selected) {
    return selected->text().toStdString();
  }

  return "";
}

// === INPUT DIALOGS ===

std::string GUIManager::showInputDialog(const std::string &title,
                                        const std::string &prompt,
                                        const std::string &defaultValue) {
  bool ok;
  QString text = QInputDialog::getText(
      nullptr, QString::fromStdString(title), QString::fromStdString(prompt),
      QLineEdit::Normal, QString::fromStdString(defaultValue), &ok);

  return ok ? text.toStdString() : "";
}

std::string GUIManager::showPasswordDialog(const std::string &title,
                                           const std::string &prompt) {
  bool ok;
  QString text = QInputDialog::getText(nullptr, QString::fromStdString(title),
                                       QString::fromStdString(prompt),
                                       QLineEdit::Password, QString(), &ok);

  return ok ? text.toStdString() : "";
}

double GUIManager::showNumberDialog(const std::string &title,
                                    const std::string &prompt,
                                    double defaultValue, double min, double max,
                                    double step) {
  bool ok;
  double value = QInputDialog::getDouble(nullptr, QString::fromStdString(title),
                                         QString::fromStdString(prompt),
                                         defaultValue, min, max,
                                         2, // decimals
                                         &ok, Qt::WindowFlags(), step);

  return ok ? value : defaultValue;
}

// === CUSTOM WINDOWS ===

uint64_t GUIManager::createWindow(const std::string &title,
                                  const std::string &content, int width,
                                  int height) {
  QWidget *window = new QWidget();
  window->setWindowTitle(QString::fromStdString(title));
  window->resize(width, height);

  QVBoxLayout *layout = new QVBoxLayout(window);

  // Use QTextEdit for rich text/HTML support
  QTextEdit *textEdit = new QTextEdit(window);
  textEdit->setReadOnly(true);
  textEdit->setHtml(QString::fromStdString(content));

  layout->addWidget(textEdit);

  uint64_t id = nextWindowId++;
  customWindows[id] = window;

  window->show();

  return id;
}

void GUIManager::closeWindow(uint64_t windowId) {
  auto it = customWindows.find(windowId);
  if (it != customWindows.end()) {
    it->second->close();
    delete it->second;
    customWindows.erase(it);
  }
}

void GUIManager::updateWindowContent(uint64_t windowId,
                                     const std::string &content) {
  auto it = customWindows.find(windowId);
  if (it != customWindows.end()) {
    QTextEdit *textEdit = it->second->findChild<QTextEdit *>();
    if (textEdit) {
      textEdit->setHtml(QString::fromStdString(content));
    }
  }
}

// === NOTIFICATION FUNCTIONS ===

void GUIManager::showNotification(const std::string &title,
                                  const std::string &message,
                                  const std::string &icon, int durationMs) {
  // Check if we're on the main thread
  if (QApplication::instance()->thread() == QThread::currentThread()) {
    // We're on the main thread, show directly
    showNotificationImpl(QString::fromStdString(title),
                         QString::fromStdString(message),
                         QString::fromStdString(icon), durationMs);
  } else {
    // We're on a worker thread, dispatch to main thread
    QMetaObject::invokeMethod(
        this, "showNotificationImpl", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(title)),
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(QString, QString::fromStdString(icon)), Q_ARG(int, durationMs));
  }
}

void GUIManager::showNotificationImpl(const QString &title,
                                      const QString &message,
                                      const QString &icon, int durationMs) {
  // Use QMessageBox for simple notifications
  // In a production system, you'd want to use a proper notification daemon
  QMessageBox msgBox;
  msgBox.setWindowTitle(title);
  msgBox.setText(message);

  if (icon == "warning") {
    msgBox.setIcon(QMessageBox::Warning);
  } else if (icon == "error") {
    msgBox.setIcon(QMessageBox::Critical);
  } else if (icon == "success") {
    msgBox.setIcon(QMessageBox::Information);
  } else {
    msgBox.setIcon(QMessageBox::Information);
  }

  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.exec();
}

// === WINDOW TRANSPARENCY ===

bool GUIManager::setActiveWindowTransparency(double opacity) {
  wID activeWindow = windowManager.GetActiveWindow();
  return setWindowTransparency(activeWindow, opacity);
}

bool GUIManager::setWindowTransparency(uint64_t windowId, double opacity) {
  Display *display = XOpenDisplay(nullptr);
  if (!display) {
    Logger::getInstance().error("[GUIManager] Failed to open X11 display");
    return false;
  }

  // Clamp opacity to 0.0-1.0
  opacity = std::max(0.0, std::min(1.0, opacity));

  // Convert to X11 opacity format (0 = transparent, 0xffffffff = opaque)
  unsigned long opacityValue = static_cast<unsigned long>(opacity * 0xffffffff);

  Atom opacityAtom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", false);

  XChangeProperty(display, windowId, opacityAtom, XA_CARDINAL, 32,
                  PropModeReplace,
                  reinterpret_cast<unsigned char *>(&opacityValue), 1);

  XFlush(display);
  XCloseDisplay(display);

  return true;
}

bool GUIManager::setWindowTransparencyByTitle(const std::string &title,
                                              double opacity) {
  // Note: WindowManager doesn't have getWindowsByTitle, so we use FindByTitle
  wID window = WindowManager::FindByTitle(title.c_str());
  if (window == 0) {
    return false;
  }

  return setWindowTransparency(window, opacity);
}

// === DIALOG FUNCTIONS ===

bool GUIManager::showConfirmDialog(const std::string &title,
                                   const std::string &message) {
  QMessageBox::StandardButton reply = QMessageBox::question(
      nullptr, QString::fromStdString(title), QString::fromStdString(message),
      QMessageBox::Yes | QMessageBox::No);

  return reply == QMessageBox::Yes;
}

std::string GUIManager::showFileDialog(const std::string &title,
                                       const std::string &startDir,
                                       const std::string &filter, bool save) {
  QString fileName;

  if (save) {
    fileName = QFileDialog::getSaveFileName(
        nullptr, QString::fromStdString(title),
        QString::fromStdString(startDir), QString::fromStdString(filter));
  } else {
    fileName = QFileDialog::getOpenFileName(
        nullptr, QString::fromStdString(title),
        QString::fromStdString(startDir), QString::fromStdString(filter));
  }

  return fileName.toStdString();
}

std::string GUIManager::showDirectoryDialog(const std::string &title,
                                            const std::string &startDir) {
  QString dirName = QFileDialog::getExistingDirectory(
      nullptr, QString::fromStdString(title), QString::fromStdString(startDir));

  return dirName.toStdString();
}

// === COLOR PICKER ===

std::string GUIManager::showColorPicker(const std::string &title,
                                        const std::string &defaultColor) {
  QColor initialColor = QColor(QString::fromStdString(defaultColor));
  QColor color = QColorDialog::getColor(initialColor, nullptr,
                                        QString::fromStdString(title));

  if (color.isValid()) {
    return color.name().toStdString();
  }

  return "";
}

QWidget *GUIManager::getQWidgetForWindow(uint64_t windowId) {
  auto it = customWindows.find(windowId);
  return (it != customWindows.end()) ? it->second : nullptr;
}

} // namespace havel
