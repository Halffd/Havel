#include "ScreenshotService.hpp"

namespace havel::host {

ScreenshotService& ScreenshotService::getInstance() {
    static ScreenshotService instance;
    return instance;
}

void ScreenshotService::setBackend(std::unique_ptr<IScreenshotBackend> backend) {
    backend_ = std::move(backend);
}

IScreenshotBackend* ScreenshotService::backend() const {
    return backend_.get();
}

std::vector<unsigned char> ScreenshotService::captureFullDesktop(const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureFullDesktop(style);
}

std::vector<unsigned char> ScreenshotService::captureMonitor(int index, const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureMonitor(index, style);
}

std::vector<unsigned char> ScreenshotService::captureActiveWindow(const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureActiveWindow(style);
}

std::vector<unsigned char> ScreenshotService::captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureRegion(x, y, width, height, style);
}

int ScreenshotService::getMonitorCount() const {
    if (!backend_) return 0;
    return backend_->getMonitorCount();
}

std::vector<int> ScreenshotService::getMonitorGeometry(int index) const {
    if (!backend_) return {};
    return backend_->getMonitorGeometry(index);
}

} // namespace havel::host
