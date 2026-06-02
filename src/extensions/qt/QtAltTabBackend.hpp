#pragma once

#include "host/window/IAltTabBackend.hpp"
#include "extensions/gui/alt_tab/AltTab.hpp"

#include <memory>

namespace havel::host {

class QtAltTabBackend : public IAltTabBackend {
public:
    QtAltTabBackend() {
        if (qApp) {
            altTab_ = std::make_shared<havel::AltTabWindow>();
        }
    }

    ~QtAltTabBackend() override = default;

    void show() override {
        if (altTab_) altTab_->showAltTab();
    }

    void hide() override {
        if (altTab_) altTab_->hideAltTab();
    }

    bool isVisible() const override {
        if (!altTab_) return false;
        return altTab_->isVisible();
    }

    void next() override {
        if (altTab_) altTab_->nextWindow();
    }

    void previous() override {
        if (altTab_) altTab_->prevWindow();
    }

    void select() override {
        if (altTab_) altTab_->selectCurrentWindow();
    }

    void refresh() override {
        if (altTab_) altTab_->refreshWindows();
    }

    void setThumbnailSize(int width, int height) override {
        if (altTab_) altTab_->setThumbnailSize(width, height);
    }

private:
    std::shared_ptr<havel::AltTabWindow> altTab_;
};

} // namespace havel::host
