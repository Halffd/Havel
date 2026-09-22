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

ScreenshotResult ScreenshotService::captureFullDesktop(const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureFullDesktop(style);
}

ScreenshotResult ScreenshotService::captureMonitor(int index, const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureMonitor(index, style);
}

ScreenshotResult ScreenshotService::captureActiveWindow(const ScreenshotStyle& style) {
    if (!backend_) return {};
    return backend_->captureActiveWindow(style);
}

ScreenshotResult ScreenshotService::captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style) {
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
