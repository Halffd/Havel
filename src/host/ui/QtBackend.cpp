/*
 * QtBackend.cpp - Qt implementation of UIBackend
 */
#include "QtBackend.hpp"
#include "UIService.hpp"

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

} // namespace havel::host
