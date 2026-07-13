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

    void next() override {
        if (altTab_) altTab_->nextWindow();
    }

    void prev() override {
        if (altTab_) altTab_->prevWindow();
    }

    bool isVisible() const override {
        if (!altTab_) return false;
        return altTab_->isVisible();
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

    int getThumbnailWidth() const override {
        if (!altTab_) return 0;
        return altTab_->getThumbnailWidth();
    }

    int getThumbnailHeight() const override {
        if (!altTab_) return 0;
        return altTab_->getThumbnailHeight();
    }

    void setMaxVisibleWindows(int count) override {
        if (altTab_) altTab_->setMaxVisibleWindows(count);
    }

    int getMaxVisibleWindows() const override {
        if (!altTab_) return 0;
        return altTab_->getMaxVisibleWindows();
    }

    void setAnimationsEnabled(bool enabled) override {
        if (altTab_) altTab_->setAnimationsEnabled(enabled);
    }

    bool isAnimationsEnabled() const override {
        if (!altTab_) return false;
        return altTab_->isAnimationsEnabled();
    }

    std::vector<std::string> getWindows() const override {
        if (!altTab_) return {};
        auto windows = altTab_->getWindows();
        std::vector<std::string> result;
        for (const auto& w : windows) {
            result.push_back(w.title);
        }
        return result;
    }

    int getWindowCount() const override {
        if (!altTab_) return 0;
        return altTab_->getWindowCount();
    }

private:
    std::shared_ptr<havel::AltTabWindow> altTab_;
};

} // namespace havel::host
