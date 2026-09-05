/*
 * QtBackend.cpp - Qt implementation of UIBackend
 * Only compiled when HAVE_QT_EXTENSION is defined (in-process Qt backend).
 */
#ifdef HAVE_QT_EXTENSION

#include "QtBackend.hpp"
#include "UIService.hpp"
#include "UIManager.hpp"
#include "extensions/qt/QtAltTabBackend.hpp"
#include "host/window/AltTabService.hpp"
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSystemTrayIcon>

namespace havel::host {

QtBackend::QtBackend() = default;
QtBackend::~QtBackend() = default;

bool QtBackend::isAvailable() const {
    // Qt is always available in this build
    return true;
}

bool QtBackend::initialize() {
    if (!service_) {
        service_ = std::make_unique<UIService>();
    }
    return true;
}

void QtBackend::shutdown() {
    service_.reset();
}

std::shared_ptr<ui::UIElement> QtBackend::window(const std::string &title) {
    return service_->window(title);
}

std::shared_ptr<ui::UIElement> QtBackend::panel(const std::string &side) {
    return service_->panel(side);
}

std::shared_ptr<ui::UIElement> QtBackend::modal(const std::string &title) {
    return service_->modal(title);
}

std::shared_ptr<ui::UIElement> QtBackend::text(const std::string &content) {
    return service_->text(content);
}

std::shared_ptr<ui::UIElement> QtBackend::label(const std::string &content) {
    return service_->label(content);
}

std::shared_ptr<ui::UIElement> QtBackend::image(const std::string &path) {
    return service_->image(path);
}

std::shared_ptr<ui::UIElement> QtBackend::icon(const std::string &name) {
    return service_->icon(name);
}

std::shared_ptr<ui::UIElement> QtBackend::divider() {
    return service_->divider();
}

std::shared_ptr<ui::UIElement> QtBackend::spacer(int size) {
    return service_->spacer(size);
}

std::shared_ptr<ui::UIElement> QtBackend::progress(int value, int max) {
    return service_->progress(value, max);
}

std::shared_ptr<ui::UIElement> QtBackend::spinner() {
    return service_->spinner();
}

std::shared_ptr<ui::UIElement> QtBackend::btn(const std::string &label) {
    return service_->btn(label);
}

std::shared_ptr<ui::UIElement> QtBackend::input(const std::string &placeholder) {
    return service_->input(placeholder);
}

std::shared_ptr<ui::UIElement> QtBackend::textarea(const std::string &placeholder) {
    return service_->textarea(placeholder);
}

std::shared_ptr<ui::UIElement> QtBackend::checkbox(const std::string &label, bool checked) {
    return service_->checkbox(label, checked);
}

std::shared_ptr<ui::UIElement> QtBackend::toggle(const std::string &label, bool value) {
    return service_->toggle(label, value);
}

std::shared_ptr<ui::UIElement> QtBackend::slider(int min, int max, int value) {
    return service_->slider(min, max, value);
}

std::shared_ptr<ui::UIElement> QtBackend::dropdown(const std::vector<std::string> &options) {
    return service_->dropdown(options);
}

std::shared_ptr<ui::UIElement> QtBackend::row() {
    return service_->row();
}

std::shared_ptr<ui::UIElement> QtBackend::col() {
    return service_->col();
}

std::shared_ptr<ui::UIElement> QtBackend::grid(int cols) {
    return service_->grid(cols);
}

std::shared_ptr<ui::UIElement> QtBackend::table(int rows, int cols) {
    auto elem = std::make_shared<ui::UIElement>(ui::ElementType::TABLE);
    elem->set("rows", static_cast<int64_t>(rows));
    elem->set("cols", static_cast<int64_t>(cols));
    return elem;
}

std::shared_ptr<ui::UIElement> QtBackend::flex(const std::string &direction) {
    auto elem = std::make_shared<ui::UIElement>(ui::ElementType::FLEX);
    elem->set("direction", direction);
    return elem;
}

std::shared_ptr<ui::UIElement> QtBackend::scroll() {
    return service_->scroll();
}

std::shared_ptr<ui::UIElement> QtBackend::canvas(int width, int height) {
    auto elem = std::make_shared<ui::UIElement>(ui::ElementType::CANVAS);
    elem->set("width", static_cast<int64_t>(width));
    elem->set("height", static_cast<int64_t>(height));
    elem->set("background", std::string("white"));
    return elem;
}

std::shared_ptr<ui::UIElement> QtBackend::menu(const std::string &title) {
    return service_->menu(title);
}

std::shared_ptr<ui::UIElement> QtBackend::menuItem(const std::string &label, const std::string &shortcut) {
    return service_->menuItem(label, shortcut);
}

std::shared_ptr<ui::UIElement> QtBackend::menuSeparator() {
    return service_->menuSeparator();
}

void QtBackend::realize(std::shared_ptr<ui::UIElement> element) {
    service_->realize(element);
}

void QtBackend::show(std::shared_ptr<ui::UIElement> window) {
    service_->show(window);
}

void QtBackend::hide(std::shared_ptr<ui::UIElement> window) {
    service_->hide(window);
}

void QtBackend::close(std::shared_ptr<ui::UIElement> window) {
    service_->close(window);
}

void QtBackend::alert(const std::string &message) {
    service_->alert(message);
}

bool QtBackend::confirm(const std::string &message) {
    return service_->confirm(message);
}

std::string QtBackend::filePicker(const std::string &title) {
    return service_->filePicker(title);
}

std::string QtBackend::dirPicker(const std::string &title) {
    return service_->dirPicker(title);
}

void QtBackend::notify(const std::string &message, const std::string &type) {
    service_->notify(message, type);
}

void QtBackend::pumpEvents(int timeoutMs) {
    service_->pumpEvents(timeoutMs);
}

int QtBackend::runEventLoop() {
    return service_->runEventLoop();
}

void QtBackend::quitEventLoop(int exitCode) {
    service_->quitEventLoop(exitCode);
}

void QtBackend::setApplicationMetadata(const ApplicationMetadata& meta) {
    service_->setApplicationMetadata(meta);
}

void QtBackend::resetPerRunState() {
    service_->resetPerRunState();
}

void QtBackend::setIdleCallback(std::function<void()> cb) {
    if (!service_) return;
    service_->setIdleCallback(std::move(cb));
}

bool QtBackend::hasActiveWindows() const {
    return service_->hasActiveWindows();
}

void QtBackend::onAllWindowsClosed(std::function<void()> callback) {
    service_->onAllWindowsClosed(callback);
}

std::string QtBackend::getValue(std::shared_ptr<ui::UIElement> element) {
    return service_->getValue(element);
}

void QtBackend::setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) {
    service_->setValue(element, value);
}

