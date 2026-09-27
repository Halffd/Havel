#include "ScreenshotService.hpp"
#ifdef HAVE_OPENCV
#include <opencv2/imgcodecs.hpp>
#endif
#include <cstring>

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

bool ScreenshotService::saveToFile(const ScreenshotResult& result,
                                   const std::string& path) {
    if (!result || path.empty()) return false;
#ifdef HAVE_OPENCV
    cv::Mat img(result.height, result.width, CV_8UC4);
    std::memcpy(img.data, result.data.data(), result.data.size());
    return cv::imwrite(path, img);
#else
    (void)result;
    (void)path;
    return false;
#endif
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
