#include "AltTabService.hpp"
#include <memory>

namespace havel {

struct AltTabService::Impl {
    std::unique_ptr<IAltTabBackend> backend;
};

AltTabService::AltTabService() : pImpl(std::make_unique<Impl>()) {}
AltTabService::~AltTabService() = default;

void AltTabService::setBackend(std::unique_ptr<IAltTabBackend> backend) {
    pImpl->backend = std::move(backend);
}

void AltTabService::show() {
    if (pImpl->backend) pImpl->backend->show();
}

void AltTabService::hide() {
    if (pImpl->backend) pImpl->backend->hide();
}

void AltTabService::next() {
    if (pImpl->backend) pImpl->backend->next();
}

void AltTabService::prev() {
    if (pImpl->backend) pImpl->backend->prev();
}

bool AltTabService::isVisible() const {
    if (!pImpl->backend) return false;
    return pImpl->backend->isVisible();
}

void AltTabService::select() {
    if (pImpl->backend) pImpl->backend->select();
}

void AltTabService::refresh() {
    if (pImpl->backend) pImpl->backend->refresh();
}

void AltTabService::setThumbnailSize(int width, int height) {
    if (pImpl->backend) pImpl->backend->setThumbnailSize(width, height);
}

int AltTabService::getThumbnailWidth() const {
    if (!pImpl->backend) return 0;
    return pImpl->backend->getThumbnailWidth();
}

int AltTabService::getThumbnailHeight() const {
    if (!pImpl->backend) return 0;
    return pImpl->backend->getThumbnailHeight();
}

void AltTabService::setMaxVisibleWindows(int count) {
    if (pImpl->backend) pImpl->backend->setMaxVisibleWindows(count);
}

int AltTabService::getMaxVisibleWindows() const {
    if (!pImpl->backend) return 0;
    return pImpl->backend->getMaxVisibleWindows();
}

void AltTabService::setAnimationsEnabled(bool enabled) {
    if (pImpl->backend) pImpl->backend->setAnimationsEnabled(enabled);
}

bool AltTabService::isAnimationsEnabled() const {
    if (!pImpl->backend) return false;
    return pImpl->backend->isAnimationsEnabled();
}

std::vector<AltTabInfo> AltTabService::getWindows() const {
    return {};
}

int AltTabService::getWindowCount() const {
    if (!pImpl->backend) return 0;
    return pImpl->backend->getWindowCount();
}

void AltTabService::toggle() {
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

void AltTabService::previous() {
    prev();
}

} // namespace havel