void QtBackend::trayIcon(const std::string &iconPath, const std::string &tooltip) {
    service_->trayIcon(iconPath, tooltip);
}

void QtBackend::trayMenu(std::shared_ptr<ui::UIElement> menu) {
    service_->trayMenu(menu);
}

void QtBackend::trayNotify(const std::string &title, const std::string &message, const std::string &iconType) {
    service_->trayNotify(title, message, iconType);
}

void QtBackend::trayShow() {
    service_->trayShow();
}

void QtBackend::trayHide() {
    service_->trayHide();
}

bool QtBackend::trayIsVisible() const {
    return service_->trayIsVisible();
}

void QtBackend::applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) {
  service_->applyStyle(element, key, value);
}

void QtBackend::canvasFlush(std::shared_ptr<ui::UIElement> canvas) {
  service_->canvasFlush(canvas);
}

void QtBackend::canvasClear(std::shared_ptr<ui::UIElement> canvas) {
  service_->canvasClear(canvas);
}


void QtBackend::canvasDrawLine(std::shared_ptr<ui::UIElement> canvasEl, int x1, int y1, int x2, int y2) {
    service_->canvasDrawLine(canvasEl, x1, y1, x2, y2);
}

void QtBackend::canvasDrawRect(std::shared_ptr<ui::UIElement> canvasEl, int x, int y, int w, int h) {
    service_->canvasDrawRect(canvasEl, x, y, w, h);
}

void QtBackend::canvasDrawCircle(std::shared_ptr<ui::UIElement> canvasEl, int cx, int cy, int r) {
    service_->canvasDrawCircle(canvasEl, cx, cy, r);
}

void QtBackend::canvasSetPen(std::shared_ptr<ui::UIElement> canvasEl, int r, int g, int b, int width) {
    service_->canvasSetPen(canvasEl, r, g, b, width);
}

void QtBackend::canvasFill(std::shared_ptr<ui::UIElement> canvasEl, int x, int y) {
    service_->canvasFill(canvasEl, x, y);
}

