#include "AltTabService.hpp"
#include "core/window/WindowQuery.hpp"

namespace havel::host {

AltTabService::AltTabService() = default;
AltTabService::~AltTabService() = default;

void AltTabService::setBackend(std::unique_ptr<IAltTabBackend> backend) {
    backend_ = std::move(backend);
}

IAltTabBackend* AltTabService::backend() const {
    return backend_.get();
}

void AltTabService::show() {
    if (backend_) backend_->show();
}

void AltTabService::hide() {
    if (backend_) backend_->hide();
}

void AltTabService::toggle() {
    if (backend_) {
        if (backend_->isVisible()) backend_->hide();
        else backend_->show();
    }
}

void AltTabService::next() {
    if (backend_) backend_->next();
}

void AltTabService::previous() {
    if (backend_) backend_->previous();
}

void AltTabService::select() {
    if (backend_) backend_->select();
}

void AltTabService::refresh() {
    if (backend_) backend_->refresh();
}

std::vector<AltTabInfo> AltTabService::getWindows() const {
    auto allWindows = havel::WindowQuery::getAll();
    std::vector<AltTabInfo> result;
    result.reserve(allWindows.size());
    for (const auto& w : allWindows) {
        AltTabInfo info;
        info.title = w.title;
        info.className = w.windowClass;
        info.processName = w.exe;
        info.windowId = static_cast<int64_t>(w.id);
        info.active = false;
        result.push_back(info);
    }
    return result;
}

int AltTabService::getWindowCount() const {
    return static_cast<int>(getWindows().size());
}

void AltTabService::setThumbnailSize(int width, int height) {
    if (backend_) backend_->setThumbnailSize(width, height);
}

int AltTabService::getThumbnailWidth() const {
    return backend_ ? 200 : 100;
}

int AltTabService::getThumbnailHeight() const {
    return backend_ ? 150 : 75;
}

void AltTabService::setMaxVisibleWindows(int count) {
    (void)count;
}

int AltTabService::getMaxVisibleWindows() const {
    return 10;
}

void AltTabService::setAnimationsEnabled(bool enabled) {
    (void)enabled;
}

bool AltTabService::isAnimationsEnabled() const {
    return true;
}

} // namespace havel::host
