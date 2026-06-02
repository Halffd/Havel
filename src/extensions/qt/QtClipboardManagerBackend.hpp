#pragma once

#include "host/clipboard/IClipboardManagerBackend.hpp"
#include "extensions/gui/clipboard_manager/ClipboardManager.hpp"

namespace havel::host {

class QtClipboardManagerBackend : public IClipboardManagerBackend {
public:
    QtClipboardManagerBackend() {
        if (qApp) {
            manager_ = &havel::ClipboardManager::getInstance();
        }
    }

    ~QtClipboardManagerBackend() override = default;

    void addToHistory(const std::string &text) override {
        if (manager_) manager_->addToHistory(text);
    }

    void clearHistory() override {
        if (manager_) manager_->clearHistory();
    }

    std::string getHistoryItem(int index) const override {
        if (!manager_) return "";
        return manager_->getHistoryItem(index).toStdString();
    }

    int getHistoryCount() const override {
        if (!manager_) return 0;
        return manager_->getHistoryCount();
    }

    void pasteHistoryItem(int index) override {
        if (manager_) manager_->pasteHistoryItem(index);
    }

    void show() override {
        if (manager_) manager_->showAndFocus();
    }

    void hide() override {
        if (manager_) manager_->hide();
    }

    void toggleVisibility() override {
        if (manager_) manager_->toggleVisibility();
    }

    bool isEnabled() const override {
        if (!manager_) return false;
        return manager_->isEnabled();
    }

    void enable() override {
        if (manager_) manager_->enable();
    }

    void disable() override {
        if (manager_) manager_->disable();
    }

private:
    havel::ClipboardManager *manager_ = nullptr;
};

} // namespace havel::host