void QtBackend::canvasBeginStroke(std::shared_ptr<ui::UIElement> canvasEl) {
    service_->canvasBeginStroke(canvasEl);
}

void QtBackend::canvasEndStroke(std::shared_ptr<ui::UIElement> canvasEl) {
    service_->canvasEndStroke(canvasEl);
}

bool QtBackend::canvasUndo(std::shared_ptr<ui::UIElement> canvasEl) {
    return service_->canvasUndo(canvasEl);
}

std::vector<int> QtBackend::canvasLassoSelect(std::shared_ptr<ui::UIElement> canvasEl, int x, int y) {
    return service_->canvasLassoSelect(canvasEl, x, y);
}

int64_t QtBackend::timerCreate(int intervalMs, bool singleShot, TimerCallback cb) {
    return service_->timerCreate(intervalMs, singleShot, std::move(cb));
}

void QtBackend::timerStart(int64_t timerId) {
    service_->timerStart(timerId);
}

void QtBackend::timerStop(int64_t timerId) {
    service_->timerStop(timerId);
}

bool QtBackend::timerIsActive(int64_t timerId) const {
    return service_->timerIsActive(timerId);
}

void QtBackend::timerSetInterval(int64_t timerId, int intervalMs) {
    service_->timerSetInterval(timerId, intervalMs);
}

void QtBackend::timerSetSingleShot(int64_t timerId, bool singleShot) {
    service_->timerSetSingleShot(timerId, singleShot);
}

void QtBackend::timerDestroy(int64_t timerId) {
    service_->timerDestroy(timerId);
}

void *QtBackend::settingsCreate(const std::string &org, const std::string &app) {
    return service_->settingsCreate(org, app);
}

void QtBackend::settingsDestroy(void *settings) {
    service_->settingsDestroy(settings);
}

void QtBackend::settingsSetValue(void *settings, const std::string &key, const std::string &value) {
    service_->settingsSetValue(settings, key, value);
}

std::string QtBackend::settingsValue(void *settings, const std::string &key, const std::string &defaultValue) {
    return service_->settingsValue(settings, key, defaultValue);
}

bool QtBackend::settingsContains(void *settings, const std::string &key) {
    return service_->settingsContains(settings, key);
}

void QtBackend::settingsRemove(void *settings, const std::string &key) {
    service_->settingsRemove(settings, key);
}

void QtBackend::settingsSync(void *settings) {
    service_->settingsSync(settings);
}

std::string QtBackend::colorPicker(const std::string &initialColor) {
    return service_->colorPicker(initialColor);
}

std::string QtBackend::fontPicker(const std::string &initialFont) {
    return service_->fontPicker(initialFont);
}

std::string QtBackend::inputText(const std::string &title, const std::string &label, const std::string &defaultValue) {
    return service_->inputText(title, label, defaultValue);
}

int64_t QtBackend::inputInt(const std::string &title, const std::string &label, int defaultValue, int min, int max, int step) {
    return service_->inputInt(title, label, defaultValue, min, max, step);
}

void *QtBackend::getScreenshotManager() {
#ifdef HAVE_QT_EXTENSION
    return static_cast<void*>(service_->getScreenshotManager());
#else
    return nullptr;
#endif
}

void QtBackend::ensureAltTabBackend() {
#ifdef HAVE_QT_EXTENSION
    static bool initialized = false;
    if (!initialized) {
        havel::AltTabService::instance().setBackend(
            std::make_unique<QtAltTabBackend>());
        initialized = true;
    }
#endif
}

// ===== High-level dialog implementations =====

std::string QtBackend::showMenu(const std::string &title, const std::vector<std::string> &options, bool multiSelect) {
    if (options.empty()) return "";
    
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
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        if (multiSelect) {
            std::vector<std::string> selected;
            for (auto *item : listWidget->selectedItems()) {
                selected.push_back(item->text().toStdString());
            }
            // Return as JSON-like string for multiple selections
            std::string result = "[";
            for (size_t i = 0; i < selected.size(); ++i) {
                if (i > 0) result += ",";
                result += """ + selected[i] + """;
            }
            result += "]";
            return result;
        } else {
            auto *item = listWidget->currentItem();
            if (item) return item->text().toStdString();
        }
    }
    return "";
}

std::string QtBackend::showContextMenu(const std::vector<std::string> &options) {
    if (options.empty()) return "";
    
    QMenu menu;
    QActionGroup group(&menu);
    group.setExclusive(true);

    for (const auto &option : options) {
        QAction *action = menu.addAction(QString::fromStdString(option));
        action->setCheckable(true);
        group.addAction(action);
    }

    QAction *selected = menu.exec(QCursor::pos());
    if (selected) return selected->text().toStdString();
    return "";
}

std::string QtBackend::showInputDialog(const std::string &title, const std::string &prompt, const std::string &defaultValue) {
    bool ok = false;
    QString result = QInputDialog::getText(nullptr, 
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QLineEdit::Normal,
        QString::fromStdString(defaultValue),
        &ok);
    if (ok) return result.toStdString();
    return "";
}

std::string QtBackend::showPasswordDialog(const std::string &title, const std::string &prompt) {
    bool ok = false;
    QString result = QInputDialog::getText(nullptr,
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        QLineEdit::Password,
        "",
        &ok);
    if (ok) return result.toStdString();
    return "";
}

double QtBackend::showNumberDialog(const std::string &title, const std::string &prompt, double defaultValue, double min, double max, double step) {
    bool ok = false;
    double value = QInputDialog::getDouble(nullptr,
        QString::fromStdString(title),
        QString::fromStdString(prompt),
        defaultValue, min, max, 2, &ok, Qt::WindowFlags(), step);
    if (ok) return value;
    return defaultValue;
}

std::string QtBackend::showFileDialog(const std::string &title, const std::string &startDir, const std::string &filter, bool save) {
    QString result;
    if (save) {
        result = QFileDialog::getSaveFileName(nullptr,
            QString::fromStdString(title),
            QString::fromStdString(startDir),
            QString::fromStdString(filter));
    } else {
        result = QFileDialog::getOpenFileName(nullptr,
            QString::fromStdString(title),
            QString::fromStdString(startDir),
            QString::fromStdString(filter));
    }
    return result.toStdString();
}

std::string QtBackend::showDirectoryDialog(const std::string &title, const std::string &startDir) {
    QString result = QFileDialog::getExistingDirectory(nullptr,
        QString::fromStdString(title),
        QString::fromStdString(startDir),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    return result.toStdString();
}

std::string QtBackend::showColorPicker(const std::string &title, const std::string &defaultColor) {
    QColor initial(Qt::white);
    if (!defaultColor.empty()) {
        initial = QColor(QString::fromStdString(defaultColor));
    }
    QColor color = QColorDialog::getColor(initial, nullptr, QString::fromStdString(title), QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
        return color.name(QColor::HexArgb).toStdString();
    }
    return "";
}

bool QtBackend::showConfirmDialog(const std::string &title, const std::string &message) {
    return QMessageBox::question(nullptr,
        QString::fromStdString(title),
        QString::fromStdString(message),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

void QtBackend::showNotification(const std::string &title, const std::string &message, const std::string &icon, int durationMs) {
    QSystemTrayIcon::MessageIcon iconType = QSystemTrayIcon::Information;
    if (icon == "warning") iconType = QSystemTrayIcon::Warning;
    else if (icon == "error") iconType = QSystemTrayIcon::Critical;
    else if (icon == "success") iconType = QSystemTrayIcon::Information;
    
    // Create a temporary system tray icon for notification
    static QSystemTrayIcon *tray = nullptr;
    if (!tray) {
        tray = new QSystemTrayIcon(qApp);
        tray->setVisible(true);
    }
    tray->showMessage(QString::fromStdString(title), QString::fromStdString(message), iconType, durationMs > 0 ? durationMs : 5000);
}

bool QtBackend::setActiveWindowTransparency(double opacity) {
    QWidget *active = QApplication::activeWindow();
    if (!active) return false;
    
    active->setWindowOpacity(std::clamp(opacity, 0.0, 1.0));
    return true;
}

bool QtBackend::setWindowTransparencyById(uint64_t windowId, double opacity) {
    WId id = static_cast<WId>(windowId);
    QWidget *widget = QWidget::find(id);
    if (!widget) return false;
    
    widget->setWindowOpacity(std::clamp(opacity, 0.0, 1.0));
    return true;
}

bool QtBackend::setWindowTransparencyByTitle(const std::string &title, double opacity) {
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->windowTitle().contains(QString::fromStdString(title), Qt::CaseInsensitive)) {
            widget->setWindowOpacity(std::clamp(opacity, 0.0, 1.0));
            return true;
        }
    }
    return false;
}

} // namespace havel::host

#endif // HAVE_QT_EXTENSION
